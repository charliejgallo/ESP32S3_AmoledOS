/*
 * 2043 - enemies and bosses
 *
 * The small enemies are sprites with a three- or four-line state machine. The
 * bosses are drawn with primitives: they have parts that rotate, fall off or
 * shake, and that does not come out of a bitmap.
 *
 * Boss rule: the health bar is the sum of all its parts plus the core. Each def
 * says how many parts it has and where they are; the core is always part number
 * 'parts'. A part with radius 0 in hitbox() is a part that cannot be hit at
 * this moment (destroyed, or the core still armoured).
 */
#include "g2043.h"
#include "aos_i18n.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Small enemies
 * -------------------------------------------------------------------------- */

static const int16_t enemy_hp[EN_COUNT]     = {  1,  1,  2,  3,  2, 10,  4 };
static const int16_t enemy_score[EN_COUNT]  = { 100, 150, 200, 300, 150, 800, 250 };
static const int16_t enemy_radius[EN_COUNT] = {  4,  5,  5,  6,  4,  8,  5 };

int gx_enemy_hp(int kind)     { return enemy_hp[kind % EN_COUNT]; }
int gx_enemy_score(int kind)  { return enemy_score[kind % EN_COUNT]; }
int gx_enemy_radius(int kind) { return enemy_radius[kind % EN_COUNT]; }

void gx_enemy_update(g_t *g, g_enemy_t *e)
{
    e->t++;

    switch (e->kind) {
    case EN_DRONE:
        /* it falls straight with a gentle drift, so the formation breathes */
        e->vx = (int16_t)(gx_sin(e->t * 3 + e->a) * 5 / 256);
        break;

    case EN_WEAVER:
        e->vx = (int16_t)(gx_sin(e->t * 5 + e->a) * 30 / 256);
        break;

    case EN_DIVER:
        if (e->b == 0) {
            e->vy = 11;
            e->vx = (int16_t)(gx_sin(e->t * 4 + e->a) * 8 / 256);
            if (e->t > e->a % 30 + 45 || UNFX(e->y) > 70) {
                /* it settles and fires: it aims once, it does not chase */
                int ang = g_angle_to_player(g, e->x, e->y);
                e->vx = (int16_t)(gx_cos(ang) * 46 / 256);
                e->vy = (int16_t)(gx_sin(ang) * 46 / 256);
                e->b = 1;
                g_beep(300, 12);
            }
        }
        break;

    case EN_GUNNER:
        if (e->b == 0) {
            if (UNFX(e->y) < 62) {
                e->vy = 13;
            } else {
                e->vy = 0;
                e->vx = 0;
                e->b = 1;
                e->a = 0;
            }
        } else if (e->b == 1) {
            e->a++;
            if (e->a % 40 == 20) {
                g_shoot_at_player(g, e->x, e->y + FX(4), 34, ES_AIMED);
            }
            if (e->a > 130) {
                e->b = 2;
                e->vy = 34;     /* it leaves through the bottom */
            }
        }
        break;

    case EN_MINE:
        e->vy = 7;
        e->vx = (int16_t)(gx_sin(e->t * 2 + e->a) * 12 / 256);
        break;

    case EN_HEAVY:
        if (e->b == 0) {
            e->vy = 9;
            if (UNFX(e->y) > 48) {
                e->b = 1;
                e->vy = 2;      /* it goes on descending a little: it does not linger forever */
                e->vx = (e->a & 1) ? 12 : -12;
            }
        } else {
            e->a++;
            int x = UNFX(e->x);
            if (x < 26 || x > GX_W - 26) {
                e->vx = (int16_t)-e->vx;
            }
            if (e->a % 75 == 40) {
                /* a fan of five */
                int base = g_angle_to_player(g, e->x, e->y);
                for (int i = -2; i <= 2; i++) {
                    int ang = base + i * 10;
                    g_spawn_eshot(g, e->x, e->y + FX(6),
                                  gx_cos(ang) * 30 / 256, gx_sin(ang) * 30 / 256,
                                  ES_HEAVY);
                }
                g_beep(180, 30);
            }
        }
        break;

    case EN_TURRET:
        e->a++;
        if (e->a % 55 == 30) {
            g_shoot_at_player(g, e->x, e->y - FX(3), 30, ES_AIMED);
        }
        break;

    default:
        break;
    }

    e->x = (int16_t)(e->x + e->vx);
    e->y = (int16_t)(e->y + e->vy);

    if (e->flash) {
        e->flash--;
    }
}

