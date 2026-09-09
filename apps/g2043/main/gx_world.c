/*
 * 2043 - the planets
 *
 * A level is one entry of the gx_levels table: the sky's gradient, the scenery
 * style, the list of waves and which boss closes it. Adding a new planet is
 * writing a wave table and one more row; nothing in the game's loop finds out.
 */
#include "g2043.h"
#include "aos_i18n.h"

/* --------------------------------------------------------------------------
 * Waves
 *
 * 'at' is the level's frame; at 30 a second, 300 is second 10. 'x' is the base
 * column as a percentage of the width, so the tables read without thinking in
 * pixels. 'gift' is 1 + PU_*: the capsule drops when the whole wave is wiped
 * out, as in 1943.
 * -------------------------------------------------------------------------- */

static const g_wave_t waves_titan[] = {
    {   60, EN_DRONE,  4, FORM_COLUMN, 25, 0 },
    {  120, EN_DRONE,  4, FORM_COLUMN, 75, 0 },
    {  220, EN_DRONE,  5, FORM_V,      50, 1 + PU_TWIN },
    {  340, EN_WEAVER, 3, FORM_ROW,    30, 0 },
    {  420, EN_WEAVER, 3, FORM_ROW,    70, 0 },
    {  520, EN_TURRET, 2, FORM_SPREAD, 50, 0 },
    {  600, EN_DRONE,  6, FORM_SIDE_L, 10, 0 },
    {  700, EN_DRONE,  6, FORM_SIDE_R, 90, 1 + PU_ENERGY },
    {  820, EN_DIVER,  3, FORM_ROW,    50, 0 },
    {  940, EN_MINE,   4, FORM_SPREAD, 50, 0 },
    { 1040, EN_GUNNER, 2, FORM_ROW,    50, 1 + PU_SPREAD },
    { 1160, EN_WEAVER, 5, FORM_V,      35, 0 },
    { 1260, EN_TURRET, 3, FORM_SPREAD, 50, 0 },
    { 1360, EN_DRONE,  6, FORM_V,      65, 1 + PU_ENERGY },
    { 1480, EN_HEAVY,  1, FORM_COLUMN, 50, 1 + PU_SHIELD },
    { 1580, EN_DIVER,  4, FORM_SPREAD, 50, 0 },
};

static const g_wave_t waves_neptuno[] = {
    {   50, EN_WEAVER, 4, FORM_V,      50, 0 },
    {  140, EN_DRONE,  5, FORM_SIDE_L, 10, 0 },
    {  200, EN_DRONE,  5, FORM_SIDE_R, 90, 1 + PU_LASER },
    {  320, EN_DIVER,  4, FORM_ROW,    50, 0 },
    {  420, EN_MINE,   5, FORM_SPREAD, 50, 0 },
    {  520, EN_GUNNER, 2, FORM_ROW,    30, 0 },
    {  580, EN_GUNNER, 2, FORM_ROW,    70, 1 + PU_ENERGY },
    {  700, EN_WEAVER, 6, FORM_COLUMN, 20, 0 },
    {  760, EN_WEAVER, 6, FORM_COLUMN, 80, 0 },
    {  880, EN_HEAVY,  1, FORM_COLUMN, 35, 0 },
    {  940, EN_HEAVY,  1, FORM_COLUMN, 65, 1 + PU_SPREAD },
    { 1080, EN_DIVER,  5, FORM_SPREAD, 50, 0 },
    { 1200, EN_DRONE,  8, FORM_V,      50, 0 },
    { 1320, EN_MINE,   6, FORM_SPREAD, 50, 1 + PU_ENERGY },
    { 1440, EN_GUNNER, 3, FORM_SPREAD, 50, 0 },
    { 1560, EN_WEAVER, 6, FORM_SIDE_L, 10, 0 },
    { 1640, EN_WEAVER, 6, FORM_SIDE_R, 90, 1 + PU_LIFE },
    { 1800, EN_HEAVY,  2, FORM_ROW,    50, 1 + PU_SHIELD },
};

