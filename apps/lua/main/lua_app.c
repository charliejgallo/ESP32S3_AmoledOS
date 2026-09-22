/*
 * LUA - scripts on the watch
 *
 * Two ways in, the same script either way: a list of the .lua files in
 * /sdcard/lua, and -because this module declares one app per script- an entry
 * of its own in the launcher, with its name, its colour and its icon, beside
 * the apps written in C. The interpreter is paid for once; every script after
 * that is data.
 *
 * A script says what it wants to happen by defining functions:
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
 * Drawing: the script works in 184x224 and this upscales x2 to the panel's
 * 368x448. On the board the frame goes out with aos_hal_display_blit(),
 * because a full-screen canvas through LVGL costs about 95 ms (measured by
 * the Video app and again here: 12 fps against 25). In the simulator there is
 * no panel, so the same buffer is an LVGL canvas and everything is visible on
 * the Mac.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_ui.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lx_api.h"
#include "lx_pixel.h"

#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef AOS_SIM
#include "esp_heap_caps.h"
#endif

#define LUA_MAX_SCRIPTS     256

/* How many scripts also become apps of their own in the launcher.
 *
 * Lower than LUA_MAX_SCRIPTS on purpose: the list inside this app can show
 * everything on the card, but every launcher entry takes a slot of the
 * firmware's MAX_DYNAPPS, which the .so files already share. It was 16 when
 * that was 48; firmware v0.5.0 has 224, and 192 here leaves room for 32 .so
 * files next to a card full of scripts. On an older firmware the loader says
 * which script did not fit. A script past the last still runs: it is in the
 * list, it just does not get its own icon. */
