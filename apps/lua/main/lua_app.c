/*
 * LUA - scripts on the watch
 *
 * A list of .lua files from /sdcard/lua, and a canvas for the one you tap.
 * The script says what it wants to happen by defining functions:
 *
 *     function init()            once, before the first frame
 *     function tick(dt)          every frame, dt in milliseconds
 *     function draw()            every frame, after tick
 *     function touch(x, y, ev)   ev is "down", "move" or "up"
 *
 * All four are optional. Everything a script can reach is the 'aos' table
 * (lx_api.c); there is no io, no os and no package, because their sources are
 * not compiled into the binary.
 *
 * The promise this app makes, and the reason it exists: a broken script is a
 * message with a line number on a black screen, never a reboot. Three things
 * hold it up:
 *
 *   - Every call into Lua goes through lua_pcall.
 *   - A count hook cuts a script that will not come back, because this runs
 *     in the LVGL task and a runaway 'while true do end' would hang the
 *     interface until the watchdog panicked. That is worse than a crash: the
 *     watch freezes with the script's last frame on screen.
 *   - The parser's own recursion is bounded by LUAI_MAXCCALLS=40, set in the
 *     CMakeLists with the measurement that explains the number.
 *
 * Drawing follows the house recipe: the script works in 184x224, this
 * upscales x2 into a 368x448 canvas that LVGL copies flat. Never
 * LV_IMAGE_ALIGN_STRETCH (129 ms a frame, measured in 2043).
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lx_api.h"
#include "lx_pixel.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef AOS_SIM
#include "esp_heap_caps.h"
#endif

#define LUA_MAX_SCRIPTS     24
#define LUA_MAX_SOURCE      (48 * 1024)     /* a script bigger than this is
                                             * not a script, it is a mistake */
#define LUA_FRAME_MS        20              /* the timer's period; the frame
                                             * takes what it takes */
#define LUA_BUDGET_MS       400             /* per call into Lua, before the
                                             * hook cuts it */
#define LUA_HOOK_COUNT      2000            /* instructions between checks:
                                             * ~0.3 ms at the measured
                                             * 7.5 M ops/s */

typedef struct {
    lv_obj_t  *root;
    lv_obj_t  *list;            /* the screen with one button per script  */
    lv_obj_t  *surface;         /* what the script draws on: an LVGL
                                 * canvas in the simulator, a bare clickable
                                 * object on the board, where the pixels go
                                 * to the panel by blit and LVGL never sees
                                 * them                                    */
    lv_obj_t  *message;         /* the error, over the canvas             */
    lv_obj_t  *hud;             /* the frame's cost, an LVGL label on top  */
    uint32_t   hud_at;          /* when it was last written                */
    uint16_t   ms_push;         /* of ms_screen, what the panel took       */

    uint16_t  *small;           /* LX_W x LX_H, what the script draws on  */
    uint16_t  *big;             /* 368x448, what LVGL shows               */
    lx_buf_t   buf;
    lx_ctx_t   api;

    lua_State *L;
    int        ref_tick;
    int        ref_draw;
    int        ref_touch;
    lv_timer_t *timer;
    uint32_t   last_frame;
    uint32_t   call_start;      /* for the hook: when this call began      */
    bool       stopped;         /* an error already killed this script     */

    char       names[LUA_MAX_SCRIPTS][48];
    int        count;
} lua_ctx_t;

/* The upscale, with the bytes swapped on the way.
 *
 * aos_hal_display_blit() wants RGB565 BIG-ENDIAN, which is what the panel
 * reads; lx_pixel works in the native order like every other app. Swapping
 * during the upscale is free -this loop already writes every destination
 * pixel- and it is here and not inside lx_pixel.c so that file stays byte for
 * byte the engine the games use. The repeated rows are a memcpy of a row that
 * is already swapped.
 *
 * Why blit at all: a full-screen canvas through LVGL costs about 95 ms a
 * frame on this board (measured by the Video app, v0.3.13, and again here:
 * 83 ms a frame with 12 of them spent by the script and the upscale). The
 * push alone is 16.5 ms.
 */
