/*
 * Claudito - sprites de objetos (ver cl_sprites.h)
 */
#include "cl_sprites.h"

/* --- comida --------------------------------------------------------------- */

const char *const cl_spr_apple[11] = {
    "....NN.....",
    "...NNg.....",
    "..rrrggg...",
    ".rrrrrrrr..",
    "rrwrrrrrrr.",
    "rrwrrrrrrr.",
    "rrrrrrrrrr.",
    "rrrrrrrrrr.",
    ".rrrrrrrr..",
    "..RRRRRR...",
    "...R..R....",
};

const char *const cl_spr_pizza[11] = {
    "nnnnnnnnnnn",
    "nyyyyyyyyyn",
    ".yyryyyryy.",
    ".yyyyyyyyy.",
    "..yyyyyyy..",
    "..yryyyry..",
    "...yyyyy...",
    "...yyyyy...",
    "....yry....",
    "....yyy....",
    ".....y.....",
};

const char *const cl_spr_cookie[11] = {
    "...nnnnn...",
    "..nnnnnnn..",
    ".nnNnnnnnn.",
    "nnnnnnNnnnn",
    "nnnnnnnnnnn",
    "nnNnnnnnNnn",
    "nnnnnnnnnnn",
    "nnnnNnnnnnn",
    ".nnnnnnnnn.",
    "..nnnnnnn..",
    "...nnnnn...",
};

const char *const cl_spr_cake[11] = {
    "....r......",
    "...mmm.....",
    "..mmmmm....",
    ".mmmmmmm...",
    "mmmmmmmmm..",
    "mwmmmmmwm..",
    "sssssssss..",
    "sSssSssSs..",
    "sSssSssSs..",
    ".sssssss...",
    ".sSsssSs...",
};

/* --- juguetes ------------------------------------------------------------- */

const char *const cl_spr_ball[11] = {
    "...rrrrr...",
    "..rrrrrrr..",
    ".rwwrrrrrr.",
    "rwwrrrrrrrr",
    "rwrrrrrrrrr",
    "wwwwwwwwwww",
    "rrrrrrrrrrr",
    "rrrrrrrrrrr",
    ".rrrrrrrrr.",
    "..RRRRRRR..",
    "...RRRRR...",
};

const char *const cl_spr_balloon[11] = {
    "...ppp.....",
    "..ppppp....",
    ".ppppppp...",
    ".ppppppp...",
    ".plppppp...",
    ".plppppp...",
    ".ppppppp...",
    ".ppppppp...",
    "..ppppp....",
    "...ppp.....",
    "....p......",
};

const char *const cl_spr_dice[9] = {
    "wwwwwwwww",
    "wkwwwwwkw",
    "wwwwwwwww",
    "wwwwkwwww",
    "wwwwwwwww",
    "wkwwwwwkw",
    "wwwwwwwww",
    "WWWWWWWWW",
    ".WWWWWWW.",
};

const char *const cl_spr_bubbles[11] = {
    "...ccc.....",
    "..cwwwc....",
    "..cwwwc....",
    "...ccc.....",
    ".......cc..",
    "......cwwc.",
    "......cwwc.",
    ".......cc..",
    "..cc.......",
    ".c..c......",
    "..cc.......",
};

/* --- herramientas --------------------------------------------------------- */

const char *const cl_spr_sponge[11] = {
    ".........c.",
    "........c.c",
    ".........c.",
    "..c........",
    "yyyyyyyyyyy",
    "yYyyyyyYyyy",
    "yyyyyyyyyyy",
    "yyyYyyyyyYy",
    "yyyyyyyyyyy",
    "yYyyyYyyyyy",
    "YYYYYYYYYYY",
};

const char *const cl_spr_hand[9] = {
    ".s.s.s...",
    "ss.s.s.s.",
    "sssssssss",
    "sssssssss",
    "ssssssss.",
    ".sssssss.",
    "..ssssss.",
    "...sssss.",
    "....sss..",
};

const char *const cl_spr_moon[9] = {
    "...yyy...",
    "..yyyyy..",
    ".yyyy....",
    ".yyy.....",
    "yyy......",
    ".yyy.....",
    ".yyyy....",
    "..yyyyy..",
    "...yyy...",
};

const char *const cl_spr_door[11] = {
    ".nnnnnnnnn.",
    ".nNNNNNNNn.",
    ".nNcccccNn.",
    ".nNcccccNn.",
    ".nNNNNNNNn.",
    ".nNNNNNyNn.",
    ".nNNNNNNNn.",
    ".nNNNNNNNn.",
    ".nNNNNNNNn.",
    ".nnnnnnnnn.",
    "...........",
};

const char *const cl_spr_tree_icon[11] = {
    "...ggg.....",
    "..ggggg....",
    ".gggGggg...",
    "ggggggggg..",
    ".gggggGgg..",
    "..gggggg...",
    "...nnn.....",
    "...nnn.....",
    "...nNn.....",
    "..GGGGG....",
    "...........",
};

/* --- adornos -------------------------------------------------------------- */

const char *const cl_spr_plant[16] = {
    "......g......",
    ".....gGg.....",
    "..g..ggg..g..",
    ".gGg.ggg.gGg.",
    "..ggg.g.ggg..",
    "...gg.g.gg...",
    ".g...ggg...g.",
    "gGg..ggg..gGg",
    ".ggg.ggg.ggg.",
    "...g.ggg.g...",
    "......g......",
    "..nnnnnnnnn..",
    "..nnnnnnnnn..",
    "...NNNNNNN...",
    "...NNNNNNN...",
    "....NNNNN....",
};

const char *const cl_spr_flower[6] = {
    ".m.m..",
    "mmymm.",
    ".mym..",
    "..g...",
    ".gg...",
    "..g...",
};

const char *const cl_spr_butterfly[7] = {
    "pp.....pp",
    "pPp.k.pPp",
    "ppppkpppp",
    "..ppkpp..",
    "ppppkpppp",
    "pPp.k.pPp",
    "pp.....pp",
};

const char *const cl_spr_bowl[7] = {
    ".............",
    "..sssssssss..",
    ".wwwwwwwwwww.",
    "wWWWWWWWWWWWw",
    ".WWWWWWWWWWW.",
    "..WWWWWWWWW..",
    "....WWWWW....",
};

const char *const cl_spr_lamp[13] = {
    "..yyyyyyy..",
    ".yyyyyyyyy.",
    "yyyyyyyyyyy",
    "YYYYYYYYYYY",
    ".....D.....",
    ".....D.....",
    ".....D.....",
    ".....D.....",
    ".....D.....",
    ".....D.....",
    "....DDD....",
    "...DDDDD...",
    "..DDDDDDD..",
};

const char *const cl_spr_bone[5] = {
    "WW.....WW",
    "WWWWWWWWW",
    ".WWWWWWW.",
    "WWWWWWWWW",
    "WW.....WW",
};