void gx_enemy_draw(g_t *g, const g_enemy_t *e)
{
    const gx_sprite_t *sp = &gx_art_enemy[e->kind % EN_COUNT];
    int x = UNFX(e->x);
    int y = UNFX(e->y);

    /* trail: a spark behind the fast ones */
    if (e->kind == EN_DIVER && e->b) {
        gx_glow(&g->buf, x, y - 7, 4, gx_rgb(0x2AF0C8), 8);
    } else if (e->kind == EN_DRONE || e->kind == EN_WEAVER) {
        gx_glow(&g->buf, x, y - sp->n / 2, 3, gx_rgb(0xFF9F0A), 5);
    }

    if (e->flash) {
        gx_blit_c_solid(&g->buf, x, y, sp->rows, sp->n, gx_rgb(0xFFFFFF));
    } else {
        gx_blit_c(&g->buf, x, y, sp->rows, sp->n);
    }

    if (e->kind == EN_MINE) {
        /* the core pulses: you can see it is armed */
        int f = 8 + gx_sin(e->t * 8) / 32;
        gx_glow(&g->buf, x, y, 3, gx_rgb(0xFF2D55), f);
    }
}

void gx_enemy_died(g_t *g, g_enemy_t *e)
{
    int x = UNFX(e->x);
    int y = UNFX(e->y);

    switch (e->kind) {
    case EN_HEAVY:
        g_boom(g, x, y, 16, gx_rgb(0xFFD60A));
        g->shake = 8;
        g_beep(90, 90);
        break;

    case EN_MINE: {
        /* the mine's whole point: on dying it spits shrapnel everywhere */
        g_boom(g, x, y, 10, gx_rgb(0xFFE45E));
        for (int i = 0; i < 6; i++) {
            int ang = i * 42 + (int)(g_rnd(g) % 20);
            g_spawn_eshot(g, e->x, e->y, gx_cos(ang) * 26 / 256,
                          gx_sin(ang) * 26 / 256, ES_BALL);
        }
        g_beep(140, 60);
        break;
    }

    default:
        g_boom(g, x, y, 8, gx_rgb(0xFF9F0A));
        g_beep(500 + (int)(g_rnd(g) % 300), 18);
        break;
    }
}

/* --------------------------------------------------------------------------
 * Bosses: common tools
 * -------------------------------------------------------------------------- */

/* Flash on taking a hit.
 *
 * A boss is so large that bullets hit it on nearly every frame: painting the
 * whole thing white would leave it permanently white and the drawing would be
 * lost. With a half blend the hit is visible and the ship still reads. */
static uint16_t hit_tint(uint16_t c, bool flash)
{
    return flash ? gx_mix(c, gx_rgb(0xFFFFFF), 8) : c;
}

/* Both take the cannon's muzzle in pixels, which is how the boss's drawing
 * thinks; inside they move to sixteenths, which is how the shots live. */
static void boss_spread(g_t *g, int x, int y, int n, int spacing, int speed, int kind)
{
    int base = g_angle_to_player(g, x * FX_ONE, y * FX_ONE);
    for (int i = 0; i < n; i++) {
        int ang = base + (i - (n - 1) / 2) * spacing;
        g_spawn_eshot(g, x * FX_ONE, y * FX_ONE, gx_cos(ang) * speed / 256,
                      gx_sin(ang) * speed / 256, kind);
    }
}

static void boss_radial(g_t *g, int x, int y, int n, int offset, int speed, int kind)
{
    for (int i = 0; i < n; i++) {
        int ang = offset + i * 256 / n;
        g_spawn_eshot(g, x * FX_ONE, y * FX_ONE, gx_cos(ang) * speed / 256,
                      gx_sin(ang) * speed / 256, kind);
    }
}