#ifndef AOS_SIM
static void expand_be(const uint16_t *src, uint16_t *dst)
{
    const int dw = LX_W * LX_SCALE;
    for (int y = 0; y < LX_H; y++) {
        uint16_t       *row = dst + (size_t)y * LX_SCALE * dw;
        const uint16_t *s   = src + (size_t)y * LX_W;
        /* One 32-bit store per source pixel instead of two 16-bit ones: at
         * LX_SCALE 2 the two destination pixels are the same value, so they
         * are one word. The alignment holds: 'big' comes from malloc and the
         * row starts at an even index of a 368-wide buffer. Measured on the
         * board, this took the screen's share of the frame from 35 ms to the
         * number in docs. */
        uint32_t *pair = (uint32_t *)row;
        for (int x = 0; x < LX_W; x++) {
            uint32_t c = __builtin_bswap16(s[x]);
            pair[x] = c | (c << 16);
        }
        for (int k = 1; k < LX_SCALE; k++) {
            memcpy(row + (size_t)k * dw, row, (size_t)dw * 2);
        }
    }
}
#endif

/* ==========================================================================
 * The script's side
 * ========================================================================== */

#ifndef AOS_SIM
/* Lua's heap, in PSRAM. Without this, lua_newstate uses realloc, and realloc
 * obeys CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024: Lua allocates in crumbs, so
 * every one of them would land in the scarce internal RAM. Measured on the
 * board: a state grown to 77 KB took the free executable RAM from 107 K down
 * to 56.8 K with the default allocator, and 52 bytes with this one, at a cost
 * of 2 % in speed. */
static void *lua_psram_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;
    if (nsize == 0) {
        heap_caps_free(ptr);
        return NULL;
    }
    return heap_caps_realloc(ptr, nsize, MALLOC_CAP_SPIRAM);
}
#endif

/* The hook that keeps a runaway script from freezing the watch. It is set on
 * LUA_MASKCOUNT, so it fires every LUA_HOOK_COUNT instructions no matter what
 * the script is doing -a bare 'while true do end' included. */
static void budget_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    lua_ctx_t *ctx = NULL;
    lua_getfield(L, LUA_REGISTRYINDEX, "aos_ctx");
    ctx = (lua_ctx_t *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (ctx && lv_tick_elaps(ctx->call_start) > LUA_BUDGET_MS) {
        luaL_error(L, "the script did not return in %d ms", LUA_BUDGET_MS);
    }
}

