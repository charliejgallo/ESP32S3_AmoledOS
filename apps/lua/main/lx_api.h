/*
 * LX_API - everything a Lua script can reach
 *
 * The sandbox is this file. There is no 'io', no 'os' and no 'package' -their
 * sources are not even compiled- so the only door out of the interpreter is
 * the table this opens, and every function behind that door has to assume its
 * arguments come from somebody who does not know what a buffer is.
 *
 * Two rules it keeps:
 *   - Numbers are taken with luaL_checkinteger, which RAISES a Lua error on
 *     anything else. A script that passes a table where a colour goes gets a
 *     message with a line number, which is the whole point of scripting.
 *   - Coordinates are clamped before they reach lx_pixel. The primitives clip
 *     already, but they clip in int arithmetic: a width of two thousand
 *     million is not a clipped rectangle, it is an overflow.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lua.h"
#include "lx_pixel.h"

typedef struct {
    lx_buf_t *buf;              /* where the script draws        */

    /* What the script touched this frame, in its own coordinates. Every
     * primitive marks its own bounding box, so a script gets this for
     * nothing: it does not call anything and does not know it exists, and
     * the app pushes only those rows at the panel. A script that clears
     * every frame marks everything, which is what used to happen always. */
    lx_dirty_t *dirty;

    /* The frozen background, and the app's hook to freeze it. NULL until the
     * script asks for one; see aos.background() in lx_api.c. */
    bool      (*freeze)(void *app);
    void       *app;
    int16_t   touch_x;
    int16_t   touch_y;
    bool      touch_down;
    uint32_t  t0;               /* ms when the script started    */

    /* What the last frame was spent on, filled in by the app and handed back
     * to the script by aos.stats(). A script that is slow needs to know
     * WHICH of the two is slow: its own maths, or pushing 368x448 pixels at
     * the panel. Without this the author blames the interpreter, which on
     * this board is almost never the one at fault. */
    uint16_t  ms_script;
    uint16_t  ms_screen;
    uint16_t  ms_frame;
    uint16_t  rows;             /* of LX_H, how many went to the panel */
} lx_ctx_t;

/* Creates the global table 'aos' bound to this context. The context lives in
 * the app and must outlive the state. */
void lx_api_open(lua_State *L, lx_ctx_t *ctx);