/* Common entrance: it descends from the top to 'target' and then returns true. */
static bool boss_enter(g_boss_t *b, int target_y)
{
    if (UNFX(b->y) < target_y) {
        b->y = (int16_t)(b->y + 12);
        return false;
    }
    return true;
}

/* --------------------------------------------------------------------------
 * Boss 1 - GUARDIAN CARMESI
 *
 * A wide cruiser with two cannon gondolas. While the gondolas live, the core
 * is armoured; when both fall, it opens up and fires in a fan.
 * -------------------------------------------------------------------------- */

#define CARM_POD_DX     30
#define CARM_POD_DY      3

static void carmesi_init(g_t *g, g_boss_t *b)
{
    (void)g;
    b->part_hp[0] = 22;
    b->part_hp[1] = 22;
    b->part_hp[2] = 66;     /* core */
    b->x = FX(GX_W / 2);
    b->y = FX(-30);
}

static void carmesi_hitbox(const g_boss_t *b, int part, int *x, int *y, int *r)
{
    int bx = UNFX(b->x), by = UNFX(b->y);

    if (part == 0 || part == 1) {
        *x = bx + (part == 0 ? -CARM_POD_DX : CARM_POD_DX);
        *y = by + CARM_POD_DY;
        *r = b->part_hp[part] > 0 ? 11 : 0;
        return;
    }
    *x = bx;
    *y = by;
    /* the core can only be hurt once both gondolas have fallen */
    *r = (b->part_hp[0] <= 0 && b->part_hp[1] <= 0) ? 13 : 0;
}

static void carmesi_think(g_t *g, g_boss_t *b)
{
    if (b->phase == 0) {
        if (boss_enter(b, 46)) {
            b->phase = 1;
        }
        return;
    }

    bool open = (b->part_hp[0] <= 0 && b->part_hp[1] <= 0);
    int speed = open ? 26 : 16;

    /* lateral sweep: a pure sine, which is what makes the pattern read */
    b->x = (int16_t)(FX(GX_W / 2) + gx_sin(b->t * (open ? 3 : 2)) * FX(52) / 256);
    b->y = (int16_t)(FX(46) + gx_sin(b->t * 2 + 64) * FX(6) / 256);

    b->timer++;

    if (!open) {
        /* the gondolas take turns */
        if (b->timer % 52 == 0) {
            int pod = (b->timer / 52) & 1;
            if (b->part_hp[pod] > 0) {
                boss_spread(g, UNFX(b->x) + (pod ? CARM_POD_DX : -CARM_POD_DX),
                            UNFX(b->y) + 12, 3, 12, speed, ES_BALL);
                g_beep(200, 25);
            }
        }
        if (b->timer % 170 == 90) {
            for (int pod = 0; pod < 2; pod++) {
                if (b->part_hp[pod] > 0) {
                    boss_spread(g, UNFX(b->x) + (pod ? CARM_POD_DX : -CARM_POD_DX),
                                UNFX(b->y) + 12, 5, 9, speed - 4, ES_HEAVY);
                }
            }
            g_beep(150, 60);
        }
    } else {
        if (b->timer % 96 == 0) {
            boss_radial(g, UNFX(b->x), UNFX(b->y), 14, b->t, 24, ES_BALL);
            g_beep(120, 70);
            g->shake = 5;
        }
        if (b->timer % 44 == 20) {
            boss_spread(g, UNFX(b->x), UNFX(b->y) + 10, 3, 8, 34, ES_AIMED);
        }
    }
}