#define LUA_MAX_APPS        192
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
    uint16_t   rows;            /* of LX_H, how many were pushed           */

    /* Two lists, and the difference matters.
     *
     * 'drawn' is what the SCRIPT touched: the primitives mark it, and it is
     * what has to be undone next frame. 'dirty' is what goes to the panel,
     * which is that plus whatever was undone at the start of this one.
     *
     * With one list the undo marks fed back into themselves: the first frame
     * is a whole screen -init() clears- so the second undid the whole screen
     * and marked it, and it stayed at 224 of 224 rows for ever. */
    lx_dirty_t drawn;
    lx_dirty_t dirty;

    /* The frozen background and what was pushed last time. With a background,
     * the app undoes the previous frame at the start of this one instead of
     * making the script erase: aos.background() in lx_api.c says why. */
    uint16_t  *back;
    lx_dirty_t prev;

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

    /* The script that is running, and what it looked like on disk when it
     * was loaded: the app watches those two numbers and reloads itself when
     * the file changes. That is what makes the browser's Save the only step
     * -write it on the Mac, look at the watch- instead of save, walk over,
     * back out, tap again. */
    bool       standalone;      /* opened as its own app, not from the list */
    char       running[48];
    uint32_t   watch_at;
    time_t     watch_mtime;
    long       watch_size;
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
static void expand_be(const uint16_t *src, uint16_t *dst, int from, int to)
{
    const int dw = LX_W * LX_SCALE;
    for (int y = from; y < to; y++) {
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

/* The dirty rectangles, reduced to bands of rows.
 *
 * aos_hal_display_blit() takes a PACKED w*h buffer, and a sub-rectangle of a
 * 368-wide frame is not packed: its rows sit 368 pixels apart. A full-width
 * band is, so it goes out straight from 'big' with no staging copy and no
 * extra memory.
 *
 * What that costs is width: a ball in the middle of the screen pushes its
 * whole rows, 368 pixels wide instead of 40. What it saves is everything
 * else, and the cost of a frame is bytes over SPI - the rows nothing touched
 * are not sent at all. Arbitrary rectangles would need a staging buffer and a
 * copy per rectangle; the number in docs/LUA.md is what says whether that is
 * ever worth writing.
 *
 * Returns how many bands, with their row ranges in the script's coordinates.
 */
#define LUA_MAX_BANDS   LX_MAX_DIRTY

static int bands_of(const lx_dirty_t *d, int16_t *y0, int16_t *y1)
{
    if (d->all) {
        y0[0] = 0;
        y1[0] = LX_H;
        return 1;
    }
    if (d->n == 0) {
        return 0;
    }

    int n = 0;
    for (int i = 0; i < d->n; i++) {
        y0[n] = d->r[i].y0;
        y1[n] = d->r[i].y1;
        n++;
    }
    /* Insertion sort by the top edge: n is at most LX_MAX_DIRTY, which is 18,
     * and this runs once a frame. */
    for (int i = 1; i < n; i++) {
        int16_t a = y0[i], b = y1[i];
        int j = i - 1;
        while (j >= 0 && y0[j] > a) {
            y0[j + 1] = y0[j];
            y1[j + 1] = y1[j];
            j--;
        }
        y0[j + 1] = a;
        y1[j + 1] = b;
    }
    /* Merge what overlaps or touches: two bands a row apart are cheaper as
     * one push than as two windows. */
    int out = 0;
    for (int i = 1; i < n; i++) {
        if (y0[i] <= y1[out]) {
            if (y1[i] > y1[out]) {
                y1[out] = y1[i];
            }
        } else {
            out++;
            y0[out] = y0[i];
            y1[out] = y1[i];
        }
    }
    out++;

    /* Above three quarters of the screen, one push beats several: each band
     * is a window the panel has to be told about, and the rows saved no
     * longer pay for the telling. */
    int rows = 0;
    for (int i = 0; i < out; i++) {
        rows += y1[i] - y0[i];
    }
    if (rows * 4 > LX_H * 3) {
        y0[0] = 0;
        y1[0] = LX_H;
        return 1;
    }
    return out;
}

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

/* What the portal's console reads: one file with the running script on the
 * first line and its error, if any, on the rest.
 *
 * A file and not an endpoint, on purpose. /api/download and /api/upload
 * already serve dir=lua, so the page gets this for free and the firmware
 * still knows nothing about Lua -the same arrangement as the .pato scripts
 * and the .pix drawings. The name starts with an underscore and not a dot
 * because safe_name() in the portal refuses dot-files, and it does not end
 * in .lua so neither the list on the watch nor the one in the browser shows
 * it as a script. */
static void write_state(lua_ctx_t *ctx, const char *error)
{
    const char *sd = aos_hal_path_sd_root();
    if (!sd) {
        return;
    }
    char path[160];
    snprintf(path, sizeof(path), "%s/lua/_estado.txt", sd);
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fprintf(f, "%s\n%s\n", ctx->running, error ? error : "");
    fclose(f);
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
    write_state(ctx, what);
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

    /* Undo the previous frame, if the script froze a background: copy back
     * what it drew, and mark those rows so they go out again. Without this a
     * script has to erase for itself, which only works over a flat colour. */
    lx_dirty_reset(&ctx->dirty);
    if (ctx->back) {
        if (ctx->prev.all) {
            memcpy(ctx->small, ctx->back, (size_t)LX_W * LX_H * 2);
            lx_dirty_all(&ctx->dirty);
        } else {
            for (int i = 0; i < ctx->prev.n; i++) {
                const lx_rect_t *r = &ctx->prev.r[i];
                lx_restore(ctx->small, ctx->back, r);
                lx_dirty_add(&ctx->dirty, r->x0, r->y0,
                             r->x1 - r->x0, r->y1 - r->y0);
            }
        }
    }

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

    /* What the script drew joins what was undone: together they are the rows
     * the panel has to be told about. */
    if (ctx->drawn.all) {
        lx_dirty_all(&ctx->dirty);
    } else {
        for (int i = 0; i < ctx->drawn.n; i++) {
            const lx_rect_t *r = &ctx->drawn.r[i];
            lx_dirty_add(&ctx->dirty, r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0);
        }
    }

    /* The upscale is ours and the flush is LVGL's, but from the script's
     * point of view they are the same thing -what the screen costs- so they
     * are timed together. The flush itself happens after this returns, inside
     * LVGL: what is measured here is the part we can move. */
    uint32_t t_screen = lv_tick_get();
    int16_t y0[LUA_MAX_BANDS], y1[LUA_MAX_BANDS];
    int bands = bands_of(&ctx->dirty, y0, y1);
    ctx->rows = 0;
    for (int b = 0; b < bands; b++) {
        ctx->rows += y1[b] - y0[b];
    }
    ctx->api.rows = ctx->rows;

#ifdef AOS_SIM
    for (int b = 0; b < bands; b++) {
        lx_rect_t r = { 0, y0[b], LX_W, y1[b] };
        lx_expand(ctx->small, ctx->big, &r);
    }
    if (bands) {
        lv_obj_invalidate(ctx->surface);
    }
#else
    lv_area_t a;
    lv_obj_get_coords(ctx->surface, &a);
    uint32_t push = 0;
    for (int b = 0; b < bands; b++) {
        expand_be(ctx->small, ctx->big, y0[b], y1[b]);
        /* A band is full width, so its rows ARE contiguous in 'big' and the
         * pointer into it is what the blit wants: no staging buffer. */
        const uint16_t *rows = ctx->big +
            (size_t)y0[b] * LX_SCALE * LX_W * LX_SCALE;
        uint32_t t_push = lv_tick_get();
        aos_hal_display_blit(a.x1, a.y1 + y0[b] * LX_SCALE,
                             LX_W * LX_SCALE, (y1[b] - y0[b]) * LX_SCALE, rows);
        push += lv_tick_elaps(t_push);
    }
    ctx->ms_push = (uint16_t)push;
#endif
    ctx->api.ms_screen = (uint16_t)lv_tick_elaps(t_screen);

    /* Reset AFTER the push, not before the script draws.
     *
     * Before, it threw away what init() had marked -the whole screen, from
     * its aos.clear()- so the first frame pushed only the balls and the rest
     * of the frame buffer was whatever malloc had left there. On the board it
     * does not show, because the panel only ever gets what is pushed; in the
     * simulator, where the same buffer is an LVGL canvas, it came out as
     * bands of garbage. This way a mark made by a touch between two frames
     * survives into the next one as well. */
    ctx->prev = ctx->drawn;     /* what to undo next frame */
    lx_dirty_reset(&ctx->drawn);

    /* The cost of the frame, as a label and not as pixels in the buffer,
     * because what the blit pushed is invisible to /api/captura and to the
     * simulator: this line is the only thing about a running script that can
     * be read from the Mac. Refreshed twice a second and not every frame: a
     * label redrawn beside a full-screen surface costs almost as much as
     * enlarging the surface (APP-GUIDE 6.5), and it would be measuring
     * itself. LVGL draws it AFTER the blit, which is why it survives. */
    if (ctx->hud && lv_tick_elaps(ctx->hud_at) > 500) {
        ctx->hud_at = lv_tick_get();
        lv_label_set_text_fmt(ctx->hud, "%d = %d+%d+%d  %d/%d  %d fps",
                              ctx->api.ms_frame, ctx->api.ms_script,
                              ctx->api.ms_screen - ctx->ms_push, ctx->ms_push,
                              ctx->rows, LX_H,
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

/* aos.background(): the copy is made here, where the buffers live. Allocated
 * the first time it is asked for, so a script that never calls this costs
 * nothing; called again, it re-freezes, which is how a script changes its
 * world between levels. */
static bool freeze_background(void *arg)
{
    lua_ctx_t *ctx = (lua_ctx_t *)arg;
    if (!ctx->back) {
        ctx->back = (uint16_t *)malloc((size_t)LX_W * LX_H * 2);
        if (!ctx->back) {
            return false;
        }
    }
    memcpy(ctx->back, ctx->small, (size_t)LX_W * LX_H * 2);
    lx_dirty_reset(&ctx->prev);     /* nothing of the old world to undo */
    return true;
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
    /* The background belongs to the script that froze it. Leaving the buffer
     * behind would have the next script's first frames restored from the
     * previous script's world, which is a ghost that would be very hard to
     * read. It goes, and a script that wants one asks again. */
    free(ctx->back);
    ctx->back = NULL;
    lx_dirty_reset(&ctx->prev);
    lx_dirty_reset(&ctx->drawn);
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

    /* Copied first: 'name' may be ctx->running itself when this is a reload,
     * and unload() does not touch it but a future one might. */
    char wanted[sizeof(ctx->running)];
    snprintf(wanted, sizeof(wanted), "%s", name);
    snprintf(ctx->running, sizeof(ctx->running), "%s", wanted);
    name = wanted;

    struct stat st;
    if (stat(path, &st) == 0) {
        ctx->watch_mtime = st.st_mtime;
        ctx->watch_size  = (long)st.st_size;
    }
    ctx->watch_at = lv_tick_get();

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
    ctx->api.buf    = &ctx->buf;
    ctx->api.dirty  = &ctx->drawn;
    ctx->api.freeze = freeze_background;
    ctx->api.app    = ctx;
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

    lx_dirty_all(&ctx->drawn);      /* the first frame pushes everything */
    write_state(ctx, NULL);         /* it loaded: the console goes quiet */
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
    free(ctx->back);
    lv_free(ctx);
}

/* Called about five times a second by the runtime. It watches the file the
 * running script came from and reloads when it changes on the card.
 *
 * Checked once a second and not on every call: a stat() goes through FatFs to
 * the card. FAT keeps the time to the nearest two seconds, which is why the
 * size counts too -two edits within the same second usually change the
 * length- and why a save that changes neither is missed. That is the price of
 * doing this with no help from the firmware, and it is cheap: saving again
 * picks it up. */
static void lua_watch(aos_app_t *self, void *inst)
{
    (void)self;
    lua_ctx_t *ctx = (lua_ctx_t *)inst;
    if (!ctx->running[0] || !ctx->surface) {
        return;
    }
    if (lv_tick_elaps(ctx->watch_at) < 1000) {
        return;
    }
    ctx->watch_at = lv_tick_get();

    const char *sd = aos_hal_path_sd_root();
    if (!sd) {
        return;
    }
    char path[160];
    snprintf(path, sizeof(path), "%s/lua/%s", sd, ctx->running);
    struct stat st;
    if (stat(path, &st) != 0) {
        return;                 /* deleted while running: leave it alone */
    }
    if (st.st_mtime == ctx->watch_mtime && (long)st.st_size == ctx->watch_size) {
        return;
    }

    char again[sizeof(ctx->running)];
    snprintf(again, sizeof(again), "%s", ctx->running);
    unload(ctx);
    run_script(ctx, again);
}

/* Back goes from a script to the list, and only then out of the app. */
static bool lua_back(aos_app_t *self, void *inst)
{
    (void)self;
    lua_ctx_t *ctx = (lua_ctx_t *)inst;
    /* A script opened from the launcher has no list behind it: back leaves
     * the app, which is what every other app does. */
    if (ctx->standalone) {
        return false;
    }
    if (ctx->surface || ctx->L) {
        unload(ctx);
        ctx->running[0] = '\0';
        build_list(ctx);
        return true;
    }
    return false;
}

/* ==========================================================================
 * One app per script
 *
 * The module tells the loader how many apps it brings and describes each one,
 * so a .lua on the card is an entry in the launcher with its name, its colour
 * and its icon, beside the apps written in C. The interpreter is paid for
 * once and every script after that is data.
 *
 * The index is NOT the identity. The loader writes down which app of the
 * module a slot is and asks for that index again when it reopens it, but by
 * then a script may have been added or deleted and the indices will have
 * moved. What an app IS comes from its id -"lua.cubo" is cubo.lua- which the
 * runtime keeps and which does not move. The scan is sorted for the same
 * reason the id exists: so that the same card gives the same answer twice.
 * ========================================================================== */

/* The scripts, sorted, as of the last time anyone asked. Filled by
 * app_scan(), which is called from aos_app_count() -the loader's first
 * question- and again from each describe, because on the board the module is
 * opened, asked, and closed. */
static char s_apps_names[LUA_MAX_APPS][48];
static int  s_apps_count;

/* Which scripts have an icon file next to them, seen in the same readdir.
 * Measured on the board: with 200 scripts in the folder, every fopen walks
 * the FAT directory and costs ~25 ms, and asking for an .aic that is not
 * there cost as much as reading the script. Past AIC_SEEN_MAX the set is
 * not trusted and app_icon() goes back to asking the card. */
#define AIC_SEEN_MAX 64
static char s_aic_names[AIC_SEEN_MAX][48];
static int  s_aic_count;
static bool s_aic_overflow;

static int by_name(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static void app_scan(void)
{
    s_apps_count = 0;
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
    s_aic_count = 0;
    s_aic_overflow = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (dot && strcasecmp(dot, ".aic") == 0) {
            size_t base = (size_t)(dot - e->d_name);
            if (s_aic_count < AIC_SEEN_MAX && base < sizeof(s_aic_names[0])) {
                memcpy(s_aic_names[s_aic_count], e->d_name, base);
                s_aic_names[s_aic_count][base] = '\0';
                s_aic_count++;
            } else {
                s_aic_overflow = true;
            }
            continue;
        }
        if (!dot || strcasecmp(dot, ".lua") != 0 || e->d_name[0] == '.' ||
            s_apps_count >= LUA_MAX_APPS) {
            continue;
        }
        /* Short enough that "lua." plus the name still fits in the loader's
         * 40-byte id. If it did not, the id would be TRUNCATED there and
         * create() would look for a file that does not exist -an app in the
         * launcher that opens onto an error. A long name is still in the
         * list inside this app, where nothing depends on its length. */
        size_t n = strlen(e->d_name);
        if (n >= sizeof(s_apps_names[0]) || n + 4 >= 40) {
            continue;
        }
        memcpy(s_apps_names[s_apps_count], e->d_name, n + 1);
        s_apps_count++;
    }
    closedir(d);
    qsort(s_apps_names, (size_t)s_apps_count, sizeof(s_apps_names[0]), by_name);

    /* Said out loud because the launcher is built once, at boot: a script
     * copied to the card afterwards is in the list inside this app straight
     * away and in the launcher only after a restart, and without this line
     * there is no way to tell that from a script the scan refused. */
    aos_hal_log("lua", "%s: %d script%s for the launcher",
                dir, s_apps_count, s_apps_count == 1 ? "" : "s");
}

/* The name shown in the launcher. A script may give itself one with a comment
 * on any of its first lines:
 *
 *     -- @name Cubo giratorio
 *
 * and without it the file name, minus the extension, is used. It is worth the
 * fifteen lines: otherwise every entry in the launcher is a lower-case file
 * name among apps that are called Burbujas and Pixel Art. */
static void app_label(const char *file, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s", file);
    char *dot = strrchr(out, '.');
    if (dot) {
        *dot = '\0';
    }

    const char *sd = aos_hal_path_sd_root();
    if (!sd) {
        return;
    }
    char path[160];
    snprintf(path, sizeof(path), "%s/lua/%s", sd, file);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    char head[256];
    size_t got = fread(head, 1, sizeof(head) - 1, f);
    fclose(f);
    head[got] = '\0';

    const char *tag = strstr(head, "@name");
    if (!tag) {
        return;
    }
    tag += 5;
    while (*tag == ' ' || *tag == '\t') tag++;
    size_t n = 0;
    while (tag[n] && tag[n] != '\n' && tag[n] != '\r' && n < out_len - 1) n++;
    while (n > 0 && (tag[n - 1] == ' ' || tag[n - 1] == '\t')) n--;
    if (n > 0) {
        memcpy(out, tag, n);
        out[n] = '\0';
    }
}

/* A colour per script, from its name. Not decoration: fifteen identical tiles
 * in the launcher are fifteen tiles you have to read one by one. */
static uint32_t app_hue(const char *name, bool second)
{
    static const uint32_t PAIRS[][2] = {
        { 0x0A84FF, 0x0050A0 }, { 0x30D158, 0x1A7F36 }, { 0xFF9F0A, 0xB36A00 },
        { 0xFF375F, 0xA61E3A }, { 0xBF5AF2, 0x7A2FA0 }, { 0x64D2FF, 0x2E8FB0 },
        { 0xFFD60A, 0xB39400 }, { 0x5E5CE6, 0x3A38A0 },
    };
    uint32_t h = 2166136261u;
    for (const char *c = name; *c; c++) {
        h = (h ^ (uint8_t)*c) * 16777619u;
    }
    return PAIRS[h % (sizeof(PAIRS) / sizeof(PAIRS[0]))][second ? 1 : 0];
}

/* An .aic beside the script gives it an icon, with no firmware and no
 * reflashing (docs/ICONS.md). /sdcard/icons/<id>.aic still works too and wins,
 * because that is the firmware's own override. */
static bool aic_listed(const char *file)
{
    if (s_aic_overflow) {
        return true;                    /* unknown: ask the card */
    }
    const char *dot = strrchr(file, '.');
    size_t base = dot ? (size_t)(dot - file) : strlen(file);
    for (int i = 0; i < s_aic_count; i++) {
        if (strlen(s_aic_names[i]) == base && strncmp(s_aic_names[i], file, base) == 0) {
            return true;
        }
    }
    return false;
}

static void app_icon(aos_app_t *app, const char *file)
{
    const char *sd = aos_hal_path_sd_root();
    if (!sd || !aic_listed(file)) {
        return;
    }
    char path[176];
    snprintf(path, sizeof(path), "%s/lua/%s", sd, file);
    char *dot = strrchr(path, '.');
    if (!dot) {
        return;
    }
    snprintf(dot, sizeof(path) - (size_t)(dot - path), ".aic");

    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    if (len > 0 && len <= 2048) {
        uint8_t *blob = (uint8_t *)malloc((size_t)len);
        if (blob) {
            if (fread(blob, 1, (size_t)len, f) == (size_t)len) {
                aos_icon_set_ops(app, blob, (size_t)len);   /* it copies it */
            }
            free(blob);
        }
    }
    fclose(f);
}

/* The script this app is, from its id: "lua.cubo" -> "cubo.lua". */
static void app_file_of(const char *id, char *out, size_t out_len)
{
    const char *base = id && strncmp(id, "lua.", 4) == 0 ? id + 4 : id;
    snprintf(out, out_len, "%s.lua", base ? base : "");
}

static void *script_create(aos_app_t *self, lv_obj_t *root)
{
    lua_ctx_t *ctx = (lua_ctx_t *)lua_create(self, root);
    if (!ctx) {
        return NULL;
    }
    ctx->standalone = true;

    char file[sizeof(ctx->running)];
    app_file_of(self->desc.id, file, sizeof(file));
    run_script(ctx, file);
    return ctx;
}

/* ========================================================================== */

static bool describe_list(aos_app_t *app)
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
    app->tick    = lua_watch;
    return true;
}

/* The strings of a descriptor have to outlive this call, and one set of
 * statics is NOT enough.
 *
 * aos_ui_register_app() does `s_apps[n] = *app`, a struct copy: it keeps the
 * POINTERS. On the board that is harmless, because the loader copies the
 * strings into a slot of its own first; but the simulator registers what the
 * module hands it, and with one shared buffer every script app ended up
 * pointing at the last name written. The symptom was a single line -"duplicate
 * app: lua.hola"- and the scripts missing from the launcher.
 *
 * One buffer per app, then. Sixteen of each is 1.6 KB.
 *
 * The id is 52: four for "lua." plus the longest file name app_scan() lets
 * through. The loader copies it into a 40-byte field of its own, and the scan
 * refuses anything that would not fit there -truncating the id is how you get
 * an app in the launcher that opens onto a file that does not exist. */
static char s_ids[LUA_MAX_APPS][52];
static char s_names[LUA_MAX_APPS][48];

static uint32_t lua_count(void)
{
    app_scan();
    return 1u + (uint32_t)s_apps_count;
}

static bool lua_describe(aos_app_t *app, uint32_t index)
{
    if (index == 0) {
        return describe_list(app);
    }
    if (s_apps_count == 0) {
        app_scan();             /* reopened on the board: scan again */
    }
    uint32_t i = index - 1;
    if (i >= (uint32_t)s_apps_count) {
        return false;
    }
    const char *file = s_apps_names[i];

    char *id   = s_ids[i];
    char *name = s_names[i];

    snprintf(id, sizeof(s_ids[0]), "lua.%s", file);
    char *dot = strrchr(id, '.');
    if (dot && strcasecmp(dot, ".lua") == 0) {
        *dot = '\0';
    }
    app_label(file, name, sizeof(s_names[0]));

    app->desc.id       = id;
    app->desc.name     = name;
    app->desc.icon     = LV_SYMBOL_PLAY;
    app->desc.icon_vec = AOS_ICON_NONE;
    app->desc.color_a  = app_hue(file, false);
    app->desc.color_b  = app_hue(file, true);
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN;
    app->desc.order    = 901 + (int32_t)i;

    app->create  = script_create;
    app->destroy = lua_destroy;
    app->back    = lua_back;
    app->tick    = lua_watch;

    app_icon(app, file);
    return true;
}

AOS_APP_ENTRY_MANY(lua_count, lua_describe);
