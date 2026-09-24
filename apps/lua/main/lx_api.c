/*
 * LX_API - the table a Lua script sees (see lx_api.h)
 */
#include "lx_api.h"

#include "aos_hal.h"
#include "aos_gesture.h"
#include "lauxlib.h"
#include "lvgl.h"

/* Coordinates a script may name. Wider than the buffer on purpose -drawing
 * something that comes in from off-screen is normal and the clip handles it-
 * but nowhere near where int arithmetic inside the primitives would wrap. */
#define LX_COORD_MAX    8192

static int clampc(lua_Integer v)
{
    if (v >  LX_COORD_MAX) return  LX_COORD_MAX;
    if (v < -LX_COORD_MAX) return -LX_COORD_MAX;
    return (int)v;
}

static int arg_coord(lua_State *L, int i)
{
    return clampc(luaL_checkinteger(L, i));
}

/* 0xRRGGBB from the script -readable in a script, which 565 is not- to what
 * the buffer and the panel speak. */
static uint16_t arg_color(lua_State *L, int i)
{
    lua_Integer hex = luaL_checkinteger(L, i);
    return lx_rgb((uint32_t)(hex & 0xFFFFFF));
}

static lx_ctx_t *ctx_of(lua_State *L)
{
    return (lx_ctx_t *)lua_touserdata(L, lua_upvalueindex(1));
}

/* What this call is about to change, so the app knows which rows to push.
 *
 * Deliberately the bounding box and not the pixels: a diagonal line marks the
 * rectangle it crosses, which is more than it dirties. Being generous here is
 * free -a row pushed twice looks the same- and being mean is a bug you see as
 * a stripe of the previous frame left on the panel. The clip is not consulted
 * either, for the same reason: lx_dirty_add clamps to the buffer anyway. */
static void mark(lx_ctx_t *c, int x, int y, int w, int h)
{
    if (c->dirty) {
        lx_dirty_add(c->dirty, x, y, w, h);
    }
}

static void mark_all(lx_ctx_t *c)
{
    if (c->dirty) {
        lx_dirty_all(c->dirty);
    }
}

/* --------------------------------------------------------------------------
 * Drawing
 * -------------------------------------------------------------------------- */

static int l_clear(lua_State *L)
{
    lx_ctx_t *c = ctx_of(L);
    lx_fill(c->buf, arg_color(L, 1));
    mark_all(c);
    return 0;
}

static int l_pixel(lua_State *L)
{
    lx_ctx_t *c = ctx_of(L);
    int x = arg_coord(L, 1), y = arg_coord(L, 2);
    lx_px(c->buf, x, y, arg_color(L, 3));
    mark(c, x, y, 1, 1);
    return 0;
}

static int l_rect(lua_State *L)
{
    lx_ctx_t *c = ctx_of(L);
    int x = arg_coord(L, 1), y = arg_coord(L, 2);
    int w = arg_coord(L, 3), h = arg_coord(L, 4);
    lx_rect(c->buf, x, y, w, h, arg_color(L, 5));
    mark(c, x, y, w, h);
    return 0;
}

static int l_frame(lua_State *L)
{
    lx_ctx_t *c = ctx_of(L);
    int x = arg_coord(L, 1), y = arg_coord(L, 2);
    int w = arg_coord(L, 3), h = arg_coord(L, 4);
    lx_frame(c->buf, x, y, w, h, arg_color(L, 5));
    mark(c, x, y, w, h);
    return 0;
}

static int l_line(lua_State *L)
{
    lx_ctx_t *c = ctx_of(L);
    int x0 = arg_coord(L, 1), y0 = arg_coord(L, 2);
    int x1 = arg_coord(L, 3), y1 = arg_coord(L, 4);
    lx_line(c->buf, x0, y0, x1, y1, arg_color(L, 5));
    mark(c, x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1,
         (x0 < x1 ? x1 - x0 : x0 - x1) + 1, (y0 < y1 ? y1 - y0 : y0 - y1) + 1);
    return 0;
}

static int l_disc(lua_State *L)
{
    lx_ctx_t *c = ctx_of(L);
    int cx = arg_coord(L, 1), cy = arg_coord(L, 2), r = arg_coord(L, 3);
    lx_disc(c->buf, cx, cy, r, arg_color(L, 4));
    mark(c, cx - r, cy - r, 2 * r + 1, 2 * r + 1);
    return 0;
}

static int l_ring(lua_State *L)
{
    lx_ctx_t *c = ctx_of(L);
    int cx = arg_coord(L, 1), cy = arg_coord(L, 2), r = arg_coord(L, 3);
    lx_ring(c->buf, cx, cy, r, arg_color(L, 4));
    mark(c, cx - r, cy - r, 2 * r + 1, 2 * r + 1);
    return 0;
}

/* The 5x7 font is ASCII, upper case, digits and signs: it is for scores and
 * counters. Anything a person has to read in their own language is a label,
 * which is the app's business and not the script's. */
static int l_text(lua_State *L)
{
    int scale = (int)luaL_optinteger(L, 5, 1);
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    lx_ctx_t *c = ctx_of(L);
    int x = arg_coord(L, 1), y = arg_coord(L, 2);
    size_t n = 0;
    const char *str = luaL_checklstring(L, 3, &n);
    lx_text(c->buf, x, y, str, arg_color(L, 4), scale);
    mark(c, x, y, (int)n * LX_CH_ADV * scale, LX_CH_H * scale);
    return 0;
}