static void carmesi_draw(g_t *g, const g_boss_t *b)
{
    gx_buf_t *buf = &g->buf;
    int x = UNFX(b->x), y = UNFX(b->y);

    uint16_t hull  = gx_rgb(0xA3242A);
    uint16_t hull2 = gx_rgb(0x6E1418);
    uint16_t deck  = gx_rgb(0xE5484D);
    uint16_t trim  = gx_rgb(0xFFD60A);
    uint16_t dark  = gx_rgb(0x2A0A0C);

    if (b->flash) {
        hull  = hit_tint(hull, true);
        hull2 = hit_tint(hull2, true);
        deck  = hit_tint(deck, true);
    }

    /* hull: two overlapping trapezoids */
    gx_round(buf, x - 46, y - 14, 92, 28, 10, hull2);
    gx_round(buf, x - 40, y - 11, 80, 20, 7, hull);
    gx_hline(buf, x - 34, y - 11, 68, deck);
    gx_hline(buf, x - 30, y + 8, 60, hull2);

    /* engines on top */
    for (int i = -1; i <= 1; i += 2) {
        int ex = x + i * 16;
        gx_rect(buf, ex - 4, y - 18, 8, 6, hull2);
        gx_glow(buf, ex, y - 19, 5, gx_rgb(0xFF9F0A), 11 + gx_sin(b->t * 12) / 40);
    }

    /* gondolas */
    for (int p = 0; p < 2; p++) {
        int px = x + (p ? CARM_POD_DX : -CARM_POD_DX);
        int py = y + CARM_POD_DY;
        if (b->part_hp[p] > 0) {
            gx_disc(buf, px, py, 10, hull2);
            gx_disc(buf, px, py, 8, hull);
            gx_disc(buf, px, py - 2, 4, deck);
            gx_rect(buf, px - 3, py + 8, 6, 8, hull2);
            gx_rect(buf, px - 1, py + 10, 2, 7, trim);
        } else {
            gx_disc(buf, px, py, 9, dark);
            gx_disc(buf, px, py, 5, gx_rgb(0x1B0203));
            if ((b->t / 3) % 2) {
                gx_glow(buf, px + (int)(g_rnd(g) % 7) - 3, py, 3,
                        gx_rgb(0xFF9F0A), 12);
            }
        }
    }

    /* core */
    bool open = (b->part_hp[0] <= 0 && b->part_hp[1] <= 0);
    if (open) {
        int pulse = 9 + gx_sin(b->t * 9) / 64;
        gx_disc(buf, x, y, 13, dark);
        gx_glow(buf, x, y, 13, gx_rgb(0xFF2D55), 14);
        gx_disc(buf, x, y, pulse > 4 ? pulse : 4, gx_rgb(0xFFE45E));
        gx_ring(buf, x, y, 13, trim);
    } else {
        gx_disc(buf, x, y, 12, hull2);
        gx_ring(buf, x, y, 12, gx_rgb(0x8A8A90));
        gx_ring(buf, x, y, 8, gx_rgb(0x8A8A90));
        gx_disc(buf, x, y, 5, gx_rgb(0x3D465F));
    }
}

/* --------------------------------------------------------------------------
 * Boss 2 - KRAKEN ORBITAL
 *
 * A mechanical creature with four undulating arms. The arms can be broken and
 * while they are whole they hurt on contact. The body can always be hurt, but
 * it hits far harder while it still has arms.
 * -------------------------------------------------------------------------- */

#define KRAK_ARMS       4
#define KRAK_SEGS       7

/* Position of a segment of arm 'arm'. It lives in a single function because
 * the drawing and the hit box both use it: if they were separated, they would
 * stop agreeing. */
static void kraken_seg(const g_boss_t *b, int arm, int seg, int *ox, int *oy)
{
    int base = 34 + arm * 20;
    int wob  = gx_sin(b->t * 5 + seg * 26 + arm * 51) * 16 / 256;
    /* the undulation grows towards the tip, but not too much: with too great
     * an amplitude the four arms cross and it looks like a smudge */
    int ang  = base + wob * (seg + 2) / 6;
    int dist = 12 + seg * 7;

    *ox = gx_cos(ang) * dist / 256;
    *oy = gx_sin(ang) * dist / 256;
}

