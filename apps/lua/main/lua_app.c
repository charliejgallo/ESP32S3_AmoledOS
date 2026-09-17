/*
 * LUA - the probe
 *
 * Not the app that will ship: the experiment that answers, on the board,
 * whether a Lua 5.4 interpreter can live inside a .so of AmoledOS. It runs
 * four canned scripts and prints what happened:
 *
 *   1. arithmetic        does the VM run at all
 *   2. a syntax error    does the watch survive a broken script
 *   3. a runtime error   does lua_pcall catch it and give a line number
 *   4. a loop            how many VM instructions per second from PSRAM
 *
 * Point 2 and 3 are the whole sales pitch of scripting -an error is a message
 * and not a reboot- so they are tested first and not last.
 *
 * Deliberately with the DEFAULT allocator (luaL_newstate -> realloc). The
 * sdkconfig has CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024 and Lua allocates in
 * crumbs well under that, so every table and every string lands in INTERNAL
 * RAM, which is the scarce one. This first version measures exactly that
 * damage -lua_gc(LUA_GCCOUNT) on screen, /api/mem around it- and the number
 * is what justifies moving the state to PSRAM afterwards.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef AOS_SIM
#include "esp_heap_caps.h"
#endif

#define LUA_BENCH_ITERS  200000

typedef struct {
    lv_obj_t *out;          /* the report, line by line */
    char      text[1024];
    lua_State *L;
} lua_ctx_t;

/* Lua's heap, in PSRAM.
 *
 * Without this, lua_newstate uses realloc, and realloc on this board obeys
 * CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024: anything under a kilobyte goes to
 * INTERNAL RAM. Lua allocates in crumbs -a table header, a string, a stack
 * slot- so every one of them lands in the scarce memory. Measured before
 * this function existed: a state grown to 77 KB took the free executable RAM
 * from 107 K down to 56.8 K.
 *
 * heap_caps_realloc(NULL, n, caps) behaves as malloc, and with nsize == 0
 * Lua means free. The .text of this .so already runs from PSRAM through the
 * MMU, so with this the interpreter costs internal RAM only for what the HAL
 * hands it. */
#ifndef AOS_SIM
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

static void say(lua_ctx_t *ctx, const char *fmt, ...) LV_FORMAT_ATTRIBUTE(2, 3);