static const g_wave_t waves_ceres[] = {
    {   40, EN_DRONE,  6, FORM_V,      50, 0 },
    {  140, EN_TURRET, 3, FORM_SPREAD, 50, 0 },
    {  220, EN_DIVER,  4, FORM_ROW,    30, 0 },
    {  280, EN_DIVER,  4, FORM_ROW,    70, 1 + PU_WAVE },
    {  400, EN_GUNNER, 3, FORM_SPREAD, 50, 0 },
    {  520, EN_MINE,   7, FORM_SPREAD, 50, 0 },
    {  620, EN_WEAVER, 8, FORM_COLUMN, 50, 1 + PU_ENERGY },
    {  760, EN_HEAVY,  2, FORM_ROW,    40, 0 },
    {  880, EN_TURRET, 4, FORM_SPREAD, 50, 0 },
    {  980, EN_DRONE, 10, FORM_SIDE_L, 10, 0 },
    { 1060, EN_DRONE, 10, FORM_SIDE_R, 90, 1 + PU_LASER },
    { 1200, EN_GUNNER, 4, FORM_ROW,    50, 0 },
    { 1340, EN_DIVER,  6, FORM_SPREAD, 50, 1 + PU_ENERGY },
    { 1480, EN_MINE,   8, FORM_SPREAD, 50, 0 },
    { 1600, EN_HEAVY,  3, FORM_V,      50, 1 + PU_LIFE },
    { 1760, EN_WEAVER, 8, FORM_V,      50, 0 },
    { 1900, EN_GUNNER, 4, FORM_SPREAD, 50, 1 + PU_SHIELD },
    { 2040, EN_DRONE, 10, FORM_V,      50, 0 },
};

/* --------------------------------------------------------------------------
 * The planets
 * -------------------------------------------------------------------------- */

const g_level_t gx_levels[] = {
    {
        .name = N_("TITAN ROJO"), .tag = N_("LLANURAS DE OXIDO"),
        .sky_top = 0x2A0E0A, .sky_bot = 0x8A3A18,
        .feat_a = 0x5E2410, .feat_b = 0xC9743A, .haze = 0xFFB877,
        .bg = BG_DUST, .length = 1700, .scroll = 22,
        .waves = waves_titan, .wave_count = sizeof(waves_titan) / sizeof(waves_titan[0]),
        .boss = 0,
    },
    {
        .name = N_("MAR DE NEPTUNO"), .tag = N_("OCEANO DE METANO"),
        .sky_top = 0x03142E, .sky_bot = 0x0E5E8C,
        .feat_a = 0x0A3A63, .feat_b = 0x7BE9FF, .haze = 0xBFE9FF,
        .bg = BG_OCEAN, .length = 1950, .scroll = 26,
        .waves = waves_neptuno, .wave_count = sizeof(waves_neptuno) / sizeof(waves_neptuno[0]),
        .boss = 1,
    },
    {
        .name = N_("CINTURON DE CERES"), .tag = N_("CAMPO DE ESCOMBROS"),
        .sky_top = 0x05060F, .sky_bot = 0x1A1038,
        .feat_a = 0x3A2C55, .feat_b = 0xB072F0, .haze = 0x8A6ACF,
        .bg = BG_BELT, .length = 2200, .scroll = 30,
        .waves = waves_ceres, .wave_count = sizeof(waves_ceres) / sizeof(waves_ceres[0]),
        .boss = 2,
    },
};

const int gx_level_count = sizeof(gx_levels) / sizeof(gx_levels[0]);

/* --------------------------------------------------------------------------
 * Scenery
 *
 * A single list of items that descend and are recycled at the top. What each
 * item draws is decided by the planet's style, so adding a new style means
 * adding a case to scenery_draw() and another to scenery_spawn().
 * -------------------------------------------------------------------------- */