static void show_error(lua_ctx_t *ctx, const char *what)
{
    if (ctx->timer) {
        lv_timer_delete(ctx->timer);
        ctx->timer = NULL;
    }
    ctx->stopped = true;

    if (!ctx->message) {
        ctx->message = lv_label_create(ctx->root);
        lv_label_set_long_mode(ctx->message, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(ctx->message, 330);
        lv_obj_set_style_bg_color(ctx->message, lv_color_hex(0x300000), 0);
        lv_obj_set_style_bg_opa(ctx->message, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(ctx->message, 10, 0);
        lv_obj_set_style_radius(ctx->message, 8, 0);
        lv_obj_set_style_text_color(ctx->message, lv_color_hex(0xFF9F9F), 0);
        lv_obj_set_style_text_font(ctx->message, &aos_montserrat_14, 0);
        lv_obj_align(ctx->message, LV_ALIGN_BOTTOM_MID, 0, -12);
    }
    lv_label_set_text(ctx->message, what);
    lv_obj_remove_flag(ctx->message, LV_OBJ_FLAG_HIDDEN);
    aos_hal_beep(220, 120);
}

/* Every call into Lua goes through here: the budget is reset, the error is
 * caught, and the script is stopped with its message on screen. */
static bool call_lua(lua_ctx_t *ctx, int nargs)
{
    ctx->call_start = lv_tick_get();
    if (lua_pcall(ctx->L, nargs, 0, 0) != LUA_OK) {
        const char *msg = lua_tostring(ctx->L, -1);
        show_error(ctx, msg ? msg : "error with no message");
        lua_pop(ctx->L, 1);
        return false;
    }
    return true;
}

static void frame_cb(lv_timer_t *t)
{
    lua_ctx_t *ctx = (lua_ctx_t *)lv_timer_get_user_data(t);
    if (ctx->stopped) {
        return;
    }

    uint32_t dt = lv_tick_elaps(ctx->last_frame);
    ctx->api.ms_frame = (uint16_t)(dt > 0xFFFF ? 0xFFFF : dt);
    ctx->last_frame = lv_tick_get();

    uint32_t t_script = lv_tick_get();

    if (ctx->ref_tick != LUA_NOREF) {
        lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->ref_tick);
        lua_pushinteger(ctx->L, (lua_Integer)dt);
        if (!call_lua(ctx, 1)) return;
    }
    if (ctx->ref_draw != LUA_NOREF) {
        lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->ref_draw);
        if (!call_lua(ctx, 0)) return;
    }

    ctx->api.ms_script = (uint16_t)lv_tick_elaps(t_script);

    /* The upscale is ours and the flush is LVGL's, but from the script's
     * point of view they are the same thing -what the screen costs- so they
     * are timed together. The flush itself happens after this returns, inside
     * LVGL: what is measured here is the part we can move. */
    uint32_t t_screen = lv_tick_get();
#ifdef AOS_SIM
    lx_rect_t all = { 0, 0, LX_W, LX_H };
    lx_expand(ctx->small, ctx->big, &all);
    lv_obj_invalidate(ctx->surface);
#else
    lv_area_t a;
    lv_obj_get_coords(ctx->surface, &a);
    expand_be(ctx->small, ctx->big);
    uint32_t t_push = lv_tick_get();
    aos_hal_display_blit(a.x1, a.y1, LX_W * LX_SCALE, LX_H * LX_SCALE, ctx->big);
    ctx->ms_push = (uint16_t)lv_tick_elaps(t_push);
#endif
    ctx->api.ms_screen = (uint16_t)lv_tick_elaps(t_screen);

    /* The cost of the frame, as a label and not as pixels in the buffer,
     * because what the blit pushed is invisible to /api/captura and to the
     * simulator: this line is the only thing about a running script that can
     * be read from the Mac. Refreshed twice a second and not every frame: a
     * label redrawn beside a full-screen surface costs almost as much as
     * enlarging the surface (APP-GUIDE 6.5), and it would be measuring
     * itself. LVGL draws it AFTER the blit, which is why it survives. */
    if (ctx->hud && lv_tick_elaps(ctx->hud_at) > 500) {
        ctx->hud_at = lv_tick_get();
        lv_label_set_text_fmt(ctx->hud, "%d = %d+%d+%d  %d fps",
                              ctx->api.ms_frame, ctx->api.ms_script,
                              ctx->api.ms_screen - ctx->ms_push, ctx->ms_push,
                              ctx->api.ms_frame ? 1000 / ctx->api.ms_frame : 0);
    }
}

/* The finger, in the script's coordinates: it never learns that the screen is
 * twice the buffer. */
static void touch_cb(lv_event_t *e)
{
    lua_ctx_t *ctx = (lua_ctx_t *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    lv_point_t p = { 0, 0 };
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &p);
    }
    lv_area_t a;
    lv_obj_get_coords(ctx->surface, &a);

    ctx->api.touch_x    = (int16_t)((p.x - a.x1) / LX_SCALE);
    ctx->api.touch_y    = (int16_t)((p.y - a.y1) / LX_SCALE);
    ctx->api.touch_down = (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING);

    /* "down", "move" or "up", and not a boolean.
     *
     * LVGL sends PRESSING again on every frame the finger stays put, so a
     * script told only "the finger is down" cannot tell one tap from holding
     * still: the first version of cubo.lua added a cube per frame and the
     * count wrapped round three times during a single touch. The kind of
     * event is the script's business and this is the only place that knows
     * it. */
    const char *ev = code == LV_EVENT_PRESSED  ? "down" :
                     code == LV_EVENT_PRESSING ? "move" : "up";

    if (ctx->stopped || ctx->ref_touch == LUA_NOREF) {
        return;
    }
    lua_rawgeti(ctx->L, LUA_REGISTRYINDEX, ctx->ref_touch);
    lua_pushinteger(ctx->L, ctx->api.touch_x);
    lua_pushinteger(ctx->L, ctx->api.touch_y);
    lua_pushstring(ctx->L, ev);
    call_lua(ctx, 3);
}

