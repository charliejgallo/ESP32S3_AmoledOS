/*
 * LX_API - the table a Lua script sees (see lx_api.h)
 */
#include "lx_api.h"

#include "aos_hal.h"
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

/* --------------------------------------------------------------------------
 * Drawing
 * -------------------------------------------------------------------------- */

static int l_clear(lua_State *L)
{
    lx_fill(ctx_of(L)->buf, arg_color(L, 1));
    return 0;
}

static int l_pixel(lua_State *L)
{
    lx_px(ctx_of(L)->buf, arg_coord(L, 1), arg_coord(L, 2), arg_color(L, 3));
    return 0;
}

static int l_rect(lua_State *L)
{
    lx_rect(ctx_of(L)->buf, arg_coord(L, 1), arg_coord(L, 2),
            arg_coord(L, 3), arg_coord(L, 4), arg_color(L, 5));
    return 0;
}

static int l_frame(lua_State *L)
{
    lx_frame(ctx_of(L)->buf, arg_coord(L, 1), arg_coord(L, 2),
             arg_coord(L, 3), arg_coord(L, 4), arg_color(L, 5));
    return 0;
}

static int l_line(lua_State *L)
{
    lx_line(ctx_of(L)->buf, arg_coord(L, 1), arg_coord(L, 2),
            arg_coord(L, 3), arg_coord(L, 4), arg_color(L, 5));
    return 0;
}

static int l_disc(lua_State *L)
{
    lx_disc(ctx_of(L)->buf, arg_coord(L, 1), arg_coord(L, 2),
            arg_coord(L, 3), arg_color(L, 4));
    return 0;
}

static int l_ring(lua_State *L)
{
    lx_ring(ctx_of(L)->buf, arg_coord(L, 1), arg_coord(L, 2),
            arg_coord(L, 3), arg_color(L, 4));
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
    lx_text(ctx_of(L)->buf, arg_coord(L, 1), arg_coord(L, 2),
            luaL_checkstring(L, 3), arg_color(L, 4), scale);
    return 0;
}

static int l_shade(lua_State *L)
{
    lua_Integer f = luaL_checkinteger(L, 5);
    if (f >  16) f =  16;
    if (f < -16) f = -16;
    lx_shade(ctx_of(L)->buf, arg_coord(L, 1), arg_coord(L, 2),
             arg_coord(L, 3), arg_coord(L, 4), (int)f);
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

/* Milliseconds since the script started, not since the watch booted: a script
 * that subtracts two of these gets small numbers, which in 32-bit floats is
 * the difference between having decimals and not having them. */
static int l_ms(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)lv_tick_elaps(ctx_of(L)->t0));
    return 1;
}

/* script_ms, screen_ms, frame_ms of the LAST frame. One millisecond of
 * resolution, which is lv_tick's: a script that draws four lines reads 0 and
 * that is the right answer. */
static int l_stats(lua_State *L)
{
    lx_ctx_t *c = ctx_of(L);
    lua_pushinteger(L, c->ms_script);
    lua_pushinteger(L, c->ms_screen);
    lua_pushinteger(L, c->ms_frame);
    return 3;
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
    {"ms",    l_ms},
    {"stats", l_stats},
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
