/*
 * 2043 - sprites (see gx_art.h)
 */
#include "gx_art.h"

/* --------------------------------------------------------------------------
 * The player's ship
 *
 * A long-nosed interceptor with a delta wing. The engines' flame is not in the
 * sprite: it flickers, so it is drawn separately on every frame.
 * -------------------------------------------------------------------------- */
static const char *const ship_rows[] = {
    ".......w.......",
    "......wcw......",
    "......wcw......",
    ".....wcCcw.....",
    ".....GcCcG.....",
    "....GGcCcGG....",
    "...GDDcCcDDG...",
    "..GDDDcCcDDDG..",
    ".GDDDDDCDDDDDG.",
    "GGDDDDDCDDDDDGG",
    "GkDDDdDCDdDDDkG",
    ".k.GDdDCDdDG.k.",
    "....GdkkkdG....",
};
const gx_sprite_t gx_art_ship = { ship_rows, 13, 15 };

static const char *const ship_icon_rows[] = {
    "...w...",
    "..wcw..",
    ".GwcwG.",
    "GdwCwdG",
    "..k.k..",
};
const gx_sprite_t gx_art_ship_icon = { ship_icon_rows, 5, 7 };

/* --------------------------------------------------------------------------
 * Enemies
 * -------------------------------------------------------------------------- */

/* Light red fighter. The engine's trail is at the top because it is
 * descending. */
static const char *const drone_rows[] = {
    "....y....",
    "..kyoyk..",
    ".RkyoykR.",
    "RrkkokkrR",
    "kRrrorrRk",
    ".kRrorRk.",
    "..kRrRk..",
    "...krk...",
    "....k....",
};

/* Violet swept-wing interceptor: the one that zigzags. */
static const char *const weaver_rows[] = {
    "....ppp....",
    "...pPpPp...",
    "..pPpppPp..",
    ".pPPpwpPPp.",
    "pPP.pwp.PPp",
    ".P..pwp..P.",
    "....ppp....",
    "....kmk....",
    ".....m.....",
};

/* Turquoise dart. It hangs still at the top and then dives. */
static const char *const diver_rows[] = {
    "...n...n...",
    "...N...N...",
    "..vNv.vNv..",
    "..vvvnvvv..",
    ".vvVvnvVvv.",
    "vvVV.n.VVvv",
    ".vV..n..Vv.",
    "..v.wnw.v..",
    "....nnn....",
    ".....n.....",
    ".....w.....",
};

/* Orange gunship with two side turrets. */
static const char *const gunner_rows[] = {
    "..o.......o..",
    ".ooo.....ooo.",
    ".oOo.ooo.oOo.",
    ".ooo.oOo.ooo.",
    "sSoooOOOooosS",
    "sSSooOwOooSSs",
    ".sSooOwOoosS.",
    "..sooOOOoos..",
    "...ooOoOoo...",
    "...kO.k.Ok...",
    "...y.....y...",
};

/* Floating mine: it does not chase, but on dying it spits shrapnel. */
static const char *const mine_rows[] = {
    "....k....",
    "..k.y.k..",
    ".k.yyy.k.",
    "..yyeyy..",
    "kyye.eyyk",
    "..yyeyy..",
    ".k.yyy.k.",
    "..k.y.k..",
    "....k....",
};

/* Blue bomber. The one that drops the capsule when the whole formation
 * falls. */
static const char *const heavy_rows[] = {
    ".....b.....b.....",
    "....bBb...bBb....",
    "....bBb.b.bBb....",
    "...GbBbbbbbBbG...",
    "..GgbBBBcBBBbgG..",
    ".GgggbBBcBBbgggG.",
    "GggDdbBBcBBbdDggG",
    ".GgDdbBBcBBbdDgG.",
    "..GDdbBBcBBbdDG..",
    "...kdbBBBBBbdk...",
    "....bBBBBBBBb....",
    "....kBb.b.bBk....",
    ".....k.....k.....",
};

/* Surface turret: it descends with the terrain and fires upwards. */
static const char *const turret_rows[] = {
    "....SkS....",
    "....sks....",
    "...SsksS...",
    "..SssSssS..",
    ".SsSSsSSsS.",
    "SsSSSsSSSsS",
    "sSSTTTTTSSs",
    ".STTTTTTTS.",
    "..kTTTTTk..",
};

const gx_sprite_t gx_art_enemy[EN_COUNT] = {
    [EN_DRONE]  = { drone_rows,   9, 9  },
    [EN_WEAVER] = { weaver_rows,  9, 11 },
    [EN_DIVER]  = { diver_rows,  11, 11 },
    [EN_GUNNER] = { gunner_rows, 11, 13 },
    [EN_MINE]   = { mine_rows,    9, 9  },
    [EN_HEAVY]  = { heavy_rows,  13, 17 },
    [EN_TURRET] = { turret_rows,  9, 11 },
};

/* --------------------------------------------------------------------------
 * Debris
 * -------------------------------------------------------------------------- */

static const char *const rock_a_rows[] = {
    "..SSs..",
    ".SsssS.",
    "SsssSsS",
    "SsSSssS",
    ".SssSS.",
    "..SSk..",
};

static const char *const rock_b_rows[] = {
    ".SSsS.",
    "SsssSS",
    "SsSssS",
    ".SkSS.",
};

static const char *const rock_c_rows[] = {
    ".ss.",
    "sSSs",
    ".Sk.",
};

const gx_sprite_t gx_art_rock[3] = {
    { rock_a_rows, 6, 7 },
    { rock_b_rows, 4, 6 },
    { rock_c_rows, 3, 4 },
};