/* ==========================================================================
 * Loading and unloading a script
 * ========================================================================== */

static void unload(lua_ctx_t *ctx)
{
    if (ctx->timer) {
        lv_timer_delete(ctx->timer);
        ctx->timer = NULL;
    }
    if (ctx->L) {
        lua_close(ctx->L);
        ctx->L = NULL;
    }
    ctx->ref_tick = ctx->ref_draw = ctx->ref_touch = LUA_NOREF;
    ctx->stopped = false;
    if (ctx->surface) {
        lv_obj_delete(ctx->surface);
        ctx->surface = NULL;
    }
    if (ctx->hud) {
        lv_obj_delete(ctx->hud);
        ctx->hud = NULL;
    }
    if (ctx->message) {
        lv_obj_delete(ctx->message);
        ctx->message = NULL;
    }
}

/* Picks up a global if it is a function, and keeps it in the registry so the
 * frame does not go through a global lookup -or through whatever the script
 * may have done to the globals table since. */
static int grab(lua_State *L, const char *name)
{
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return LUA_NOREF;
    }
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

static void run_script(lua_ctx_t *ctx, const char *name)
{
    char path[160];
    const char *sd = aos_hal_path_sd_root();
    snprintf(path, sizeof(path), "%s/lua/%s", sd ? sd : "/sdcard", name);

    /* Read it whole and hand it to luaL_loadbuffer instead of using
     * luaL_loadfile: the file is small, this way the chunk name is ours, and
     * the reader never sits inside the parser holding the card open. */
    FILE *f = fopen(path, "rb");
    if (!f) {
        show_error(ctx, path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    if (len <= 0 || len > LUA_MAX_SOURCE) {
        fclose(f);
        show_error(ctx, "the file is empty or too big");
        return;
    }
    char *src = (char *)malloc((size_t)len + 1);
    if (!src) {
        fclose(f);
        show_error(ctx, "no memory for the script");
        return;
    }
    size_t got = fread(src, 1, (size_t)len, f);
    fclose(f);
    src[got] = 0;

    lv_obj_clean(ctx->root);
    ctx->list = NULL;
    ctx->message = NULL;
    ctx->hud = NULL;

    /* On the board the frame goes to the panel by blit and LVGL is never told
     * about it, so the surface is only there to catch the finger and to give
     * the blit its absolute position. In the simulator there is no panel and
     * aos_hal_display_blit() says so, so the same buffer is an LVGL canvas
     * and everything is visible on the Mac -which is the only reason a script
     * can be designed without the watch in hand. */
#ifdef AOS_SIM
    ctx->surface = lv_canvas_create(ctx->root);
    lv_canvas_set_buffer(ctx->surface, ctx->big, LX_W * LX_SCALE, LX_H * LX_SCALE,
                         LV_COLOR_FORMAT_RGB565);
    lv_image_set_antialias(ctx->surface, false);
#else
    ctx->surface = lv_obj_create(ctx->root);
    lv_obj_remove_style_all(ctx->surface);
#endif
    lv_obj_set_size(ctx->surface, LX_W * LX_SCALE, LX_H * LX_SCALE);
    lv_obj_align(ctx->surface, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(ctx->surface, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ctx->surface, touch_cb, LV_EVENT_PRESSED, ctx);
    lv_obj_add_event_cb(ctx->surface, touch_cb, LV_EVENT_PRESSING, ctx);
    lv_obj_add_event_cb(ctx->surface, touch_cb, LV_EVENT_RELEASED, ctx);

    ctx->hud = lv_label_create(ctx->root);
    lv_label_set_text(ctx->hud, "");
    lv_obj_set_style_text_color(ctx->hud, lv_color_hex(0x5A6070), 0);
    lv_obj_set_style_text_font(ctx->hud, &aos_montserrat_14, 0);
    lv_obj_set_style_bg_color(ctx->hud, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->hud, LV_OPA_COVER, 0);
    lv_obj_align(ctx->hud, LV_ALIGN_TOP_RIGHT, -4, 2);
    ctx->hud_at = lv_tick_get();

    memset(ctx->small, 0, (size_t)LX_W * LX_H * 2);
    lx_buf_init(&ctx->buf, ctx->small, LX_W, LX_H);
    ctx->api.buf = &ctx->buf;
    ctx->api.t0  = lv_tick_get();

#ifdef AOS_SIM
    ctx->L = luaL_newstate();
#else
    ctx->L = lua_newstate(lua_psram_alloc, NULL);
#endif
    if (!ctx->L) {
        free(src);
        show_error(ctx, "no memory for the interpreter");
        return;
    }
    luaL_openlibs(ctx->L);
    lx_api_open(ctx->L, &ctx->api);

    lua_pushlightuserdata(ctx->L, ctx);
    lua_setfield(ctx->L, LUA_REGISTRYINDEX, "aos_ctx");
    lua_sethook(ctx->L, budget_hook, LUA_MASKCOUNT, LUA_HOOK_COUNT);

    char chunk[64];
    snprintf(chunk, sizeof(chunk), "@%s", name);
    int rc = luaL_loadbuffer(ctx->L, src, got, chunk);
    free(src);
    if (rc != LUA_OK) {
        const char *msg = lua_tostring(ctx->L, -1);
        show_error(ctx, msg ? msg : "it does not compile");
        return;
    }
    ctx->call_start = lv_tick_get();
    if (!call_lua(ctx, 0)) {
        return;                     /* the chunk itself blew up */
    }

    ctx->ref_tick  = grab(ctx->L, "tick");
    ctx->ref_draw  = grab(ctx->L, "draw");
    ctx->ref_touch = grab(ctx->L, "touch");

    lua_getglobal(ctx->L, "init");
    if (lua_isfunction(ctx->L, -1)) {
        if (!call_lua(ctx, 0)) return;
    } else {
        lua_pop(ctx->L, 1);
    }

    ctx->last_frame = lv_tick_get();
    ctx->timer = lv_timer_create(frame_cb, LUA_FRAME_MS, ctx);
}

/* ==========================================================================
 * The list
 * ========================================================================== */

static void pick_cb(lv_event_t *e)
{
    lua_ctx_t *ctx = (lua_ctx_t *)lv_event_get_user_data(e);
    lv_obj_t  *btn = (lv_obj_t *)lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (idx >= 0 && idx < ctx->count) {
        run_script(ctx, ctx->names[idx]);
    }
}

static void scan(lua_ctx_t *ctx)
{
    ctx->count = 0;
    const char *sd = aos_hal_path_sd_root();
    if (!sd) {
        return;
    }
    char dir[128];
    snprintf(dir, sizeof(dir), "%s/lua", sd);
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && ctx->count < LUA_MAX_SCRIPTS) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || strcasecmp(dot, ".lua") != 0 || e->d_name[0] == '.') {
            continue;
        }
        /* A name that does not fit is skipped rather than truncated: a
         * truncated one would sit in the list and then fail to open, which
         * is a worse bug than not being listed. */
        size_t n = strlen(e->d_name);
        if (n >= sizeof(ctx->names[0])) {
            continue;
        }
        memcpy(ctx->names[ctx->count], e->d_name, n + 1);
        ctx->count++;
    }
    closedir(d);
}

