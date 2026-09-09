/*
 * CLAUDE JUMP - physics and rules
 *
 * This file deliberately includes neither LVGL nor the HAL: these are the pure
 * rules, whole and deterministic given the generator. The only thing that
 * leaves here is cj_sfx(), which the app implements while looking at its sound
 * switch.
 *
 * All in 32-bit integers: the ESP32-S3 has neither floating-point division nor
 * 64-bit arithmetic in hardware, and in a dynamic app every compiler helper
 * dragged in is one more symbol to resolve and a fatter .so. The game's only
 * float arithmetic is in cjump.c, reading the sensor.
 */
#include "cjump.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * The five zones
 *
 * They change the sky, the platforms' colour and the difficulty. The cut is by
 * metres, and changing zone rebuilds the whole background: it is the game's
 * one expensive frame and it happens every few hundred metres.
 * -------------------------------------------------------------------------- */
const cj_zone_t cj_zones[CJ_ZONES] = {
    {   /* 0. The meadow: daytime sky and grass platforms */
        .sky_top = 0x7EC8FF, .sky_bot = 0xC9F0FF,
        .deco_a  = 0xFFFFFF, .deco_b  = 0xDCEEFF,
        .plat_a  = 0x4ADE80, .plat_b  = 0x1E7A3C,
        .from_m  = 0, .deco_kind = 0,
        .moving_pc = 0, .fragile_pc = 0, .fading_pc = 0, .bug_pc = 0,
        .gap_min = 26, .gap_max = 40,
    },
    {   /* 1. Sunset: orange and pink, the moving ones appear */
        .sky_top = 0x3B2A6B, .sky_bot = 0xFF9E5E,
        .deco_a  = 0xFFE3C0, .deco_b  = 0xE79A86,
        .plat_a  = 0xFFB03A, .plat_b  = 0xB65E00,
        .from_m  = 150, .deco_kind = 0,
        .moving_pc = 22, .fragile_pc = 8, .fading_pc = 0, .bug_pc = 0,
        .gap_min = 30, .gap_max = 46,
    },
    {   /* 2. Night: stars and the first bugs */
        .sky_top = 0x070B24, .sky_bot = 0x1E3A6E,
        .deco_a  = 0xFFFFFF, .deco_b  = 0x7BE9FF,
        .plat_a  = 0x7BB6FF, .plat_b  = 0x2A4FA0,
        .from_m  = 400, .deco_kind = 1,
        .moving_pc = 26, .fragile_pc = 14, .fading_pc = 10, .bug_pc = 30,
        .gap_min = 32, .gap_max = 50,
    },
    {   /* 3. Space: deep violet, planets and more bugs */
        .sky_top = 0x120033, .sky_bot = 0x3A0E60,
        .deco_a  = 0xFF6FAE, .deco_b  = 0x2AF0C8,
        .plat_a  = 0xB072F0, .plat_b  = 0x5B2AA0,
        .from_m  = 800, .deco_kind = 2,
        .moving_pc = 30, .fragile_pc = 16, .fading_pc = 16, .bug_pc = 45,
        .gap_min = 34, .gap_max = 54,
    },
    {   /* 4. The aurora: the last one, everything together */
        .sky_top = 0x001A2E, .sky_bot = 0x0E7A6B,
        .deco_a  = 0x2AF0C8, .deco_b  = 0xB072F0,
        .plat_a  = 0x2AF0C8, .plat_b  = 0x0E8A78,
        .from_m  = 1400, .deco_kind = 1,
        .moving_pc = 34, .fragile_pc = 18, .fading_pc = 20, .bug_pc = 55,
        .gap_min = 36, .gap_max = 58,
    },
};

/* --------------------------------------------------------------------------
 * Random numbers
 *
 * A 32-bit xorshift. Cheap, with no global state and repeatable, which is what
 * is needed to be able to reproduce an odd game.
 * -------------------------------------------------------------------------- */