static void scenery_spawn(g_t *g, g_scenery_t *s, bool anywhere)
{
    const g_level_t *lv = &gx_levels[g->level];

    s->kind  = (uint8_t)(g_rnd(g) % 3);
    s->size  = (uint8_t)g_rnd_range(g, 6, 26);
    s->x     = (int16_t)g_rnd_range(g, -8, GX_W + 8);
    s->y     = (int16_t)(anywhere ? g_rnd_range(g, -20, GX_H) * FX_ONE
                                  : g_rnd_range(g, -40, -12) * FX_ONE);
    /* parallax: the small ones move more slowly because they look further
     * away */
    s->speed = (int16_t)(lv->scroll / 2 + lv->scroll * s->size / 40);
    s->alive = 1;
}

void gx_bg_reset(g_t *g)
{
    for (int i = 0; i < G_MAX_STARS; i++) {
        g->stars[i].x     = (uint8_t)(g_rnd(g) % GX_W);
        g->stars[i].y     = (int16_t)(g_rnd(g) % (GX_H * FX_ONE));
        g->stars[i].layer = (uint8_t)(g_rnd(g) % 3);
    }
    for (int i = 0; i < G_MAX_SCENERY; i++) {
        scenery_spawn(g, &g->scenery[i], true);
    }
}

void gx_bg_update(g_t *g)
{
    const g_level_t *lv = &gx_levels[g->level];

    g->scroll += lv->scroll;

    for (int i = 0; i < G_MAX_SCENERY; i++) {
        g_scenery_t *s = &g->scenery[i];
        s->y = (int16_t)(s->y + s->speed);
        if (UNFX(s->y) > GX_H + 30) {
            scenery_spawn(g, s, false);
        }
    }

    for (int i = 0; i < G_MAX_STARS; i++) {
        g_star_t *st = &g->stars[i];
        st->y = (int16_t)(st->y + (int16_t)(lv->scroll / 2 + st->layer * lv->scroll / 2));
        if (UNFX(st->y) > GX_H) {
            st->y = 0;
            st->x = (uint8_t)(g_rnd(g) % GX_W);
        }
    }
}

/* -------------------------------------------------------------------------- */

static void draw_dust(g_t *g, const g_level_t *lv)
{
    gx_buf_t *b = &g->buf;
    uint16_t a = gx_rgb(lv->feat_a);
    uint16_t c = gx_rgb(lv->feat_b);

    for (int i = 0; i < G_MAX_SCENERY; i++) {
        const g_scenery_t *s = &g->scenery[i];
        int r  = s->size / 2 + 2;
        int sy = UNFX(s->y);

        switch (s->kind) {
        case 0:     /* crater */
            gx_disc(b, s->x, sy, r, a);
            gx_ring(b, s->x, sy, r, c);
            gx_ring(b, s->x, sy, r - 2 > 0 ? r - 2 : 1, a);
            break;
        case 1:     /* rock spur */
            gx_round(b, s->x - r, sy - r / 2, r * 2, r, r / 3, a);
            gx_hline(b, s->x - r + 2, sy - r / 2, r * 2 - 4, c);
            break;
        default:    /* dune */
            gx_disc(b, s->x, sy, r, c);
            gx_disc(b, s->x, sy - 2, r - 1 > 0 ? r - 1 : 1, a);
            break;
        }
    }

    /* suspended dust: fast little streaks that give a sense of speed */
    if (!g->detail) {
        return;
    }
    uint16_t haze = gx_rgb(lv->haze);
    for (int i = 0; i < 14; i++) {
        int x = (int)((g->scroll / 3 + i * 977) % GX_W);
        int y = (int)((g->scroll * 2 / 3 + i * 613) % GX_H);
        gx_hline(b, x, y, 3, gx_mix(b->px[y * GX_W + x], haze, 5));
    }
}