static void build_list(lua_ctx_t *ctx)
{
    lv_obj_clean(ctx->root);
    ctx->surface = NULL;
    ctx->message = NULL;
    ctx->hud = NULL;
    lv_obj_set_style_bg_color(ctx->root, lv_color_hex(0x05050C), 0);

    scan(ctx);

    ctx->list = lv_obj_create(ctx->root);
    lv_obj_remove_style_all(ctx->list);
    lv_obj_set_size(ctx->list, 368, 448);
    lv_obj_set_flex_flow(ctx->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(ctx->list, 14, 0);
    lv_obj_set_style_pad_row(ctx->list, 8, 0);

    lv_obj_t *title = lv_label_create(ctx->list);
    lv_label_set_text(title, "Lua");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &aos_montserrat_28, 0);

    if (ctx->count == 0) {
        lv_obj_t *empty = lv_label_create(ctx->list);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(empty, 330);
        lv_label_set_text(empty, _("No hay guiones en /lua de la tarjeta"));
        lv_obj_set_style_text_color(empty, lv_color_hex(0x8E8E93), 0);
        return;
    }

    for (int i = 0; i < ctx->count; i++) {
        lv_obj_t *btn = lv_button_create(ctx->list);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1C1C2E), 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, pick_cb, LV_EVENT_CLICKED, ctx);

        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, ctx->names[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(0xE8E8F0), 0);
        lv_obj_center(l);
    }
}

