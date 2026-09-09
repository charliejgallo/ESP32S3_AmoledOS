/*
 * ARKANOS - bricks, capsules and the level table
 *
 * Each level is a text map 11 characters wide, one per column, and up to 13
 * rows. Adding a level means adding a map and a row to ak_levels[]: there is
 * no per-level code anywhere.
 *
 *   . or space    empty
 *   1..8          one-hit brick, each digit a colour
 *   H             silver, two hits
 *   T             gold, three hits
 *   S             steel, unbreakable and it does not count towards finishing
 *   B             bomb, blows up its eight neighbours
 *   M             mystery, always drops a capsule
 */
#include "arkanos.h"
#include "aos_i18n.h"

/* --------------------------------------------------------------------------
 * Brick types
 * -------------------------------------------------------------------------- */

static const ak_brick_def_t ak_bricks[BK_COUNT] = {
    [BK_NONE]  = { 0x000000,   0, 0,            0 },
    [BK_C1]    = { 0xFF4A3D,   1, 0,           50 },
    [BK_C2]    = { 0xFF9F0A,   1, 0,           60 },
    [BK_C3]    = { 0xFFE45E,   1, 0,           70 },
    [BK_C4]    = { 0x4ADE80,   1, 0,           80 },
    [BK_C5]    = { 0x2AF0C8,   1, 0,           90 },
    [BK_C6]    = { 0x4A9DF5,   1, 0,          100 },
    [BK_C7]    = { 0xB072F0,   1, 0,          110 },
    [BK_C8]    = { 0xD97757,   1, 0,          120 },
    [BK_HARD]  = { 0x99A3BC,   2, 0,          160 },
    [BK_TOUGH] = { 0xE0A800,   3, 0,          300 },
    [BK_STEEL] = { 0x5A6478, 255, AK_BF_SOLID,  0 },
    [BK_BOMB]  = { 0xFF2D55,   1, AK_BF_BOMB, 200 },
    [BK_MYST]  = { 0x7BE9FF,   1, AK_BF_MYST, 150 },
};

const ak_brick_def_t *ak_brick(int kind)
{
    if (kind < 0 || kind >= BK_COUNT) {
        kind = BK_NONE;
    }
    return &ak_bricks[kind];
}

int ak_brick_from_char(char ch)
{
    if (ch >= '1' && ch <= '8') {
        return BK_C1 + (ch - '1');
    }
    switch (ch) {
    case 'H': return BK_HARD;
    case 'T': return BK_TOUGH;
    case 'S': return BK_STEEL;
    case 'B': return BK_BOMB;
    case 'M': return BK_MYST;
    default:  return BK_NONE;
    }
}

/* --------------------------------------------------------------------------
 * Capsules
 * -------------------------------------------------------------------------- */

/* These names go to ak_popup(), which copies them into a char txt[8]: seven
 * usable characters and not one more, silently clipped. And they are drawn by
 * the game's own font, which is ASCII and upper case only. Any translation has
 * to honour both. */
static const ak_cap_def_t ak_caps[CAP_COUNT] = {
    [CAP_WIDE]   = { 'A', 0x4A9DF5, N_("ANCHA")  },
    [CAP_SLOW]   = { 'L', 0x2AF0C8, N_("LENTA")  },
    [CAP_MULTI]  = { 'T', 0xB072F0, N_("TRIPLE") },
    [CAP_LASER]  = { 'D', 0xFF4A3D, N_("DISPARO") },
    [CAP_CATCH]  = { 'I', 0x4ADE80, N_("IMAN")   },
    [CAP_LIFE]   = { 'V', 0xFF6FAE, N_("VIDA")   },
    [CAP_POINTS] = { 'P', 0xFFE45E, "+500"   },
};

const ak_cap_def_t *ak_cap_def(int kind)
{
    if (kind < 0 || kind >= CAP_COUNT) {
        kind = CAP_POINTS;
    }
    return &ak_caps[kind];
}

/* --------------------------------------------------------------------------
 * The maps
 * -------------------------------------------------------------------------- */

static const char *const lv_muro[] = {
    "...........",
    "11111111111",
    "22222222222",
    "33333333333",
    "44444444444",
};

static const char *const lv_torres[] = {
    "1.1.1.1.1.1",
    "1.1.1.1.1.1",
    "2.2.2.2.2.2",
    "2.2.2.2.2.2",
    "3.3.3.3.3.3",
    "3.3.3.3.3.3",
    "44444444444",
};

static const char *const lv_piramide[] = {
    ".....6.....",
    "....565....",
    "...45654...",
    "..3456543..",
    ".234565432.",
    "12345654321",
    "...........",
    "S....M....S",
};

static const char *const lv_panal[] = {
    "1.1.1.1.1.1",
    ".2.2.2.2.2.",
    "3.3.3.3.3.3",
    ".4.4.4.4.4.",
    "5.5.5.5.5.5",
    ".6.6.6.6.6.",
    "H.H.H.H.H.H",
};