static void kraken_init(g_t *g, g_boss_t *b)
{
    (void)g;
    for (int i = 0; i < KRAK_ARMS; i++) {
        b->part_hp[i] = 15;
    }
    b->part_hp[KRAK_ARMS] = 62;
    b->x = FX(GX_W / 2);
    b->y = FX(-30);
}

static void kraken_hitbox(const g_boss_t *b, int part, int *x, int *y, int *r)
{
    int bx = UNFX(b->x), by = UNFX(b->y);

    if (part < KRAK_ARMS) {
        int ox, oy;
        kraken_seg(b, part, KRAK_SEGS / 2, &ox, &oy);
        *x = bx + ox;
        *y = by + oy;
        *r = b->part_hp[part] > 0 ? 7 : 0;
        return;
    }
    *x = bx;
    *y = by;
    *r = 15;
}

static void kraken_think(g_t *g, g_boss_t *b)
{
    if (b->phase == 0) {
        if (boss_enter(b, 54)) {
            b->phase = 1;
        }
        return;
    }

    b->timer++;

    if (b->phase == 2) {
        /* charge: it throws itself at the player and comes back */
        b->y = (int16_t)(b->y + b->vy);
        b->x = (int16_t)(b->x + b->vx);
        if (b->timer > 26) {
            b->vy = -20;
        }
        if (UNFX(b->y) <= 54 && b->timer > 26) {
            b->y = FX(54);
            b->vy = 0;
            b->vx = 0;
            b->phase = 1;
            b->timer = 0;
        }
        return;
    }

    b->x = (int16_t)(FX(GX_W / 2) + gx_sin(b->t * 2) * FX(46) / 256);
    b->y = (int16_t)(FX(54) + gx_sin(b->t * 3 + 30) * FX(8) / 256);

    int arms = 0;
    for (int i = 0; i < KRAK_ARMS; i++) {
        if (b->part_hp[i] > 0) {
            arms++;
        }
    }

    if (b->timer % 78 == 40) {
        /* homing orbs: you have to move, not dodge once */
        for (int i = 0; i < 3; i++) {
            int ang = g_angle_to_player(g, b->x, b->y) + (i - 1) * 18;
            g_spawn_eshot(g, b->x, b->y + FX(8), gx_cos(ang) * 20 / 256,
                          gx_sin(ang) * 20 / 256, ES_HOMING);
        }
        g_beep(240, 40);
    }

    /* with fewer arms, more agitated */
    if (b->timer % (arms > 2 ? 120 : 70) == 0) {
        boss_radial(g, UNFX(b->x), UNFX(b->y), 10, b->t * 3, 22, ES_BALL);
    }

    if (b->timer > (arms ? 220 : 150)) {
        b->timer = 0;
        b->phase = 2;
        int ang = g_angle_to_player(g, b->x, b->y);
        b->vx = (int16_t)(gx_cos(ang) * 30 / 256);
        b->vy = (int16_t)(gx_sin(ang) * 46 / 256);
        if (b->vy < 8) {
            b->vy = 30;
        }
        g_beep(110, 120);
    }
}