/* ==========================================================================
 * The app
 * ========================================================================== */

static void *lua_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    lua_ctx_t *ctx = (lua_ctx_t *)lv_malloc_zeroed(sizeof(lua_ctx_t));
    if (!ctx) {
        return NULL;
    }
    ctx->root = root;
    ctx->ref_tick = ctx->ref_draw = ctx->ref_touch = LUA_NOREF;

    /* malloc and not lv_malloc: these are big, and big means PSRAM, which is
     * where a canvas belongs (APP-GUIDE 6.2). */
    ctx->small = (uint16_t *)malloc((size_t)LX_W * LX_H * 2);
    ctx->big   = (uint16_t *)malloc((size_t)LX_W * LX_SCALE * LX_H * LX_SCALE * 2);
    if (!ctx->small || !ctx->big) {
        free(ctx->small);
        free(ctx->big);
        lv_free(ctx);
        return NULL;
    }

    /* LUA_RUN=cubo.lua opens straight into that script. On the board getenv
     * always returns NULL -there is no environment- so this costs nothing and
     * travels in the same binary; in the simulator it is the only way in,
     * because a scripted tap on a list button does not land reliably. The
     * house does this in Gemas and Clima for the same reason. */
    const char *run = getenv("LUA_RUN");
    if (run && *run) {
        lv_obj_set_style_bg_color(root, lv_color_hex(0x05050C), 0);
        run_script(ctx, run);
        return ctx;
    }

    build_list(ctx);
    return ctx;
}

static void lua_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    lua_ctx_t *ctx = (lua_ctx_t *)inst;

    /* The timer first: it reaches into the state, and the state is about to
     * go. The LVGL objects under root are the runtime's business. */
    if (ctx->timer) {
        lv_timer_delete(ctx->timer);
        ctx->timer = NULL;
    }
    if (ctx->L) {
        lua_close(ctx->L);
    }
    free(ctx->small);
    free(ctx->big);
    lv_free(ctx);
}

/* Back goes from a script to the list, and only then out of the app. */
static bool lua_back(aos_app_t *self, void *inst)
{
    (void)self;
    lua_ctx_t *ctx = (lua_ctx_t *)inst;
    if (ctx->surface || ctx->L) {
        unload(ctx);
        build_list(ctx);
        return true;
    }
    return false;
}

static bool lua_app_init(aos_app_t *app)
{
    app->desc.id       = "aos.lua";
    app->desc.name     = "Lua";
    app->desc.icon     = "Lua";
    app->desc.icon_vec = AOS_ICON_NONE;
    app->desc.color_a  = 0x2C2D72;
    app->desc.color_b  = 0x000080;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN;
    app->desc.order    = 900;

    app->create  = lua_create;
    app->destroy = lua_destroy;
    app->back    = lua_back;
    return true;
}

AOS_APP_ENTRY(lua_app_init);