static const char *const lv_fortaleza[] = {
    "SSSSSSSSSSS",
    "S111111111S",
    "S1HHHHHHH1S",
    "S1H2B2B2H1S",
    "S1HHHHHHH1S",
    "S111111111S",
    "SS.......SS",
};

static const char *const lv_reja[] = {
    "1S1S1S1S1S1",
    "1.1.1.1.1.1",
    "2S2S2S2S2S2",
    "2.2.2.2.2.2",
    "3S3S3S3S3S3",
    "3.3.3.3.3.3",
    "44444M44444",
};

static const char *const lv_calavera[] = {
    "...........",
    "..HHHHHHH..",
    ".HHHHHHHHH.",
    "HH.HHHHH.HH",
    "HH.HHHHH.HH",
    ".HHHHHHHHH.",
    "..H.H.H.H..",
    "..B.....B..",
};

static const char *const lv_nave[] = {
    ".....5.....",
    "....555....",
    "...55555...",
    "..5566555..",
    ".555666555.",
    "55566S66555",
    "...5.6.5...",
    "..4.....4..",
};

static const char *const lv_espiral[] = {
    "S111111111S",
    "S.........S",
    "S.2222222.S",
    "S.S.....S.S",
    "S.S.3M3.S.S",
    "S.S.....S.S",
    "S.4444444.S",
    "S.........S",
};

static const char *const lv_corazon[] = {
    ".111...111.",
    "11111.11111",
    "11111111111",
    ".111111111.",
    "..1111111..",
    "...11111...",
    "....111....",
    ".....1.....",
};

static const char *const lv_claudito[] = {
    "..8.....8..",
    "...8...8...",
    "..8888888..",
    ".888888888.",
    "88.88888.88",
    "88888888888",
    ".888888888.",
    "..8888888..",
    "...8...8...",
};

static const char *const lv_jefe[] = {
    ".SSSSSSSSS.",
    "STTTTTTTTTS",
    "ST.TTTTT.TS",
    "STTTTTTTTTS",
    "STT.TTT.TTS",
    "STTT...TTTS",
    "STTTTTTTTTS",
    ".HHHHMHHHH.",
    "..HHHHHHH..",
};

#define LV(a)   (a), (uint8_t)(sizeof(a) / sizeof((a)[0]))

/* The speed goes in 1/16 of an art pixel per frame: 52 is three and a quarter
 * pixels, that is, some 195 screen pixels per second at 30 frames, and the
 * ball crosses the field in a little over two seconds. */
static const ak_level_t ak_levels[] = {
    { N_("PRIMER MURO"), LV(lv_muro),       52, 26, 0x0B1026, 0x1B2350, 0x4A9DF5, 26 },
    { N_("TORRES"),      LV(lv_torres),     55, 24, 0x120B26, 0x2A1B50, 0xB072F0, 22 },
    { N_("PIRAMIDE"),    LV(lv_piramide),   58, 24, 0x261A0B, 0x50361B, 0xFF9F0A, 20 },
    { N_("PANAL"),       LV(lv_panal),      61, 26, 0x0B2620, 0x1B5045, 0x2AF0C8, 24 },
    { N_("FORTALEZA"),   LV(lv_fortaleza),  64, 30, 0x260B12, 0x501B2A, 0xFF4A3D, 18 },
    { N_("REJA"),        LV(lv_reja),       67, 26, 0x0B1626, 0x1B3650, 0x7BE9FF, 26 },
    { N_("CALAVERA"),    LV(lv_calavera),   70, 28, 0x14141C, 0x2E2E3C, 0x99A3BC, 30 },
    { N_("NAVE"),        LV(lv_nave),       73, 26, 0x0B1026, 0x152E50, 0x4ADE80, 28 },
    { N_("ESPIRAL"),     LV(lv_espiral),    76, 30, 0x1A0B26, 0x3A1B50, 0xB072F0, 20 },
    { N_("CORAZON"),     LV(lv_corazon),    79, 28, 0x260B18, 0x501B36, 0xFF6FAE, 24 },
    { N_("CLAUDITO"),    LV(lv_claudito),   82, 30, 0x1C1008, 0x3C2214, 0xD97757, 22 },
    { N_("EL JEFE"),     LV(lv_jefe),       86, 32, 0x05060C, 0x1B1B2A, 0xE0A800, 34 },
};

int ak_level_count(void)
{
    return (int)(sizeof(ak_levels) / sizeof(ak_levels[0]));
}

const ak_level_t *ak_level_get(int i)
{
    int n = ak_level_count();
    if (i < 0) {
        i = 0;
    }
    if (i >= n) {
        i = n - 1;
    }
    return &ak_levels[i];
}