static void kraken_draw(g_t *g, const g_boss_t *b)
{
    gx_buf_t *buf = &g->buf;
    int x = UNFX(b->x), y = UNFX(b->y);

    uint16_t skin  = gx_rgb(0x1E7A6E);
    uint16_t skin2 = gx_rgb(0x0E4A44);
    uint16_t glow  = gx_rgb(0x2AF0C8);
    uint16_t eye   = gx_rgb(0xFF6FAE);

    if (b->flash) {
        skin  = hit_tint(skin, true);
        skin2 = hit_tint(skin2, true);
    }

    /* arms first: they end up behind the body */
    for (int arm = 0; arm < KRAK_ARMS; arm++) {
        bool alive = b->part_hp[arm] > 0;
        int last = alive ? KRAK_SEGS : 2;    /* if it died, the stump is left */
        for (int seg = last - 1; seg >= 0; seg--) {
            int ox, oy;
            kraken_seg(b, arm, seg, &ox, &oy);
            int r = 6 - seg / 2;
            if (r < 2) {
                r = 2;
            }
            gx_disc(buf, x + ox, y + oy, r, alive ? skin2 : gx_rgb(0x2A2A32));
            gx_disc(buf, x + ox, y + oy - 1, r - 1 > 0 ? r - 1 : 1,
                    alive ? skin : gx_rgb(0x3A3A44));
            if (alive && seg == KRAK_SEGS / 2) {
                gx_glow(buf, x + ox, y + oy, 5, glow, 7);
            }
        }
        if (!alive && (b->t / 4) % 2) {
            int ox, oy;
            kraken_seg(b, arm, 2, &ox, &oy);
            gx_glow(buf, x + ox, y + oy, 4, gx_rgb(0xFF9F0A), 10);
        }
    }

    /* body */
    gx_disc(buf, x, y, 17, skin2);
    gx_disc(buf, x, y - 2, 14, skin);
    gx_disc(buf, x, y - 5, 9, gx_mix(skin, gx_rgb(0xFFFFFF), 4));
    gx_ring(buf, x, y, 17, glow);

    /* eyes: the only part that changes with the state */
    int blink = (b->phase == 2) ? 4 : 3;
    gx_disc(buf, x - 6, y + 1, blink, eye);
    gx_disc(buf, x + 6, y + 1, blink, eye);
    gx_px(buf, x - 6, y + 1, gx_rgb(0xFFFFFF));
    gx_px(buf, x + 6, y + 1, gx_rgb(0xFFFFFF));

    int pulse = 6 + gx_sin(b->t * 7) / 64;
    gx_glow(buf, x, y + 10, pulse > 3 ? pulse : 3, glow, 10);
}

/* --------------------------------------------------------------------------
 * Boss 3 - NUCLEO DEL CINTURON
 *
 * An asteroid fortress: four blocks orbiting the core. The core is armoured
 * while a single block stands.
 * -------------------------------------------------------------------------- */

#define NUC_SEGS    4
#define NUC_RADIUS  36

static void nucleo_init(g_t *g, g_boss_t *b)
{
    (void)g;
    for (int i = 0; i < NUC_SEGS; i++) {
        b->part_hp[i] = 18;
    }
    b->part_hp[NUC_SEGS] = 72;
    b->x = FX(GX_W / 2);
    b->y = FX(-30);
}

static void nucleo_seg_pos(const g_boss_t *b, int seg, int *x, int *y)
{
    int ang = b->a + seg * (256 / NUC_SEGS);
    *x = UNFX(b->x) + gx_cos(ang) * NUC_RADIUS / 256;
    *y = UNFX(b->y) + gx_sin(ang) * NUC_RADIUS / 256;
}

static void nucleo_hitbox(const g_boss_t *b, int part, int *x, int *y, int *r)
{
    if (part < NUC_SEGS) {
        nucleo_seg_pos(b, part, x, y);
        *r = b->part_hp[part] > 0 ? 10 : 0;
        return;
    }
    *x = UNFX(b->x);
    *y = UNFX(b->y);

    for (int i = 0; i < NUC_SEGS; i++) {
        if (b->part_hp[i] > 0) {
            *r = 0;         /* armoured */
            return;
        }
    }
    *r = 14;
}