static void say(lua_ctx_t *ctx, const char *fmt, ...)
{
    char line[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    size_t used = strlen(ctx->text);
    snprintf(ctx->text + used, sizeof(ctx->text) - used, "%s\n", line);
    lv_label_set_text(ctx->out, ctx->text);
}

/* Runs a chunk and reports. Never lets an error out: that is the point. */
static void run(lua_ctx_t *ctx, const char *what, const char *code)
{
    lua_State *L = ctx->L;
    int top = lua_gettop(L);

    if (luaL_loadstring(L, code) != LUA_OK) {
        say(ctx, "%s: compile -> %s", what, lua_tostring(L, -1));
        lua_settop(L, top);
        return;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        say(ctx, "%s: run -> %s", what, lua_tostring(L, -1));
        lua_settop(L, top);
        return;
    }
    say(ctx, "%s: %s", what, lua_tostring(L, -1) ? lua_tostring(L, -1) : "(nil)");
    lua_settop(L, top);
}

static void *lua_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    lua_ctx_t *ctx = lv_malloc_zeroed(sizeof(lua_ctx_t));
    if (!ctx) {
        return NULL;
    }

    lv_obj_set_style_bg_color(root, lv_color_hex(0x000018), 0);

    ctx->out = lv_label_create(root);
    lv_label_set_long_mode(ctx->out, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ctx->out, 340);
    lv_obj_align(ctx->out, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_set_style_text_color(ctx->out, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(ctx->out, &aos_montserrat_14, 0);
    lv_label_set_text(ctx->out, "");

#ifdef AOS_SIM
    ctx->L = luaL_newstate();
#else
    ctx->L = lua_newstate(lua_psram_alloc, NULL);
#endif
    if (!ctx->L) {
        say(ctx, "luaL_newstate() -> NULL: no RAM");
        return ctx;
    }
    luaL_openlibs(ctx->L);
    say(ctx, "%s", LUA_RELEASE);

    run(ctx, "sum",    "return 2 + 2");
    run(ctx, "floats", "return 1/3");
    run(ctx, "broken", "return 2 +");
    run(ctx, "nil",    "local t = nil\nreturn t.x");

    uint32_t t0 = lv_tick_get();
    char code[128];
    snprintf(code, sizeof(code),
             "local s = 0 for i = 1, %d do s = s + i * 2 end return s",
             LUA_BENCH_ITERS);
    run(ctx, "loop", code);
    uint32_t ms = lv_tick_elaps(t0);
    if (ms > 0) {
        /* Four VM instructions per turn of the loop, give or take: the two
         * arithmetic ones, the FORLOOP and the store. It is not a benchmark
         * of anything but itself; what it answers is the order of magnitude. */
        say(ctx, "%d ms -> ~%d k ops/s", (int)ms,
            (int)((uint64_t)LUA_BENCH_ITERS * 4 / (ms > 0 ? ms : 1)));
    }

    /* The parser's stack, which is what decides where the VM can live.
     *
     * Lua compiles by recursive descent: every level of nesting is another C
     * frame, and the task of LVGL has 16 KB with about 6 KB to spare. Lua
     * guards itself with LUAI_MAXCCALLS, 200 by default, so the question is
     * whether those 200 levels fit or whether a script with enough
     * parentheses takes the watch down.
     *
     * Measured the hard way on 2026-09-17: the answer is that it takes it
     * down. Ramping 20, 60, 120, 190 panicked the board, which came back on
     * its own with boot_reason=PANIC.
     *
     * So the depth is READ FROM THE CARD instead of being compiled in:
     * /sdcard/lua_depth.txt, one number. That way the threshold is bisected
     * over the portal -upload a number, open the app, read the screen- with
     * no rebuild and no cable, and the app that is installed is not a trap
     * that panics every time somebody opens it.
     */
    int depth = 20;
    {
        const char *root = aos_hal_path_sd_root();
        char path[96];
        snprintf(path, sizeof(path), "%s/lua_depth.txt", root ? root : "/sdcard");
        FILE *f = fopen(path, "r");
        if (f) {
            char buf[16] = "";
            if (fread(buf, 1, sizeof(buf) - 1, f) > 0) {
                int value = atoi(buf);
                if (value > 0 && value < 5000) {
                    depth = value;
                }
            }
            fclose(f);
        }
    }

    char *deep = lv_malloc(2 * depth + 16);
    if (deep) {
        char *w = deep;
        w += sprintf(w, "return ");
        for (int k = 0; k < depth; k++) *w++ = '(';
        *w++ = '1';
        for (int k = 0; k < depth; k++) *w++ = ')';
        *w = 0;
        say(ctx, "depth %d: about to compile", depth);
        run(ctx, "deep", deep);
        lv_free(deep);
    }

    /* And how deep a script may legitimately recurse, which is the price of
     * the guard above: the same counter limits both. */
    run(ctx, "rec 30",
        "local function f(n) if n == 0 then return 0 end return 1 + f(n-1) end return f(30)");
    run(ctx, "rec 100",
        "local function f(n) if n == 0 then return 0 end return 1 + f(n-1) end return f(100)");

    run(ctx, "rec 1000",
        "local function f(n) if n == 0 then return 0 end return 1 + f(n-1) end return f(1000)");

    /* A long script is a different thing from a deep one: 300 statements do
     * not nest, they just make the parser hold a bigger function. */
    {
        size_t len = 300 * 24 + 32;
        char *big = lv_malloc(len);
        if (big) {
            char *w = big;
            w += sprintf(w, "local s = 0\n");
            for (int k = 0; k < 300; k++) {
                w += sprintf(w, "s = s + %d\n", k);
            }
            sprintf(w, "return s");
            run(ctx, "300 lines", big);
            lv_free(big);
        }
    }

    say(ctx, "lua heap: %d KB", lua_gc(ctx->L, LUA_GCCOUNT));
    return ctx;
}

static void lua_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    lua_ctx_t *ctx = (lua_ctx_t *)inst;
    if (ctx->L) {
        lua_close(ctx->L);
    }
    lv_free(ctx);
}

static bool lua_app_init(aos_app_t *app)
{
    app->desc.id       = "aos.lua";
    app->desc.name     = "Lua";
    app->desc.icon     = "Lua";
    app->desc.icon_vec = AOS_ICON_NONE;
    app->desc.color_a  = 0x2C2D72;
    app->desc.color_b  = 0x000080;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE;
    app->desc.order    = 900;

    app->create  = lua_create;
    app->destroy = lua_destroy;
    return true;
}

AOS_APP_ENTRY(lua_app_init);