static int l_shade(lua_State *L)
{
    lua_Integer f = luaL_checkinteger(L, 5);
    if (f >  16) f =  16;
    if (f < -16) f = -16;
    lx_ctx_t *c = ctx_of(L);
    int x = arg_coord(L, 1), y = arg_coord(L, 2);
    int w = arg_coord(L, 3), h = arg_coord(L, 4);
    lx_shade(c->buf, x, y, w, h, (int)f);
    mark(c, x, y, w, h);
    return 0;
}

/* --------------------------------------------------------------------------
 * The world outside the buffer
 * -------------------------------------------------------------------------- */

/* x, y, down. The coordinates are already in the script's 184x224, divided by
 * the app: a script never learns that the screen is twice that. */
static int l_touch(lua_State *L)
{
    lx_ctx_t *c = ctx_of(L);
    lua_pushinteger(L, c->touch_x);
    lua_pushinteger(L, c->touch_y);
    lua_pushboolean(L, c->touch_down);
    return 3;
}

/* aos.fingers(): how many fingers are down, then id, x, y of each, in the
 * script's pixels. For pads: each finger keeps its id while it stays down
 * (aos_touch_points), so "the stick's finger" can be told from "the fire
 * button's". */
static int l_fingers(lua_State *L)
{
    aos_touch_point_t pts[2];
    int n = aos_touch_points(pts);
    lua_pushinteger(L, n);
    int pushed = 1;
    for (int i = 0; i < 2; i++) {
        if (!pts[i].down) continue;
        lua_pushinteger(L, pts[i].id);
        lua_pushinteger(L, (lua_Integer)(pts[i].x / LX_SCALE));
        lua_pushinteger(L, (lua_Integer)(pts[i].y / LX_SCALE));
        pushed += 3;
    }
    return pushed;
}

/* Milliseconds since the script started, not since the watch booted: a script
 * that subtracts two of these gets small numbers, which in 32-bit floats is
 * the difference between having decimals and not having them. */
static int l_ms(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)lv_tick_elaps(ctx_of(L)->t0));
    return 1;
}

/* script_ms, screen_ms, frame_ms and the rows pushed, of the LAST frame. One
 * millisecond of resolution, which is lv_tick's: a script that draws four
 * lines reads 0 and that is the right answer.
 *
 * The fourth is out of aos.H, and it is the one a script can do something
 * about: it is what the frame cost, in the only currency the panel charges
 * in. Every primitive marks the box it touched, so a script that stops
 * clearing sees this fall and the milliseconds fall with it. */
static int l_stats(lua_State *L)
{
    lx_ctx_t *c = ctx_of(L);
    lua_pushinteger(L, c->ms_script);
    lua_pushinteger(L, c->ms_screen);
    lua_pushinteger(L, c->ms_frame);
    lua_pushinteger(L, c->rows);
    return 4;
}

/* Freezes what is on the buffer right now as the background.
 *
 * From then on the app UNDOES, at the start of every frame, whatever the
 * script drew in the one before: it copies those rectangles back from the
 * frozen copy. So a script stops having to erase, and -the point of it- the
 * background may be drawn rather than a flat colour, which is what erasing
 * by painting over could never handle.
 *
 * The contract it brings: after this, anything drawn is transient and lasts
 * one frame. Something meant to stay goes on the buffer before the call, or
 * the call is made again to freeze it in.
 *
 * Returns false if there is no memory for the copy (82 KB of PSRAM), and a
 * script can carry on without one: it just has to erase for itself. */
static int l_background(lua_State *L)
{
    lx_ctx_t *c = ctx_of(L);
    lua_pushboolean(L, c->freeze && c->freeze(c->app));
    return 1;
}

static int l_beep(lua_State *L)
{
    lua_Integer hz = luaL_checkinteger(L, 1);
    lua_Integer ms = luaL_checkinteger(L, 2);
    if (hz < 50)   hz = 50;
    if (hz > 8000) hz = 8000;
    if (ms < 1)    ms = 1;
    if (ms > 2000) ms = 2000;      /* the tone blocks nothing, but a script
                                    * asking for a minute of beep is a bug */
    aos_hal_beep((int)hz, (int)ms);
    return 0;
}

/* --------------------------------------------------------------------------
 * Opening
 * -------------------------------------------------------------------------- */

static const luaL_Reg lx_funcs[] = {
    {"clear", l_clear},
    {"pixel", l_pixel},
    {"rect",  l_rect},
    {"frame", l_frame},
    {"line",  l_line},
    {"disc",  l_disc},
    {"ring",  l_ring},
    {"text",  l_text},
    {"shade", l_shade},
    {"touch", l_touch},
    {"fingers", l_fingers},
    {"ms",    l_ms},
    {"stats", l_stats},
    {"background", l_background},
    {"beep",  l_beep},
    {NULL, NULL},
};

void lx_api_open(lua_State *L, lx_ctx_t *ctx)
{
    lua_createtable(L, 0, (int)(sizeof(lx_funcs) / sizeof(lx_funcs[0])) + 2);

    for (const luaL_Reg *f = lx_funcs; f->name; f++) {
        lua_pushlightuserdata(L, ctx);
        lua_pushcclosure(L, f->func, 1);   /* the context as an upvalue, so
                                            * there is no global state here */
        lua_setfield(L, -2, f->name);
    }

    lua_pushinteger(L, LX_W);
    lua_setfield(L, -2, "W");
    lua_pushinteger(L, LX_H);
    lua_setfield(L, -2, "H");

    lua_setglobal(L, "aos");
}