static void nucleo_think(g_t *g, g_boss_t *b)
{
    if (b->phase == 0) {
        if (boss_enter(b, 66)) {
            b->phase = 1;
        }
        b->a += 2;
        return;
    }

    int alive = 0;
    for (int i = 0; i < NUC_SEGS; i++) {
        if (b->part_hp[i] > 0) {
            alive++;
        }
    }

    b->a += alive ? 2 : 4;
    b->timer++;

    b->x = (int16_t)(FX(GX_W / 2) + gx_sin(b->t * (alive ? 1 : 3)) * FX(38) / 256);

    if (alive) {
        /* each block spits outwards: the cloud turns with the fortress */
        if (b->timer % 54 == 0) {
            for (int i = 0; i < NUC_SEGS; i++) {
                if (b->part_hp[i] <= 0) {
                    continue;
                }
                int sx, sy;
                nucleo_seg_pos(b, i, &sx, &sy);
                int ang = b->a + i * (256 / NUC_SEGS);
                g_spawn_eshot(g, FX(sx), FX(sy), gx_cos(ang) * 26 / 256,
                              gx_sin(ang) * 26 / 256, ES_BALL);
            }
            g_beep(210, 30);
        }
        if (b->timer % 96 == 40) {
            for (int i = 0; i < 2; i++) {
                int x = g_rnd_range(g, 20, GX_W - 20);
                g_spawn_eshot(g, FX(x), b->y, g_rnd_range(g, -6, 6), 22, ES_ROCK);
            }
        }
    } else {
        /* a three-armed spiral, in bursts */
        if (b->timer % 150 < 40 && b->timer % 5 == 0) {
            boss_radial(g, UNFX(b->x), UNFX(b->y), 3, b->timer * 11, 24, ES_HEAVY);
        }
        if (b->timer % 150 == 100) {
            boss_radial(g, UNFX(b->x), UNFX(b->y), 16, b->t * 5, 20, ES_BALL);
            g->shake = 6;
            g_beep(100, 80);
        }
    }
}

static void nucleo_draw(g_t *g, const g_boss_t *b)
{
    gx_buf_t *buf = &g->buf;
    int x = UNFX(b->x), y = UNFX(b->y);

    uint16_t rock  = gx_rgb(0x6B5B45);
    uint16_t rock2 = gx_rgb(0x3A3020);
    uint16_t vein  = gx_rgb(0xB072F0);
    uint16_t core  = gx_rgb(0xFF9F0A);

    if (b->flash) {
        rock  = hit_tint(rock, true);
        rock2 = hit_tint(rock2, true);
    }

    /* joining ring */
    gx_ring(buf, x, y, NUC_RADIUS, gx_rgb(0x2A2436));
    gx_ring(buf, x, y, NUC_RADIUS - 1, gx_rgb(0x1A1626));

    int alive = 0;
    for (int i = 0; i < NUC_SEGS; i++) {
        int sx, sy;
        nucleo_seg_pos(b, i, &sx, &sy);

        if (b->part_hp[i] > 0) {
            alive++;
            gx_disc(buf, sx, sy, 10, rock2);
            gx_disc(buf, sx, sy - 1, 8, rock);
            gx_disc(buf, sx - 3, sy - 3, 3, gx_mix(rock, gx_rgb(0xFFFFFF), 5));
            gx_ring(buf, sx, sy, 10, vein);
            /* cannon muzzle, facing outwards */
            int ang = b->a + i * (256 / NUC_SEGS);
            int mx = sx + gx_cos(ang) * 9 / 256;
            int my = sy + gx_sin(ang) * 9 / 256;
            gx_disc(buf, mx, my, 2, gx_rgb(0x120E1A));
        } else {
            gx_disc(buf, sx, sy, 5, gx_rgb(0x1A1626));
            if ((b->t / 5) % 2) {
                gx_glow(buf, sx, sy, 4, gx_rgb(0xFF4A3D), 9);
            }
        }
    }

    /* core */
    gx_disc(buf, x, y, 16, rock2);
    gx_disc(buf, x, y, 14, rock);
    gx_ring(buf, x, y, 14, vein);

    if (alive) {
        /* armoured: plates closed */
        for (int i = 0; i < 6; i++) {
            int ang = i * 42 + b->a / 2;
            gx_line(buf, x, y, x + gx_cos(ang) * 13 / 256,
                    y + gx_sin(ang) * 13 / 256, rock2);
        }
        gx_disc(buf, x, y, 5, gx_rgb(0x2A2436));
    } else {
        int pulse = 10 + gx_sin(b->t * 8) / 40;
        gx_glow(buf, x, y, 15, core, 14);
        gx_disc(buf, x, y, pulse > 4 ? pulse : 4, gx_rgb(0xFFE45E));
        gx_ring(buf, x, y, pulse + 2, gx_rgb(0xFFFFFF));
    }
}