static void draw_ocean(g_t *g, const g_level_t *lv)
{
    gx_buf_t *b = &g->buf;
    uint16_t deep = gx_rgb(lv->feat_a);
    uint16_t foam = gx_rgb(lv->feat_b);
    uint16_t ice  = gx_rgb(0xE8F6FF);

    for (int i = 0; i < G_MAX_SCENERY; i++) {
        const g_scenery_t *s = &g->scenery[i];
        int r  = s->size / 2 + 2;
        int sy = UNFX(s->y);

        switch (s->kind) {
        case 0:     /* deep patch */
            gx_disc(b, s->x, sy, r, deep);
            break;
        case 1: {   /* floe */
            gx_round(b, s->x - r, sy - r / 2, r * 2, r + 2, r / 3, ice);
            gx_round(b, s->x - r + 2, sy - r / 2 + 1, r * 2 - 4, r / 2, 1, foam);
            break;
        }
        default:    /* crest */
            gx_hline(b, s->x - r, sy, r, foam);
            gx_hline(b, s->x + 2, sy + 2, r - 2, foam);
            break;
        }
    }

    /* Swell lines: two layers at different speeds. The gaps and the lengths
     * come from a hash of the position, otherwise it comes out as a comb and
     * the grid shows. */
    for (int k = 0; k < 2; k++) {
        int step = k ? 23 : 31;
        uint16_t col = gx_mix(gx_rgb(lv->sky_bot), foam, k ? 4 : 7);
        for (int y = (int)((g->scroll / (k ? 3 : 2)) % step); y < GX_H; y += step) {
            int seed = y * 7919 + k * 104729;
            for (int x = -10; x < GX_W; x += 26) {
                int h = (seed + x * 31) & 0xFF;
                if (h < 96) {
                    continue;
                }
                int off = gx_sin((int)(g->scroll / 4 + y * 3 + x)) / 40;
                gx_hline(b, x + off + (h % 9), y, 4 + (h % 8), col);
            }
        }
    }
}

static void draw_belt(g_t *g, const g_level_t *lv)
{
    gx_buf_t *b = &g->buf;

    /* Nebula: two slow patches that give the black some depth. They are the
     * most expensive part of the whole background — twenty thousand blended
     * pixels per frame — so they are the first thing switched off when the
     * board cannot keep up. */
    if (g->detail) {
        uint16_t neb = gx_rgb(lv->feat_b);
        int ny = (int)((g->scroll / 6) % (GX_H + 160)) - 80;
        gx_glow(b, 46, ny, 62, neb, 4);
        gx_glow(b, 140, ny - 120, 50, gx_rgb(lv->haze), 3);
    }

    for (int i = 0; i < G_MAX_STARS; i++) {
        const g_star_t *st = &g->stars[i];
        static const uint32_t tint[3] = { 0x556080, 0x99A3BC, 0xFFFFFF };
        gx_px(b, st->x, UNFX(st->y), gx_rgb(tint[st->layer]));
        if (st->layer == 2) {
            gx_px(b, st->x, UNFX(st->y) + 1, gx_rgb(0x99A3BC));
        }
    }

    for (int i = 0; i < G_MAX_SCENERY; i++) {
        const g_scenery_t *s = &g->scenery[i];
        const gx_sprite_t *sp = &gx_art_rock[s->kind % 3];
        int sy = UNFX(s->y);
        /* the large rocks are drawn twice, offset, so they look fatter without
         * needing another sprite */
        gx_blit_c(b, s->x, sy, sp->rows, sp->n);
        if (s->size > 18) {
            gx_blit_c(b, s->x + sp->w - 2, sy + 2, sp->rows, sp->n);
        }
    }
}

void gx_bg_draw(g_t *g)
{
    const g_level_t *lv = &gx_levels[g->level];

    gx_vgrad(&g->buf, 0, GX_H - 1, gx_rgb(lv->sky_top), gx_rgb(lv->sky_bot));

    switch (lv->bg) {
    case BG_DUST:  draw_dust(g, lv);  break;
    case BG_OCEAN: draw_ocean(g, lv); break;
    default:       draw_belt(g, lv);  break;
    }
}