uint32_t cj_rand(cj_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

int cj_rand_range(cj_t *g, int lo, int hi)
{
    if (hi <= lo) {
        return lo;
    }
    return lo + (int)(cj_rand(g) % (uint32_t)(hi - lo + 1));
}

static int chance(cj_t *g, int percent)
{
    return percent > 0 && (int)(cj_rand(g) % 100u) < percent;
}

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* --------------------------------------------------------------------------
 * Current zone
 * -------------------------------------------------------------------------- */

static uint8_t zone_for(uint32_t metros)
{
    uint8_t z = 0;
    for (uint8_t i = 0; i < CJ_ZONES; i++) {
        if (metros >= cj_zones[i].from_m) {
            z = i;
        }
    }
    return z;
}

/* --------------------------------------------------------------------------
 * Adding and removing
 * -------------------------------------------------------------------------- */

static cj_coin_t *coin_free(cj_t *g)
{
    for (int i = 0; i < MAX_COINS; i++) {
        if (!g->coins[i].active) {
            return &g->coins[i];
        }
    }
    return NULL;
}

static void spawn_particles(cj_t *g, int32_t x, int32_t y, int n, uint32_t hex,
                            int speed)
{
    uint16_t c = cj_rgb(hex);
    for (int k = 0; k < n; k++) {
        cj_part_t *p = NULL;
        for (int i = 0; i < MAX_PARTS; i++) {
            if (!g->parts[i].life) {
                p = &g->parts[i];
                break;
            }
        }
        if (!p) {
            return;
        }
        p->x     = x;
        p->y     = y;
        p->vx    = (int16_t)cj_rand_range(g, -speed, speed);
        p->vy    = (int16_t)cj_rand_range(g, -speed, speed / 2);
        p->life  = (uint8_t)cj_rand_range(g, 8, 16);
        p->color = c;
        p->drawn = 0;
    }
}

/* --------------------------------------------------------------------------
 * World generation
 *
 * One new platform per gap, always above the last. The horizontal limit is not
 * decorative: with the absolute control the critter covers some 5.5 px per
 * frame, so a short gap gives little flight time and a platform at the far end
 * would be unreachable. The reach is estimated from the gap and the
 * displacement is clamped to it.
 * -------------------------------------------------------------------------- */

static void spawn_platform(cj_t *g, int32_t wy, const cj_zone_t *z)
{
    cj_plat_t *p = NULL;
    for (int i = 0; i < MAX_PLATS; i++) {
        if (!g->plats[i].active) {
            p = &g->plats[i];
            break;
        }
    }
    if (!p) {
        return;
    }

    int gap    = (int)((g->gen_y - wy) / FX);
    int reach  = 30 + gap * 2;
    int lo     = g->last_plat_x - reach;
    int hi     = g->last_plat_x + reach;
    lo = lo < 2 ? 2 : lo;
    hi = hi > CJ_W - PLAT_W - 2 ? CJ_W - PLAT_W - 2 : hi;
    int px = cj_rand_range(g, lo, hi);

    memset(p, 0, sizeof(*p));
    p->active = 1;
    p->x      = (int32_t)px * FX;
    p->y      = wy;
    p->type   = PLAT_NORMAL;

    /* After a fragile one there is always a whole one: a fragile one gives no
     * boost, so two in a row are a guaranteed fall and that is not difficulty,
     * it is a trap. */
    if (g->last_type != PLAT_FRAGILE && chance(g, z->fragile_pc)) {
        p->type = PLAT_FRAGILE;
    } else if (chance(g, z->fading_pc)) {
        p->type = PLAT_FADING;
    } else if (chance(g, z->moving_pc)) {
        p->type = PLAT_MOVING;
        p->vx   = (int16_t)(cj_rand_range(g, 8, 16) * (chance(g, 50) ? 1 : -1));
    }

    if (p->type != PLAT_FRAGILE) {
        if (chance(g, 6)) {
            p->item = ITEM_SPRING;
        } else if (chance(g, 2)) {
            p->item = ITEM_ROCKET;
        }
    }

    /* A coin floating above the platform, not stuck to it: that way you have
     * to aim the jump and passing alongside is not enough. */
    if (p->item == ITEM_NONE && chance(g, 32)) {
        cj_coin_t *c = coin_free(g);
        if (c) {
            memset(c, 0, sizeof(*c));
            c->active = 1;
            c->x = p->x + (PLAT_W / 2 - COIN_R) * FX;
            c->y = p->y - (int32_t)cj_rand_range(g, 14, 26) * FX;
            c->phase = (uint8_t)cj_rand_range(g, 0, 15);
        }
    }

    g->last_plat_x = (int16_t)px;
    g->last_type   = p->type;
    g->gen_y = wy;
}

static void spawn_bug(cj_t *g, int32_t wy)
{
    for (int i = 0; i < MAX_BUGS; i++) {
        if (g->bugs[i].active) {
            continue;
        }
        cj_bug_t *b = &g->bugs[i];
        memset(b, 0, sizeof(*b));
        b->active = 1;
        b->kind   = (uint8_t)(chance(g, 50) ? 1 : 0);
        b->x      = (int32_t)cj_rand_range(g, 4, CJ_W - BUG_W - 4) * FX;
        b->y      = wy;
        b->vx     = (int16_t)(cj_rand_range(g, 6, 14) * (chance(g, 50) ? 1 : -1));
        return;
    }
}

static void generate(cj_t *g)
{
    const cj_zone_t *z = &cj_zones[g->zone];

    while (g->gen_y > g->cam_y - 40 * FX) {
        int gap = cj_rand_range(g, z->gap_min, z->gap_max);
        int32_t wy = g->gen_y - (int32_t)gap * FX;
        spawn_platform(g, wy, z);

        /* The bugs go between platforms, never on one: if they appeared stuck
         * to the only possible foothold there would be no way to dodge
         * them. */
        if (chance(g, z->bug_pc / 4)) {
            spawn_bug(g, wy - (int32_t)cj_rand_range(g, 12, 22) * FX);
        }
    }
}

/* --------------------------------------------------------------------------
 * New game
 * -------------------------------------------------------------------------- */

void cj_game_reset(cj_t *g)
{
    memset(g->plats, 0, sizeof(g->plats));
    memset(g->coins, 0, sizeof(g->coins));
    memset(g->bugs, 0, sizeof(g->bugs));
    memset(g->parts, 0, sizeof(g->parts));

    g->bg_seed = cj_rand(g);        /* this game's sky                        */
    g->cam_y   = 0;
    g->hx      = (int32_t)((CJ_W - HERO_W) / 2) * FX;
    g->hy      = (int32_t)(CJ_H - 70) * FX;
    g->hvx     = 0;
    g->hvy     = V_JUMP;
    g->target_x = g->hx;
    g->facing  = 0;
    g->rocket  = 0;
    g->squash  = 0;
    g->start_y = g->hy;
    g->top_y   = g->hy;
    g->score   = 0;
    g->coins_run = 0;
    g->new_record = 0;
    g->zone    = 0;
    g->zone_changed = 1;
    g->hero_drawn = 0;
    g->hud_dirty  = 1;
    g->hud_score  = 0xFFFFFFFFu;    /* forces the score's first drawing       */
    g->hud_coins  = 0xFFFFFFFFu;

    /* The starting platform, always whole and underfoot. */
    g->last_plat_x = (int16_t)(CJ_W / 2 - PLAT_W / 2);
    g->last_type   = PLAT_NORMAL;
    g->gen_y = (int32_t)(CJ_H - 40) * FX;
    cj_plat_t *p = &g->plats[0];
    memset(p, 0, sizeof(*p));
    p->active = 1;
    p->type   = PLAT_NORMAL;
    p->x      = (int32_t)g->last_plat_x * FX;
    p->y      = g->gen_y;

    generate(g);
    cj_dirty_all(&g->d_prev);
    cj_dirty_reset(&g->d_cur);
    cj_dirty_reset(&g->d_bg);
    g->state = ST_PLAY;
}

/* --------------------------------------------------------------------------
 * Bounces
 * -------------------------------------------------------------------------- */

static void bounce(cj_t *g, int32_t v)
{
    g->hvy    = v;
    g->squash = 4;
}

static void land_on(cj_t *g, cj_plat_t *p)
{
    if (p->item == ITEM_SPRING) {
        bounce(g, V_SPRING);
        cj_sfx(520, 30);
        cj_sfx(1040, 45);
        return;
    }
    if (p->item == ITEM_ROCKET) {
        p->item   = ITEM_NONE;
        g->rocket = ROCKET_FRAMES;
        cj_sfx(300, 40);
        cj_sfx(700, 40);
        cj_sfx(1200, 60);
        return;
    }

    bounce(g, V_JUMP);
    cj_sfx(680, 22);

    if (p->type == PLAT_FADING) {
        p->fade = 1;
    }
}

/* --------------------------------------------------------------------------
 * Collisions
 * -------------------------------------------------------------------------- */

static bool overlap(int32_t ax, int32_t ay, int aw, int ah,
                    int32_t bx, int32_t by, int bw, int bh)
{
    return ax < bx + (int32_t)bw * FX && ax + (int32_t)aw * FX > bx &&
           ay < by + (int32_t)bh * FX && ay + (int32_t)ah * FX > by;
}

/* You only land while FALLING and only if the feet crossed the platform's edge
 * between the previous frame and this one. Comparing loose positions skips the
 * platform when the velocity exceeds its height, which with 12 px per frame
 * and 6 px of platform happens all the time. */
static void check_platforms(cj_t *g, int32_t feet_prev)
{
    if (g->hvy <= 0 || g->rocket) {
        return;
    }
    int32_t feet = g->hy + (int32_t)HERO_H * FX;

    for (int i = 0; i < MAX_PLATS; i++) {
        cj_plat_t *p = &g->plats[i];
        if (!p->active || p->fade) {
            continue;
        }
        if (feet_prev > p->y || feet < p->y) {
            continue;
        }
        /* The feet are narrower than the body: landing with a pixel hanging
         * over the void feels unfair in both directions. */
        int32_t fx0 = g->hx + 3 * FX;
        int32_t fx1 = g->hx + (int32_t)(HERO_W - 3) * FX;
        if (fx1 < p->x || fx0 > p->x + (int32_t)PLAT_W * FX) {
            continue;
        }
        /* The fragile one breaks WITHOUT stopping the fall: lifting the feet
         * onto the edge and then letting it drop looks like a one-frame catch,
         * which reads as a collision bug and not as a platform giving way. */
        if (p->type == PLAT_FRAGILE) {
            p->active = 0;
            spawn_particles(g, p->x + (PLAT_W / 2) * FX, p->y, 7, 0xB07A4A, 26);
            cj_sfx(220, 40);
            return;
        }
        g->hy = p->y - (int32_t)HERO_H * FX;
        land_on(g, p);
        return;
    }
}

static void check_coins(cj_t *g)
{
    for (int i = 0; i < MAX_COINS; i++) {
        cj_coin_t *c = &g->coins[i];
        if (!c->active) {
            continue;
        }
        if (!overlap(g->hx, g->hy, HERO_W, HERO_H,
                     c->x, c->y, COIN_R * 2, COIN_R * 2)) {
            continue;
        }
        c->active = 0;
        g->coins_run += 2;
        g->hud_dirty = 1;
        spawn_particles(g, c->x, c->y, 5, 0xFFD60A, 30);
        cj_sfx(1400, 25);
        cj_sfx(1900, 30);
    }
}

/* Stomping a bug bursts it and gives a bounce; touching it from the side or
 * from below is the end. The boundary is the usual one in a platformer: coming
 * down and being above its midpoint. */
static bool check_bugs(cj_t *g, int32_t feet_prev)
{
    for (int i = 0; i < MAX_BUGS; i++) {
        cj_bug_t *b = &g->bugs[i];
        if (!b->active) {
            continue;
        }
        if (!overlap(g->hx, g->hy, HERO_W, HERO_H, b->x, b->y, BUG_W, BUG_H)) {
            continue;
        }
        if (g->rocket) {
            b->active = 0;
            spawn_particles(g, b->x, b->y, 8, 0xFF4A3D, 34);
            cj_sfx(900, 30);
            continue;
        }
        if (g->hvy > 0 && feet_prev <= b->y + (int32_t)(BUG_H / 2) * FX) {
            b->active = 0;
            g->coins_run += 5;
            g->hud_dirty = 1;
            spawn_particles(g, b->x, b->y, 8, 0x4ADE80, 34);
            bounce(g, V_BUG_STOMP);
            cj_sfx(300, 30);
            cj_sfx(900, 40);
            continue;
        }
        return true;
    }
    return false;
}

/* --------------------------------------------------------------------------
 * The critter plays itself (CJ_AUTO=1 in the simulator)
 *
 * It aims at the nearest whole platform above. It is not an AI: it is a way of
 * leaving the game running for hours so the trails left by a badly recorded
 * dirty rectangle show up, which is what really has to be hunted in an engine
 * like this.
 * -------------------------------------------------------------------------- */
static void autoplay(cj_t *g)
{
    /* The key is that the target is chosen according to where it is going, and
     * not always "the one above": climbing it aims at the next one, falling it
     * aims at the one below it, which is the one it will really land on. With
     * the one above in both halves of the jump, the critter drifts off the
     * foothold precisely while falling and catches none of them. */
    int32_t feet  = g->hy + (int32_t)HERO_H * FX;
    int32_t best  = 0;
    int32_t bx    = g->hx;
    bool    found = false;

    for (int i = 0; i < MAX_PLATS; i++) {
        cj_plat_t *p = &g->plats[i];
        if (!p->active || p->fade || p->type == PLAT_FRAGILE) {
            continue;
        }
        if (g->hvy > 0) {
            /* falling: the highest of those below the feet */
            if (p->y < feet) {
                continue;
            }
            if (!found || p->y < best) {
                best = p->y;
                bx = p->x;
                found = true;
            }
        } else {
            /* climbing: the lowest of those above */
            if (p->y >= feet) {
                continue;
            }
            if (!found || p->y > best) {
                best = p->y;
                bx = p->x;
                found = true;
            }
        }
    }
    if (!found) {
        return;
    }
    g->target_x = bx + (int32_t)(PLAT_W - HERO_W) / 2 * FX;

    /* If there is a coin on the way up, it detours to fetch it. */
    for (int i = 0; i < MAX_COINS; i++) {
        cj_coin_t *c = &g->coins[i];
        if (c->active && g->hvy < 0 && c->y < g->hy && c->y > g->hy - 40 * FX) {
            g->target_x = c->x - (int32_t)(HERO_W / 2 - COIN_R) * FX;
            break;
        }
    }
}

/* --------------------------------------------------------------------------
 * One frame
 * -------------------------------------------------------------------------- */

void cj_step(cj_t *g)
{
    if (g->state != ST_PLAY && g->state != ST_DYING) {
        return;
    }

    if (g->autoplay && g->state == ST_PLAY) {
        autoplay(g);
    }

    int32_t feet_prev = g->hy + (int32_t)HERO_H * FX;

    /* ---- horizontal: the control says where, not how much ---------------- */
    if (g->state == ST_PLAY) {
        int32_t dx = g->target_x - g->hx;
        g->hvx = clamp32(dx / 2, -VX_MAX, VX_MAX);
        g->hx += g->hvx;
        g->hx  = clamp32(g->hx, 0, (int32_t)(CJ_W - HERO_W) * FX);
        g->facing = (uint8_t)(g->hvx < -16 ? 1 : (g->hvx > 16 ? 2 : 0));
    }

    /* ---- vertical -------------------------------------------------------- */
    if (g->rocket) {
        g->rocket--;
        g->hvy = V_ROCKET;
        if ((g->rocket & 3) == 0) {
            spawn_particles(g, g->hx + (HERO_W / 2) * FX,
                            g->hy + HERO_H * FX, 2, 0xFF9F0A, 18);
        }
        if (g->rocket == 0) {
            g->hvy = V_JUMP / 2;
        }
    } else {
        g->hvy += GRAV;
        if (g->hvy > V_FALL_MAX) {
            g->hvy = V_FALL_MAX;
        }
    }
    g->hy += g->hvy;

    if (g->squash) {
        g->squash--;
    }

    /* ---- moving platforms and fading ones -------------------------------- */
    for (int i = 0; i < MAX_PLATS; i++) {
        cj_plat_t *p = &g->plats[i];
        if (!p->active) {
            continue;
        }
        if (p->type == PLAT_MOVING) {
            p->x += p->vx;
            if (p->x < 2 * FX) {
                p->x = 2 * FX;
                p->vx = (int16_t)-p->vx;
            } else if (p->x > (int32_t)(CJ_W - PLAT_W - 2) * FX) {
                p->x = (int32_t)(CJ_W - PLAT_W - 2) * FX;
                p->vx = (int16_t)-p->vx;
            }
        }
        if (p->fade) {
            p->fade++;
            if (p->fade > 8) {
                p->active = 0;
            }
        }
        /* It drops out of the world: it is recycled. The rectangle it occupied
         * is already in d_prev, so the next frame's restore erases it by
         * itself. */
        if (p->y > g->cam_y + (int32_t)(CJ_H + 20) * FX) {
            p->active = 0;
        }
    }

    for (int i = 0; i < MAX_COINS; i++) {
        cj_coin_t *c = &g->coins[i];
        if (c->active) {
            c->phase = (uint8_t)((c->phase + 1) & 31);
            if (c->y > g->cam_y + (int32_t)(CJ_H + 20) * FX) {
                c->active = 0;
            }
        }
    }

    for (int i = 0; i < MAX_BUGS; i++) {
        cj_bug_t *b = &g->bugs[i];
        if (!b->active) {
            continue;
        }
        b->x += b->vx;
        if (b->x < 2 * FX || b->x > (int32_t)(CJ_W - BUG_W - 2) * FX) {
            b->vx = (int16_t)-b->vx;
            b->x = clamp32(b->x, 2 * FX, (int32_t)(CJ_W - BUG_W - 2) * FX);
        }
        b->anim = (uint8_t)((b->anim + 1) & 15);
        if (b->y > g->cam_y + (int32_t)(CJ_H + 20) * FX) {
            b->active = 0;
        }
    }

    for (int i = 0; i < MAX_PARTS; i++) {
        cj_part_t *p = &g->parts[i];
        if (!p->life) {
            continue;
        }
        p->x += p->vx;
        p->y += p->vy;
        p->vy = (int16_t)(p->vy + 6);
        p->life--;
    }

    /* ---- collisions ------------------------------------------------------ */
    if (g->state == ST_PLAY) {
        check_platforms(g, feet_prev);
        check_coins(g);
        if (check_bugs(g, feet_prev)) {
            g->state  = ST_DYING;
            g->hvy    = V_JUMP / 2;     /* a little hop and then the fall     */
            g->rocket = 0;
            cj_sfx(400, 60);
            cj_sfx(260, 80);
            cj_sfx(160, 140);
        }
    }

    /* ---- camera ---------------------------------------------------------- */
    if (g->state == ST_PLAY) {
        int32_t screen_y = g->hy - g->cam_y;
        if (screen_y < (int32_t)CAM_LINE * FX) {
            g->cam_y = g->hy - (int32_t)CAM_LINE * FX;
        }
        if (g->hy < g->top_y) {
            g->top_y = g->hy;
            uint32_t m = (uint32_t)((g->start_y - g->top_y) / (FX * 4));
            if (m != g->score) {
                g->score = m;
                g->hud_dirty = 1;
            }
            uint8_t z = zone_for(g->score);
            if (z != g->zone) {
                g->zone = z;
                g->zone_changed = 1;
                cj_sfx(880, 40);
                cj_sfx(1320, 60);
            }
        }
        generate(g);
    }

    /* ---- ending ---------------------------------------------------------- */
    if (g->state == ST_PLAY && g->hy > g->cam_y + (int32_t)CJ_H * FX) {
        g->state = ST_DYING;
        cj_sfx(300, 80);
        cj_sfx(180, 160);
    }
    if (g->state == ST_DYING &&
        g->hy > g->cam_y + (int32_t)(CJ_H + 40) * FX) {
        g->state = ST_OVER;
        if (g->score > g->hiscore) {
            g->hiscore = g->score;
            g->new_record = 1;
        }
    }
}