/* -------------------------------------------------------------------------- */

const gx_boss_def_t gx_bosses[] = {
    { N_("GUARDIAN CARMESI"),   110, 2, carmesi_init, carmesi_think, carmesi_draw, carmesi_hitbox },
    { N_("KRAKEN ORBITAL"),     122, 4, kraken_init,  kraken_think,  kraken_draw,  kraken_hitbox  },
    { N_("NUCLEO DEL CINTURON"),144, 4, nucleo_init,  nucleo_think,  nucleo_draw,  nucleo_hitbox  },
};

const int gx_boss_count = sizeof(gx_bosses) / sizeof(gx_bosses[0]);

/* --------------------------------------------------------------------------
 * Bosses: what they share
 * -------------------------------------------------------------------------- */

void gx_boss_start(g_t *g, int def)
{
    g_boss_t *b = &g->boss;
    const gx_boss_def_t *d = &gx_bosses[def % gx_boss_count];

    memset(b, 0, sizeof(*b));
    b->def    = (uint8_t)(def % gx_boss_count);
    b->parts  = d->parts;
    b->hp_max = d->hp;
    b->hp     = d->hp;
    b->alive  = 1;

    if (d->init) {
        d->init(g, b);
    }
}

void gx_boss_update(g_t *g)
{
    g_boss_t *b = &g->boss;
    if (!b->alive) {
        return;
    }
    const gx_boss_def_t *d = &gx_bosses[b->def];

    b->t++;
    if (b->flash) {
        b->flash--;
    }

    if (b->dying) {
        b->dying--;
        /* death throes: chained explosions all over the body */
        if ((b->dying % 4) == 0) {
            int rx = UNFX(b->x) + g_rnd_range(g, -34, 34);
            int ry = UNFX(b->y) + g_rnd_range(g, -18, 18);
            g_boom(g, rx, ry, g_rnd_range(g, 6, 14), gx_rgb(0xFFE45E));
            g_beep(90 + (int)(g_rnd(g) % 200), 40);
        }
        g->shake = 4;
        if (b->dying == 0) {
            b->alive = 0;
            g->flash_screen = 8;
        }
        return;
    }

    d->think(g, b);
}

void gx_boss_draw(g_t *g)
{
    const g_boss_t *b = &g->boss;
    if (!b->alive) {
        return;
    }
    if (b->dying && (b->dying / 2) % 2) {
        /* it blinks as it falls apart */
        gx_boss_def_t const *d = &gx_bosses[b->def];
        g_boss_t tmp = *b;
        tmp.flash = 1;
        d->draw(g, &tmp);
        return;
    }
    gx_bosses[b->def].draw(g, b);
}

bool gx_boss_hit(g_t *g, int x, int y, int dmg)
{
    g_boss_t *b = &g->boss;
    if (!b->alive || b->dying) {
        return false;
    }
    const gx_boss_def_t *d = &gx_bosses[b->def];

    for (int part = 0; part <= b->parts; part++) {
        int px, py, pr;
        d->hitbox(b, part, &px, &py, &pr);
        if (pr <= 0) {
            continue;
        }
        int dx = x - px, dy = y - py;
        if (dx * dx + dy * dy > pr * pr) {
            continue;
        }

        b->flash = 1;
        b->hp = (int16_t)(b->hp - dmg);
        b->part_hp[part] = (int16_t)(b->part_hp[part] - dmg);

        if (b->part_hp[part] <= 0) {
            b->part_hp[part] = 0;
            if (part < b->parts) {
                g_boom(g, px, py, 14, gx_rgb(0xFF9F0A));
                g->shake = 6;
                g_add_score(g, 1000);
                g_beep(120, 90);
            }
        } else {
            g_spawn_fx(g, FX(x), FX(y), 0, 0, FX_SPARK, gx_rgb(0xFFE45E), 6);
        }

        if (b->hp <= 0) {
            b->hp = 0;
            b->dying = 70;
            g_add_score(g, 10000);
        }
        return true;
    }
    return false;
}
