/*
 * 2043 - THE BATTLE OF CERES
 *
 * A vertical shoot-'em-up for AmoledOS, a shameless homage to Capcom's 1943:
 * an energy bar that drains by itself, capsules dropped by a formation when it
 * falls whole, and a different boss at the end of each planet.
 *
 * It builds two ways from the same source:
 *
 *   .so for the board            cd apps/g2043 && idf.py -G 'Unix Makefiles' set-target esp32s3 && idf.py so
 *   built-in app of the simulator   the simulator builds it (AOS_SIM_BUILTIN)
 *
 * Controls: on opening it asks whether you play by dragging your finger or by
 * tilting the board. Firing is always the side button (BOOT), which the
 * runtime hands over to the app while it is flying.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include "g2043.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Settings
 * -------------------------------------------------------------------------- */

#define PLAYER_R        4           /* the player's hit box: deliberately small */
#define PLAYER_SPEED    58          /* 1/16 of a pixel per frame */
#define TOUCH_LIFT      13          /* the ship flies above the finger */
#define ROLL_FRAMES     22
#define ROLL_COOLDOWN   70
#define HIT_INVULN      26
#define RESPAWN_INVULN  70
#define DRAIN_EVERY     8           /* frames per unit of energy */
#define TILT_DEAD       40          /* milli-g of the sensor's dead zone */
#define HOLD_BOX_W      62          /* sensitive area of the score, in art pixels */
#define HOLD_MS        900          /* how long you have to hold to pause */
#define HOLD_SLOP        6          /* how far the finger may move without cancelling */
#define TILT_FULL      260          /* milli-g to reach the edge         */

#define KEY_HI          "g2043_hi"
#define KEY_CTRL        "g2043_ctrl"
#define KEY_AUTO        "g2043_auto"
#define KEY_AXES        "g2043_axes"
#define KEY_SFX         "g2043_sfx"
#define KEY_FPS         "g2043_fps"

typedef struct {
    g_t         g;
    uint16_t  *mem;
    uint16_t  *big;   /* upscaled x2, what the canvas draws */
    lv_obj_t   *canvas;
    lv_obj_t   *touch;
    lv_timer_t *timer;
    int16_t     period;         /* current period of the timer, in ms */
    int16_t     real_ms;        /* how long a frame really takes */
    int16_t     tune_t;         /* frames until the next adjustment  */
    int16_t     frames;         /* frames since the app opened       */
    uint64_t    prev_ms;        /* clock of the previous frame       */
    uint64_t    hold_ms;        /* when the sustained touch started */

    /* LVGL panels above the canvas */
    lv_obj_t   *title;
    lv_obj_t   *title_hi;
    lv_obj_t   *chip_auto;
    lv_obj_t   *chip_invx;
    lv_obj_t   *chip_invy;
    lv_obj_t   *chip_swap;
    lv_obj_t   *pause;
    lv_obj_t   *chip_sfx;
    lv_obj_t   *chip_fps;
    lv_obj_t   *over;
    lv_obj_t   *over_score;

    bool        want_exit;
    bool        want_pause;
} app_t;

/* --------------------------------------------------------------------------
 * Utilities
 * -------------------------------------------------------------------------- */

uint32_t g_rnd(g_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

int g_rnd_range(g_t *g, int lo, int hi)
{
    if (hi <= lo) {
        return lo;
    }
    return lo + (int)(g_rnd(g) % (uint32_t)(hi - lo + 1));
}

/* x2 upscaling by hand.
 *
 * Measured on the board: letting LVGL stretch the canvas costs 129 ms per
 * frame (7 fps). In lv_draw_sw_transform an RGB565 source also generates a
 * per-pixel alpha channel and composites with alpha blending instead of
 * copying. Upscaling here, the canvas ends up 1:1 and for LVGL it is a flat
 * copy. */
/* This pass already walks the small buffer's 41,216 pixels and writes the
 * large one's 165 thousand, so it is the place where the two things that used
 * to cost a whole screen each come for free:
 *
 *   - the screen shake, which was done by moving the canvas object. Moving an
 *     LVGL object invalidates the old area AND the new one: two screens of
 *     drawing, right in the explosions, which is when the frame is already at
 *     its most expensive. Here it is reading from a shifted origin and
 *     painting the uncovered border black.
 *   - the white flash, which was a full-screen gx_shade() (read, blend and
 *     write over all 41,216) for the five to eight frames following each big
 *     explosion. Here it is one more blend on a pixel we were about to write
 *     anyway.
 *
 * The offset is in art pixels, that is, two screen pixels at a time.
 *
 * It writes 32 bits at a time: with GX_SCALE=2 the destination is always
 * aligned to 4 (each row starts on a multiple of 736 pixels and each x lands
 * at 4*x bytes). That halves the writes on the path that goes to PSRAM. */
static void expand_gx(const uint16_t *src, uint16_t *dst, int sx, int sy, int flash)
{
    const int dw = GX_W * GX_SCALE;

    for (int y = 0; y < GX_H; y++) {
        uint16_t *row = dst + (size_t)y * GX_SCALE * dw;
        uint32_t *p32 = (uint32_t *)(void *)row;
        int src_y = y - sy;

        if (src_y < 0 || src_y >= GX_H) {
            memset(row, 0, (size_t)dw * sizeof(uint16_t));
        } else {
            const uint16_t *s = src + (size_t)src_y * GX_W;
            int x0 = sx > 0 ? sx : 0;
            int x1 = sx < 0 ? GX_W + sx : GX_W;

            for (int x = 0; x < x0; x++) {
                p32[x] = 0;
            }
            for (int x = x1; x < GX_W; x++) {
                p32[x] = 0;
            }
            if (flash > 0) {
                for (int x = x0; x < x1; x++) {
                    uint16_t c = gx_mix(s[x - sx], 0xFFFF, flash);
                    p32[x] = ((uint32_t)c << 16) | c;
                }
            } else {
                for (int x = x0; x < x1; x++) {
                    uint16_t c = s[x - sx];
                    p32[x] = ((uint32_t)c << 16) | c;
                }
            }
        }

        for (int k = 1; k < GX_SCALE; k++) {
            memcpy(row + (size_t)k * dw, row, (size_t)dw * sizeof(uint16_t));
        }
    }
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* --------------------------------------------------------------------------
 * Numbers to text, by hand
 *
 * The HUD is drawn on every frame and called snprintf four times, which in
 * newlib takes several hundred bytes of stack. And the stack is the LVGL
 * task's, the same one that afterwards draws the whole screen. Putting digits
 * into a buffer does not need that much.
 * -------------------------------------------------------------------------- */

static char *num_pad(char *dst, uint32_t v, int digits)
{
    for (int i = digits - 1; i >= 0; i--) {
        dst[i] = (char)('0' + v % 10);
        v /= 10;
    }
    dst[digits] = '\0';
    return dst + digits;
}

static char *num_str(char *dst, uint32_t v)
{
    char tmp[12];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < (int)sizeof(tmp));

    for (int i = 0; i < n; i++) {
        dst[i] = tmp[n - 1 - i];
    }
    dst[n] = '\0';
    return dst + n;
}

/* The beeps are limited to one every 45 ms: in the middle of a firefight there
 * would be hundreds a second, and in the simulator each one also prints a
 * line. */
static bool s_sfx = true;

void g_beep(int freq, int ms)
{
    static uint64_t last;
    if (!s_sfx) {
        return;
    }
    uint64_t now = aos_hal_uptime_ms();
    if (now - last < 45) {
        return;
    }
    last = now;
    aos_hal_beep(freq, ms);
}

void g_add_score(g_t *g, int points)
{
    g->score += (uint32_t)points;
    if (g->score > g->hiscore) {
        g->hiscore = g->score;
    }
}

int g_angle_to_player(const g_t *g, int x, int y)
{
    return gx_atan2(g->py - y, g->px - x);
}

g_shot_t *g_spawn_eshot(g_t *g, int x, int y, int vx, int vy, int kind)
{
    for (int i = 0; i < G_MAX_ESHOTS; i++) {
        g_shot_t *s = &g->eshots[i];
        if (s->alive) {
            continue;
        }
        s->x = (int16_t)x;
        s->y = (int16_t)y;
        s->vx = (int16_t)vx;
        s->vy = (int16_t)vy;
        s->kind = (uint8_t)kind;
        s->t = 0;
        s->alive = 1;
        return s;
    }
    return NULL;
}

void g_shoot_at_player(g_t *g, int x, int y, int speed, int kind)
{
    int ang = g_angle_to_player(g, x, y);
    g_spawn_eshot(g, x, y, gx_cos(ang) * speed / 256, gx_sin(ang) * speed / 256, kind);
    g_beep(260, 20);
}

g_fx_t *g_spawn_fx(g_t *g, int x, int y, int vx, int vy, int kind,
                   uint16_t color, int life)
{
    g_fx_t *slot = NULL;
    int oldest = -1;

    for (int i = 0; i < G_MAX_FX; i++) {
        if (!g->fx[i].alive) {
            slot = &g->fx[i];
            break;
        }
        /* if there is no room, the oldest is overwritten: better to lose a
         * spark than to stop drawing the new explosion */
        if (g->fx[i].t > oldest) {
            oldest = g->fx[i].t;
            slot = &g->fx[i];
        }
    }
    if (!slot) {
        return NULL;
    }

    slot->x = (int16_t)x;
    slot->y = (int16_t)y;
    slot->vx = (int16_t)vx;
    slot->vy = (int16_t)vy;
    slot->kind = (uint8_t)kind;
    slot->color = color;
    slot->t = 0;
    slot->life = (int16_t)life;
    slot->text = NULL;
    slot->alive = 1;
    return slot;
}

void g_boom(g_t *g, int x, int y, int radius, uint16_t color)
{
    g_fx_t *f = g_spawn_fx(g, FX(x), FX(y), 0, 0, FX_BOOM, color, radius);
    if (f) {
        f->life = (int16_t)radius;
    }
    g_spawn_fx(g, FX(x), FX(y), 0, 0, FX_RING, color, radius + 6);

    int n = radius / 3 + 2;
    for (int i = 0; i < n; i++) {
        int ang = (int)(g_rnd(g) % 256);
        int sp = g_rnd_range(g, 8, 26);
        g_spawn_fx(g, FX(x), FX(y), gx_cos(ang) * sp / 256, gx_sin(ang) * sp / 256,
                   FX_DEBRIS, color, g_rnd_range(g, 8, 18));
    }
}

void g_drop_pickup(g_t *g, int x, int y, int kind)
{
    for (int i = 0; i < G_MAX_PICKUPS; i++) {
        g_pickup_t *p = &g->pickups[i];
        if (p->alive) {
            continue;
        }
        p->x = (int16_t)x;
        p->y = (int16_t)y;
        p->vy = 10;
        p->kind = (uint8_t)kind;
        p->t = 0;
        p->alive = 1;
        return;
    }
}

/* --------------------------------------------------------------------------
 * Capsules
 * -------------------------------------------------------------------------- */

static const uint32_t pickup_color[PU_COUNT] = {
    0x4A9DF5,   /* PU_TWIN   */
    0xFF9F0A,   /* PU_SPREAD */
    0xFF2D55,   /* PU_LASER  */
    0xB072F0,   /* PU_WAVE   */
    0x30D158,   /* PU_ENERGY */
    0x7BE9FF,   /* PU_SHIELD */
    0xFFE45E,   /* PU_LIFE   */
};

static const char pickup_letter[PU_COUNT] = { 'D', 'T', 'L', 'O', 'E', 'S', '1' };

/* Note: this is drawn with gx_text(), the game's own 5x7 font, which is ASCII
 * 0x20-0x5F and UPPER CASE ONLY -it silently discards any other byte- and
 * gx_text_w() measures with strlen, that is, in bytes. The translations of
 * everything that goes to the canvas have to stay upper case without
 * accents. */
static const char *const pickup_name[PU_COUNT] = {
    N_("DOBLE"), N_("TRIPLE"), N_("LASER"), N_("ONDA"), N_("ENERGIA"),
    N_("ESCUDO"), N_("NAVE EXTRA"),
};

static void pickup_take(g_t *g, g_pickup_t *p)
{
    static const uint8_t as_weapon[PU_COUNT] = {
        W_TWIN, W_SPREAD, W_LASER, W_WAVE, 0xFF, 0xFF, 0xFF,
    };
    uint8_t w = as_weapon[p->kind];

    if (w != 0xFF) {
        if (g->weapon == w) {
            if (g->wlevel < 3) {
                g->wlevel++;
            } else {
                g_add_score(g, 2000);
            }
        } else {
            g->weapon = w;
            g->wlevel = 1;
        }
    } else if (p->kind == PU_ENERGY) {
        g->energy = (int16_t)clampi(g->energy + 90, 0, G_ENERGY_MAX);
    } else if (p->kind == PU_SHIELD) {
        g->shield = 380;
    } else {
        if (g->lives < 6) {
            g->lives++;
        } else {
            g_add_score(g, 5000);
        }
    }

    g_fx_t *f = g_spawn_fx(g, p->x, p->y - FX(6), 0, -6, FX_TEXT,
                           gx_rgb(pickup_color[p->kind]), 34);
    if (f) {
        f->text = _(pickup_name[p->kind]);
    }
    g_add_score(g, 500);
    aos_hal_beep(1400, 40);
    p->alive = 0;
}

static void pickup_draw(g_t *g, const g_pickup_t *p)
{
    int x = UNFX(p->x), y = UNFX(p->y);
    uint16_t col = gx_rgb(pickup_color[p->kind]);
    /* it pulses so it can be seen through the gunfire */
    int f = 6 + gx_sin(p->t * 9) / 42;

    gx_glow(&g->buf, x, y, 9, col, f);
    gx_round(&g->buf, x - 6, y - 6, 13, 13, 3, gx_rgb(0x0A0A12));
    gx_round(&g->buf, x - 5, y - 5, 11, 11, 2, col);
    gx_round(&g->buf, x - 4, y - 4, 9, 9, 2, gx_rgb(0x0A0A12));

    char s[2] = { pickup_letter[p->kind], 0 };
    gx_text(&g->buf, x - 2, y - 3, s, col);
}

/* --------------------------------------------------------------------------
 * Waves
 * -------------------------------------------------------------------------- */

static g_enemy_t *enemy_spawn(g_t *g, int kind, int x, int y, int wave)
{
    for (int i = 0; i < G_MAX_ENEMIES; i++) {
        g_enemy_t *e = &g->enemies[i];
        if (e->alive) {
            continue;
        }
        memset(e, 0, sizeof(*e));
        e->kind  = (uint8_t)kind;
        e->x     = (int16_t)x;
        e->y     = (int16_t)y;
        e->hp    = (int16_t)gx_enemy_hp(kind);
        e->wave  = (uint8_t)wave;
        e->a     = (int16_t)(g_rnd(g) % 256);
        e->vy    = 34;
        e->alive = 1;
        return e;
    }
    return NULL;
}

static void wave_spawn(g_t *g, const g_wave_t *w, int id)
{
    int base = w->x * GX_W / 100;
    int n = w->count;

    g->wave_left[id] = 0;
    g->wave_gift[id] = w->gift;

    for (int i = 0; i < n; i++) {
        int x = base, y = -12;
        int vx = 0, vy = 34;

        switch (w->form) {
        case FORM_COLUMN:
            y = -12 - i * 17;
            break;
        case FORM_ROW:
            x = base - (n - 1) * 8 + i * 16;
            break;
        case FORM_V:
            x = base + (i - (n - 1) / 2) * 15;
            y = -12 - (i > (n - 1) / 2 ? i - (n - 1) / 2 : (n - 1) / 2 - i) * 11;
            break;
        case FORM_SIDE_L:
            x = -12 - i * 18;
            y = 26 + i * 5;
            vx = 44;
            vy = 7;
            break;
        case FORM_SIDE_R:
            x = GX_W + 12 + i * 18;
            y = 26 + i * 5;
            vx = -44;
            vy = 7;
            break;
        default:    /* FORM_SPREAD */
            x = 18 + (GX_W - 36) * i / (n > 1 ? n - 1 : 1);
            y = -12 - (int)(g_rnd(g) % 26);
            break;
        }

        if (w->kind == EN_TURRET) {
            /* the turret moves down with the terrain, it does not fly */
            vy = (int16_t)gx_levels[g->level].scroll;
            vx = 0;
        }

        g_enemy_t *e = enemy_spawn(g, w->kind, FX(clampi(x, -30, GX_W + 30)),
                                   (int16_t)(y * FX_ONE), id);
        if (!e) {
            break;
        }
        e->vx = (int16_t)vx;
        e->vy = (int16_t)vy;
        g->wave_left[id]++;
    }
}

static void waves_update(g_t *g)
{
    const g_level_t *lv = &gx_levels[g->level];
#ifdef AOS_SIM_BUILTIN
    if (getenv("G2043_TRACE") && (g->level_t % 30) == 0) {
        int n = 0;
        for (int i = 0; i < G_MAX_ENEMIES; i++) if (g->enemies[i].alive) n++;
        printf("[2043] t=%d estado=%d oleada=%d/%d vivos=%d\n",
               (int)g->level_t, g->state, g->wave_next, lv->wave_count, n);
    }
#endif

    while (g->wave_next < lv->wave_count &&
           lv->waves[g->wave_next].at <= g->level_t) {
        wave_spawn(g, &lv->waves[g->wave_next], (int)g->wave_next + 1);
        g->wave_next++;
    }
}

/* --------------------------------------------------------------------------
 * The player's shots
 * -------------------------------------------------------------------------- */

static g_shot_t *pshot(g_t *g, int x, int y, int vx, int vy, int kind)
{
    for (int i = 0; i < G_MAX_PSHOTS; i++) {
        g_shot_t *s = &g->pshots[i];
        if (s->alive) {
            continue;
        }
        s->x = (int16_t)x;
        s->y = (int16_t)y;
        s->vx = (int16_t)vx;
        s->vy = (int16_t)vy;
        s->kind = (uint8_t)kind;
        s->t = 0;
        s->alive = 1;
        return s;
    }
    return NULL;
}

static int pshot_damage(const g_t *g, int kind)
{
    switch (kind) {
    case PS_LASER: return 2 + g->wlevel / 2;
    case PS_WAVE:  return 3 + g->wlevel;
    default:       return 1;
    }
}

static int pshot_radius(int kind)
{
    switch (kind) {
    case PS_LASER: return 2;
    case PS_WAVE:  return 7;
    default:       return 2;
    }
}

static void player_fire(g_t *g)
{
    int x = g->px, y = g->py - FX(7);
    int lv = g->wlevel;

    switch (g->weapon) {
    case W_TWIN:
        for (int i = -1; i <= 1; i += 2) {
            pshot(g, x + i * FX(3), y, 0, -66, PS_BULLET);
            pshot(g, x + i * FX(8), y + FX(3), i * 3, -62, PS_BULLET);
        }
        g->fire_cd = (int16_t)(9 - lv);
        break;

    case W_SPREAD: {
        int n = lv >= 3 ? 5 : 3;
        for (int i = 0; i < n; i++) {
            int ang = 192 + (i - (n - 1) / 2) * 11;      /* 192 brads = up */
            pshot(g, x, y, gx_cos(ang) * 56 / 256, gx_sin(ang) * 56 / 256, PS_BULLET);
        }
        g->fire_cd = (int16_t)(12 - lv);
        break;
    }

    case W_LASER:
        pshot(g, x, y, 0, -118, PS_LASER);
        if (lv >= 2) {
            pshot(g, x - FX(6), y + FX(4), 0, -118, PS_LASER);
            pshot(g, x + FX(6), y + FX(4), 0, -118, PS_LASER);
        }
        g->fire_cd = (int16_t)(9 - lv);
        break;

    case W_WAVE:
        pshot(g, x, y, 0, -40, PS_WAVE);
        if (lv >= 3) {
            pshot(g, x - FX(10), y + FX(6), -4, -38, PS_WAVE);
            pshot(g, x + FX(10), y + FX(6), 4, -38, PS_WAVE);
        }
        g->fire_cd = (int16_t)(20 - lv * 2);
        break;

    default:    /* W_SHOT */
        pshot(g, x - FX(4), y, 0, -64, PS_BULLET);
        pshot(g, x + FX(4), y, 0, -64, PS_BULLET);
        if (lv >= 3) {
            pshot(g, x - FX(9), y + FX(4), -5, -60, PS_BULLET);
            pshot(g, x + FX(9), y + FX(4), 5, -60, PS_BULLET);
        }
        g->fire_cd = (int16_t)(10 - lv * 2);
        break;
    }

    g_beep(1500 + g->weapon * 120, 10);
}

/* --------------------------------------------------------------------------
 * The player
 * -------------------------------------------------------------------------- */

static void player_reset(g_t *g, bool full)
{
    g->px = FX(GX_W / 2);
    g->py = FX(G_PLAY_Y1 - 34);
    g->pvx = g->pvy = 0;
    g->energy = G_ENERGY_MAX;
    g->invuln = RESPAWN_INVULN;
    g->roll = 0;
    g->roll_cd = 0;
    g->shield = 0;
    if (full) {
        g->weapon = W_SHOT;
        g->wlevel = 1;
    }
}

static void player_move(g_t *g)
{
    /* Both controls end in the same thing: a point the ship is drawn towards.
     * With the finger it is where the finger is; with the sensor, the tilt
     * mapped to an absolute position.
     *
     * Absolute and not a velocity on purpose: with a velocity, a badly
     * calibrated zero leaves the ship drifting into the edge on its own until
     * you correct for it. This way, a level board is always the centre. */
    int tx = g->px, ty = g->py;

    if (g->control == CTRL_TILT) {
        /* The IMU hangs off the same I2C bus as the touch panel, and on this
         * board polling that bus in parallel with the touch driver has already
         * saturated CPU 0 once (it is written down in DECISIONES). At ten
         * reads a second the ship responds just as well and the bus is left
         * alone. */
        if (g->imu_skip == 0) {
            aos_imu_t imu;
            g->imu_skip = 3;
            if (aos_hal_imu_read(&imu)) {
                if (!g->tilt_zeroed) {
                    g->tilt_zero_x = imu.ax;
                    g->tilt_zero_y = imu.ay;
                    g->tilt_zeroed = 1;
                }
                /* in milli-g, the only thing we ask floating point for */
                g->tilt_mx = (int16_t)clampi(
                    (int)((imu.ax - g->tilt_zero_x) * 1000.0f), -2000, 2000);
                g->tilt_my = (int16_t)clampi(
                    (int)((imu.ay - g->tilt_zero_y) * 1000.0f), -2000, 2000);
            }
        }
        g->imu_skip--;

        {
            int mx = g->tilt_mx;
            int my = g->tilt_my;

            if (g->swap_axes) {
                int t = mx; mx = my; my = t;
            }
            if (g->inv_x) mx = -mx;
            if (g->inv_y) my = -my;

            /* dead zone: without it the ship never quite holds still */
            mx = (mx > TILT_DEAD) ? mx - TILT_DEAD
               : (mx < -TILT_DEAD) ? mx + TILT_DEAD : 0;
            my = (my > TILT_DEAD) ? my - TILT_DEAD
               : (my < -TILT_DEAD) ? my + TILT_DEAD : 0;

            /* multiply before dividing: the other way round, the scale comes
             * out in whole sixteenths and the step shows */
            tx = FX(GX_W / 2) +
                 clampi(mx, -TILT_FULL, TILT_FULL) * FX(GX_W / 2 - 10) / TILT_FULL;
            /* the vertical zero is not the centre but the lower part: with the
             * board level the ship has to end up where the game is played */
            ty = FX(G_PLAY_Y1 - 58) +
                 clampi(my, -TILT_FULL, TILT_FULL) * FX(95) / TILT_FULL;
        }
    } else if (g->touching) {
        /* the finger covers the ship, so the ship flies a little higher */
        tx = g->touch_x;
        ty = g->touch_y - FX(TOUCH_LIFT);
    }

    int tvx = clampi((tx - g->px) / 2, -PLAYER_SPEED, PLAYER_SPEED);
    int tvy = clampi((ty - g->py) / 2, -PLAYER_SPEED, PLAYER_SPEED);

    /* smoothing: the ship has some inertia and does not teleport */
    g->pvx += (int16_t)((tvx - g->pvx) / 2);
    g->pvy += (int16_t)((tvy - g->pvy) / 2);

    g->px = (int16_t)(g->px + g->pvx);
    g->py = (int16_t)(g->py + g->pvy);

    g->px = (int16_t)clampi(g->px, FX(9), FX(GX_W - 9));
    g->py = (int16_t)clampi(g->py, FX(G_PLAY_Y0 + 9), FX(G_PLAY_Y1 - 7));
}

static void player_die(g_t *g)
{
    g_boom(g, UNFX(g->px), UNFX(g->py), 18, gx_rgb(0x7BE9FF));
    g->shake = 12;
    g->flash_screen = 5;
    aos_hal_beep(90, 200);

    g->lives--;
    g->state = ST_DEAD;
    g->state_t = 0;
}

static void player_hurt(g_t *g, int amount)
{
    if (g->invuln > 0 || g->roll > 0) {
        return;
    }
    if (g->shield > 0) {
        g->shield = 0;
        g->invuln = HIT_INVULN;
        g_boom(g, UNFX(g->px), UNFX(g->py), 10, gx_rgb(0x7BE9FF));
        aos_hal_beep(700, 60);
        return;
    }

    g->energy = (int16_t)(g->energy - amount);
    g->invuln = HIT_INVULN;
    g->shake = 6;
    aos_hal_beep(200, 70);

    if (g->energy <= 0) {
        g->energy = 0;
        player_die(g);
    }
}

static void player_draw(g_t *g)
{
    int x = UNFX(g->px), y = UNFX(g->py);

    if (g->state == ST_DEAD) {
        return;
    }
    /* it blinks while invulnerable, except mid-barrel-roll */
    if (g->invuln > 0 && !g->roll && (g->invuln / 3) % 2) {
        return;
    }

    /* engine flame: two tongues that flicker */
    int flame = 4 + (int)(g_rnd(g) % 3);
    for (int i = -1; i <= 1; i += 2) {
        gx_glow(&g->buf, x + i * 4, y + 8, flame, gx_rgb(0xFF9F0A), 12);
        gx_vline(&g->buf, x + i * 4, y + 7, flame, gx_rgb(0xFFE45E));
    }

    if (g->roll > 0) {
        /* barrel roll: the ship spins about its axis, that is, it is seen to
         * narrow */
        int phase = (ROLL_FRAMES - g->roll) * 256 / ROLL_FRAMES;
        int w = 15 * (gx_cos(phase) < 0 ? -gx_cos(phase) : gx_cos(phase)) / 256;
        gx_glow(&g->buf, x, y, 12, gx_rgb(0x7BE9FF), 7);
        gx_blit_c_xscale(&g->buf, x, y, gx_art_ship.rows, gx_art_ship.n,
                         w < 2 ? 2 : w);
    } else {
        gx_blit_c(&g->buf, x, y, gx_art_ship.rows, gx_art_ship.n);
    }

    if (g->shield > 0 && (g->shield > 90 || (g->shield / 4) % 2)) {
        int r = 13 + gx_sin(g->state_t * 8) / 90;
        gx_ring(&g->buf, x, y, r, gx_rgb(0x7BE9FF));
        gx_glow(&g->buf, x, y, r, gx_rgb(0x7BE9FF), 4);
    }
}

/* --------------------------------------------------------------------------
 * Shots and effects: advancing and drawing
 * -------------------------------------------------------------------------- */

static void shots_update(g_t *g)
{
    for (int i = 0; i < G_MAX_PSHOTS; i++) {
        g_shot_t *s = &g->pshots[i];
        if (!s->alive) {
            continue;
        }
        s->x = (int16_t)(s->x + s->vx);
        s->y = (int16_t)(s->y + s->vy);
        s->t++;
        if (UNFX(s->y) < G_PLAY_Y0 - 8 || UNFX(s->x) < -6 || UNFX(s->x) > GX_W + 6) {
            s->alive = 0;
        }
    }

    for (int i = 0; i < G_MAX_ESHOTS; i++) {
        g_shot_t *s = &g->eshots[i];
        if (!s->alive) {
            continue;
        }
        if (s->kind == ES_HOMING && s->t < 60) {
            /* it corrects course a little at a time: you have to move, dodging
             * is not enough */
            int ang = g_angle_to_player(g, s->x, s->y);
            s->vx = (int16_t)(s->vx + (gx_cos(ang) * 22 / 256 - s->vx) / 12);
            s->vy = (int16_t)(s->vy + (gx_sin(ang) * 22 / 256 - s->vy) / 12);
        }
        s->x = (int16_t)(s->x + s->vx);
        s->y = (int16_t)(s->y + s->vy);
        s->t++;

        int x = UNFX(s->x), y = UNFX(s->y);
        if (y > G_PLAY_Y1 + 10 || y < G_PLAY_Y0 - 14 || x < -10 || x > GX_W + 10) {
            s->alive = 0;
        }
    }
}

static void shots_draw(g_t *g)
{
    for (int i = 0; i < G_MAX_PSHOTS; i++) {
        const g_shot_t *s = &g->pshots[i];
        if (!s->alive) {
            continue;
        }
        int x = UNFX(s->x), y = UNFX(s->y);

        switch (s->kind) {
        case PS_LASER:
            gx_vline(&g->buf, x, y - 5, 11, gx_rgb(0xFF2D55));
            gx_vline(&g->buf, x - 1, y - 3, 7, gx_rgb(0xFF6FAE));
            gx_vline(&g->buf, x + 1, y - 3, 7, gx_rgb(0xFF6FAE));
            gx_glow(&g->buf, x, y, 4, gx_rgb(0xFF2D55), 8);
            break;
        case PS_WAVE: {
            int r = 5 + (s->t / 4 > 3 ? 3 : s->t / 4);
            gx_glow(&g->buf, x, y, r + 3, gx_rgb(0xB072F0), 10);
            gx_ring(&g->buf, x, y, r, gx_rgb(0xE0C0FF));
            gx_ring(&g->buf, x, y, r - 2 > 0 ? r - 2 : 1, gx_rgb(0xB072F0));
            break;
        }
        default:
            if (g->detail) {
                gx_glow(&g->buf, x, y, 3, gx_rgb(0x7BE9FF), 8);
            }
            gx_rect(&g->buf, x - 1, y - 3, 3, 6, gx_rgb(0xFFFFFF));
            gx_px(&g->buf, x, y - 4, gx_rgb(0x7BE9FF));
            break;
        }
    }

    for (int i = 0; i < G_MAX_ESHOTS; i++) {
        const g_shot_t *s = &g->eshots[i];
        if (!s->alive) {
            continue;
        }
        int x = UNFX(s->x), y = UNFX(s->y);

        switch (s->kind) {
        case ES_HEAVY:
            gx_glow(&g->buf, x, y, 6, gx_rgb(0xFF4A3D), 11);
            gx_disc(&g->buf, x, y, 3, gx_rgb(0xFFE45E));
            gx_ring(&g->buf, x, y, 3, gx_rgb(0xFF4A3D));
            break;
        case ES_ROCK: {
            const gx_sprite_t *sp = &gx_art_rock[2];
            gx_blit_c(&g->buf, x, y, sp->rows, sp->n);
            break;
        }
        case ES_HOMING:
            gx_glow(&g->buf, x, y, 5, gx_rgb(0x2AF0C8), 12);
            gx_disc(&g->buf, x, y, 2, gx_rgb(0xFFFFFF));
            gx_ring(&g->buf, x, y, 3, gx_rgb(0x2AF0C8));
            break;
        default:
            if (g->detail) {
                gx_glow(&g->buf, x, y, 4, gx_rgb(0xFF9F0A), 10);
            }
            gx_disc(&g->buf, x, y, 2, gx_rgb(0xFFE45E));
            gx_ring(&g->buf, x, y, 2, gx_rgb(0xFF9F0A));
            break;
        }
    }
}

static void fx_update(g_t *g)
{
    for (int i = 0; i < G_MAX_FX; i++) {
        g_fx_t *f = &g->fx[i];
        if (!f->alive) {
            continue;
        }
        f->t++;
        f->x = (int16_t)(f->x + f->vx);
        f->y = (int16_t)(f->y + f->vy);

        switch (f->kind) {
        case FX_DEBRIS:
            f->vy = (int16_t)(f->vy + 1);       /* they are a little heavy */
            f->vx = (int16_t)(f->vx * 15 / 16);
            if (f->t > f->life) f->alive = 0;
            break;
        case FX_BOOM:
            if (f->t > f->life / 2 + 6) f->alive = 0;
            break;
        case FX_RING:
            if (f->t > 10) f->alive = 0;
            break;
        default:
            if (f->t > f->life) f->alive = 0;
            break;
        }
    }
}

static void fx_draw(g_t *g)
{
    for (int i = 0; i < G_MAX_FX; i++) {
        const g_fx_t *f = &g->fx[i];
        if (!f->alive) {
            continue;
        }
        int x = UNFX(f->x), y = UNFX(f->y);

        switch (f->kind) {
        case FX_BOOM: {
            int r = f->life * f->t / (f->life / 2 + 6);
            if (r < 1) r = 1;
            gx_glow(&g->buf, x, y, r + 4, f->color, 14);
            gx_disc(&g->buf, x, y, r / 2 + 1, gx_rgb(0xFFFFFF));
            gx_ring(&g->buf, x, y, r, f->color);
            break;
        }
        case FX_RING: {
            int r = f->life * f->t / 10;
            gx_ring(&g->buf, x, y, r, gx_mix(f->color, gx_rgb(0xFFFFFF), 8));
            break;
        }
        case FX_TEXT:
            if (f->text) {
                gx_text_center(&g->buf, x, y, f->text, f->color, gx_rgb(0x0A0A12));
            }
            break;
        default:
            gx_px(&g->buf, x, y, gx_rgb(0xFFFFFF));
            gx_px(&g->buf, x + 1, y, f->color);
            gx_px(&g->buf, x, y + 1, f->color);
            break;
        }
    }
}

/* --------------------------------------------------------------------------
 * Collisions
 * -------------------------------------------------------------------------- */

static bool near(int ax, int ay, int bx, int by, int r)
{
    int dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy <= r * r;
}

static void enemy_kill(g_t *g, g_enemy_t *e)
{
    gx_enemy_died(g, e);
    g_add_score(g, gx_enemy_score(e->kind));

    int id = e->wave;
    e->alive = 0;

    if (id && g->wave_left[id] > 0 && --g->wave_left[id] == 0 && g->wave_gift[id]) {
        /* the formation fell whole: the capsule drops, as in 1943 */
        g_drop_pickup(g, e->x, e->y, g->wave_gift[id] - 1);
        g->wave_gift[id] = 0;
    }
}

static void collisions(g_t *g)
{
    /* the player's shots against enemies and the boss */
    for (int i = 0; i < G_MAX_PSHOTS; i++) {
        g_shot_t *s = &g->pshots[i];
        if (!s->alive) {
            continue;
        }
        int sx = UNFX(s->x), sy = UNFX(s->y);
        int sr = pshot_radius(s->kind);
        int dmg = pshot_damage(g, s->kind);
        bool pierce = (s->kind == PS_LASER || s->kind == PS_WAVE);

        for (int j = 0; j < G_MAX_ENEMIES; j++) {
            g_enemy_t *e = &g->enemies[j];
            if (!e->alive) {
                continue;
            }
            if (!near(sx, sy, UNFX(e->x), UNFX(e->y), sr + gx_enemy_radius(e->kind))) {
                continue;
            }
            e->hp = (int16_t)(e->hp - dmg);
            e->flash = 2;
            if (e->hp <= 0) {
                enemy_kill(g, e);
            } else {
                g_spawn_fx(g, s->x, s->y, 0, 0, FX_SPARK, gx_rgb(0xFFE45E), 4);
            }
            if (!pierce) {
                s->alive = 0;
                break;
            }
        }
        if (!s->alive) {
            continue;
        }
        if (gx_boss_hit(g, sx, sy, dmg) && !pierce) {
            s->alive = 0;
        }
    }

    if (g->state == ST_DEAD) {
        return;
    }

    int px = UNFX(g->px), py = UNFX(g->py);

    /* enemy shots against the player */
    for (int i = 0; i < G_MAX_ESHOTS; i++) {
        g_shot_t *s = &g->eshots[i];
        if (!s->alive) {
            continue;
        }
        if (near(UNFX(s->x), UNFX(s->y), px, py, PLAYER_R + 3)) {
            s->alive = 0;
            player_hurt(g, 26);
            if (g->state == ST_DEAD) {
                return;
            }
        }
    }

    /* collision with enemies */
    for (int i = 0; i < G_MAX_ENEMIES; i++) {
        g_enemy_t *e = &g->enemies[i];
        if (!e->alive) {
            continue;
        }
        if (!near(UNFX(e->x), UNFX(e->y), px, py, PLAYER_R + gx_enemy_radius(e->kind))) {
            continue;
        }
        if (g->invuln <= 0 && g->roll <= 0 && g->shield <= 0) {
            e->hp = (int16_t)(e->hp - 3);
            if (e->hp <= 0) {
                enemy_kill(g, e);
            }
        }
        player_hurt(g, 42);
        if (g->state == ST_DEAD) {
            return;
        }
    }

    /* collision with the boss */
    if (g->boss.alive && !g->boss.dying) {
        const gx_boss_def_t *d = &gx_bosses[g->boss.def];
        for (int part = 0; part <= g->boss.parts; part++) {
            int bx, by, br;
            d->hitbox(&g->boss, part, &bx, &by, &br);
            if (br > 0 && near(bx, by, px, py, PLAYER_R + br)) {
                player_hurt(g, 46);
                break;
            }
        }
    }

    /* capsules */
    for (int i = 0; i < G_MAX_PICKUPS; i++) {
        g_pickup_t *p = &g->pickups[i];
        if (p->alive && near(UNFX(p->x), UNFX(p->y), px, py, 11)) {
            pickup_take(g, p);
        }
    }
}

/* --------------------------------------------------------------------------
 * HUD
 * -------------------------------------------------------------------------- */

static void hud_draw(g_t *g)
{
    gx_buf_t *b = &g->buf;
    char buf[32];

    /* bands: darken instead of covering, so the background stays alive behind */
    gx_shade(b, 0, 0, GX_W, G_HUD_H, -11);
    gx_hline(b, 0, G_HUD_H, GX_W, gx_rgb(0x2A3145));
    gx_shade(b, 0, GX_H - G_FOOT_H, GX_W, G_FOOT_H, -11);
    gx_hline(b, 0, GX_H - G_FOOT_H - 1, GX_W, gx_rgb(0x2A3145));

    /* Holding your finger here pauses the game: meanwhile, the score fills up
     * so it is clear you have to keep pressing. */
    if (g->hold_pct > 0) {
        int w = HOLD_BOX_W * g->hold_pct / 100;
        gx_rect(b, 0, 0, w, G_HUD_H, gx_rgb(0x1F4FBF));
        gx_vline(b, w, 0, G_HUD_H, gx_rgb(0x7BE9FF));
    }

    /* the score wraps at 999999, as it should */
    num_pad(buf, g->score % 1000000u, 6);
    gx_text_sh(b, 3, 3, buf, gx_rgb(0xFFFFFF), gx_rgb(0x090B14));

    buf[0] = 'H'; buf[1] = 'I'; buf[2] = ' ';
    num_pad(buf + 3, g->hiscore % 1000000u, 6);
    gx_text_sh(b, GX_W / 2 - gx_text_w(buf) / 2 - 6, 3, buf,
               gx_rgb(0xFFE45E), gx_rgb(0x090B14));

    for (int i = 0; i < g->lives - 1 && i < 5; i++) {
        gx_blit(b, GX_W - 10 - i * 9, 3, gx_art_ship_icon.rows, gx_art_ship_icon.n);
    }

    if (g->show_fps) {
        char *e = num_str(buf, (uint32_t)(g->fps10 / 10));
        *e++ = '.';
        *e++ = (char)('0' + g->fps10 % 10);
        *e++ = ' '; *e++ = 'F'; *e++ = 'P'; *e++ = 'S'; *e = '\0';
        gx_text_sh(b, 3, G_HUD_H + 3, buf, gx_rgb(0x2AF0C8), gx_rgb(0x090B14));
    }

    /* energy bar: it drains by itself, it is the level's clock */
    int y = GX_H - G_FOOT_H + 3;
    gx_text_sh(b, 3, y + 1, "E", gx_rgb(0x8E8E93), gx_rgb(0x090B14));

    int bw = 108;
    int fill = g->energy * bw / G_ENERGY_MAX;
    uint16_t col = g->energy > G_ENERGY_MAX / 2 ? gx_rgb(0x30D158)
                 : g->energy > G_ENERGY_MAX / 5 ? gx_rgb(0xFFE45E)
                                                : gx_rgb(0xFF4A3D);
    gx_rect(b, 12, y, bw, 9, gx_rgb(0x161B2B));
    gx_rect(b, 12, y, fill, 9, col);
    gx_frame(b, 12, y, bw, 9, gx_rgb(0x606B85));
    if (g->energy < G_ENERGY_MAX / 5 && (g->state_t / 6) % 2) {
        gx_frame(b, 11, y - 1, bw + 2, 11, gx_rgb(0xFF4A3D));
    }

    /* weapon and planet */
    static const char weapon_letter[W_COUNT] = { 'N', 'D', 'T', 'L', 'O' };
    buf[0] = weapon_letter[g->weapon % W_COUNT];
    buf[1] = (char)('0' + (g->wlevel > 9 ? 9 : g->wlevel));
    buf[2] = '\0';
    gx_text_sh(b, 126, y + 1, buf, gx_rgb(0x7BE9FF), gx_rgb(0x090B14));

    buf[0] = 'P';
    buf[1] = (char)('0' + g->level + 1);
    buf[2] = '\0';
    gx_text_sh(b, GX_W - 16, y + 1, buf, gx_rgb(0xB072F0), gx_rgb(0x090B14));

    /* the boss's bar */
    if (g->boss.alive) {
        int w = GX_W - 40;
        int f = g->boss.hp * w / (g->boss.hp_max > 0 ? g->boss.hp_max : 1);
        gx_rect(b, 20, G_HUD_H + 3, w, 5, gx_rgb(0x161B2B));
        gx_rect(b, 20, G_HUD_H + 3, f, 5, gx_rgb(0xFF4A3D));
        gx_frame(b, 20, G_HUD_H + 3, w, 5, gx_rgb(0xFF9F0A));
        /* the name only during the introduction: afterwards it covers the boss */
        if (g->boss.t < 130) {
            gx_text_center(b, GX_W / 2, G_HUD_H + 10, _(gx_bosses[g->boss.def].name),
                           gx_rgb(0xFF9F0A), gx_rgb(0x090B14));
        }
    }
}

static void banner(g_t *g, const char *big, const char *small, int y)
{
    gx_shade(&g->buf, 0, y - 6, GX_W, small ? 30 : 18, -9);
    gx_text_center(&g->buf, GX_W / 2, y, big, gx_rgb(0xFFFFFF), gx_rgb(0x090B14));
    if (small) {
        gx_text_center(&g->buf, GX_W / 2, y + 12, small, gx_rgb(0xFFE45E),
                       gx_rgb(0x090B14));
    }
}

/* --------------------------------------------------------------------------
 * State machine
 * -------------------------------------------------------------------------- */

static void level_start(g_t *g, int level)
{
    g->level = (uint8_t)clampi(level, 0, gx_level_count - 1);
    g->level_t = 0;
    g->wave_next = 0;
    g->state = ST_READY;
    g->state_t = 0;

    memset(g->enemies, 0, sizeof(g->enemies));
    memset(g->pshots, 0, sizeof(g->pshots));
    memset(g->eshots, 0, sizeof(g->eshots));
    memset(g->pickups, 0, sizeof(g->pickups));
    memset(g->fx, 0, sizeof(g->fx));
    memset(g->wave_left, 0, sizeof(g->wave_left));
    memset(g->wave_gift, 0, sizeof(g->wave_gift));
    memset(&g->boss, 0, sizeof(g->boss));

    gx_bg_reset(g);
    player_reset(g, false);
}

static void game_start(g_t *g)
{
    g->score = 0;
    g->lives = G_LIVES_START;
    g->weapon = W_SHOT;
    g->wlevel = 1;
    g->tilt_zeroed = 0;
    level_start(g, 0);
}

static bool enemies_left(const g_t *g)
{
    for (int i = 0; i < G_MAX_ENEMIES; i++) {
        if (g->enemies[i].alive) {
            return true;
        }
    }
    return false;
}

static void world_update(g_t *g)
{
    gx_bg_update(g);

    for (int i = 0; i < G_MAX_ENEMIES; i++) {
        g_enemy_t *e = &g->enemies[i];
        if (!e->alive) {
            continue;
        }
        gx_enemy_update(g, e);

        int x = UNFX(e->x), y = UNFX(e->y);
        if (y > G_PLAY_Y1 + 24 || x < -34 || x > GX_W + 34) {
            e->alive = 0;
            if (e->wave && g->wave_left[e->wave] > 0) {
                /* it got away: the formation no longer counts as wiped out */
                g->wave_left[e->wave]--;
                g->wave_gift[e->wave] = 0;
            }
        }
    }

    for (int i = 0; i < G_MAX_PICKUPS; i++) {
        g_pickup_t *p = &g->pickups[i];
        if (!p->alive) {
            continue;
        }
        p->t++;
        p->y = (int16_t)(p->y + p->vy);
        p->x = (int16_t)(p->x + gx_sin(p->t * 4) * 8 / 256);
        if (UNFX(p->y) > G_PLAY_Y1 + 10) {
            p->alive = 0;
        }
    }

    shots_update(g);
    fx_update(g);
}

static void world_draw(g_t *g)
{
    gx_bg_draw(g);

    for (int i = 0; i < G_MAX_PICKUPS; i++) {
        if (g->pickups[i].alive) {
            pickup_draw(g, &g->pickups[i]);
        }
    }
    gx_boss_draw(g);
    for (int i = 0; i < G_MAX_ENEMIES; i++) {
        if (g->enemies[i].alive) {
            gx_enemy_draw(g, &g->enemies[i]);
        }
    }
    player_draw(g);
    shots_draw(g);
    fx_draw(g);
}

/* declared further down: the LVGL panels and the flight state */
static void overlay_show(app_t *a, lv_obj_t *panel);
static bool state_is_flying(const g_t *g);
static void overlay_hide_all(app_t *a);
static void title_refresh(app_t *a);

static void game_over(app_t *a)
{
    g_t *g = &a->g;
    char buf[64];

    g->state = ST_GAMEOVER;
    g->state_t = 0;
    if (g->score >= g->hiscore) {
        aos_hal_pref_set_i32(KEY_HI, (int32_t)g->hiscore);
    }
    snprintf(buf, sizeof(buf), _("PUNTOS  %lu\nRECORD  %lu"),
             (unsigned long)g->score, (unsigned long)g->hiscore);
    lv_label_set_text(a->over_score, buf);
    overlay_show(a, a->over);
}

static void step_state(app_t *a)
{
    g_t *g = &a->g;
    const g_level_t *lv = &gx_levels[g->level];

    g->state_t++;

    switch (g->state) {
    case ST_TITLE:
        /* the background stays alive behind the panel: the ship flies itself */
        gx_bg_update(g);
        shots_update(g);
        fx_update(g);
        g->px = (int16_t)(FX(GX_W / 2) + gx_sin(g->state_t * 2) * FX(40) / 256);
        g->py = FX(GX_H - 8);
        if (g->state_t % 14 == 0) {
            player_fire(g);     /* for show: the menu's ship fires by itself */
        }
        break;

    case ST_READY:
        world_update(g);
        player_move(g);
        if (g->state_t > 70) {
            g->state = ST_PLAY;
            g->state_t = 0;
        }
        break;

    case ST_PLAY:
        g->level_t++;
        waves_update(g);
        world_update(g);
        player_move(g);
        collisions(g);

        if (g->level_t >= lv->length && !enemies_left(g)) {
            gx_boss_start(g, lv->boss);
            g->state = ST_BOSS_IN;
            g->state_t = 0;
            aos_hal_beep(300, 200);
        }
        break;

    case ST_BOSS_IN:
        world_update(g);
        gx_boss_update(g);
        player_move(g);
        collisions(g);
        if (g->state_t > 60) {
            g->state = ST_BOSS;
            g->state_t = 0;
        }
        break;

    case ST_BOSS:
        world_update(g);
        gx_boss_update(g);
        player_move(g);
        collisions(g);
        if (!g->boss.alive) {
            g->state = ST_CLEAR;
            g->state_t = 0;
            g_add_score(g, 5000 + g->energy * 20);
        }
        break;

    case ST_CLEAR:
        world_update(g);
        player_move(g);
        g->py = (int16_t)(g->py - 6);       /* the ship leaves through the top */
        if (g->state_t > 110) {
            if (g->level + 1 < gx_level_count) {
                level_start(g, g->level + 1);
            } else {
                g->state = ST_WIN;
                g->state_t = 0;
                if (g->score >= g->hiscore) {
                    aos_hal_pref_set_i32(KEY_HI, (int32_t)g->hiscore);
                }
            }
        }
        break;

    case ST_DEAD:
        world_update(g);
        if (g->state_t == 40) {
            memset(g->eshots, 0, sizeof(g->eshots));    /* room to come back */
        }
        if (g->state_t > 70) {
            if (g->lives > 0) {
                player_reset(g, true);
                g->state = ST_PLAY;
                g->state_t = 0;
            } else {
                game_over(a);
            }
        }
        break;

    case ST_WIN:
        world_update(g);
        break;

    default:    /* ST_GAMEOVER, ST_PAUSE: the world stays frozen */
        break;
    }

    /* energy: 1943's hourglass */
    if (g->state == ST_PLAY || g->state == ST_BOSS || g->state == ST_BOSS_IN) {
        if ((g->state_t % DRAIN_EVERY) == 0) {
            g->energy--;
            if (g->energy <= 0) {
                g->energy = 0;
                player_die(g);
            }
        }
        if ((g->fire_down || g->autofire) && g->fire_cd <= 0) {
            player_fire(g);
        }
    }

    if (g->fire_cd > 0)  g->fire_cd--;
    if (g->last_click_t > 0) g->last_click_t--;
    if (g->invuln > 0)   g->invuln--;
    if (g->shield > 0)   g->shield--;
    if (g->roll > 0)     g->roll--;
    if (g->roll_cd > 0)  g->roll_cd--;
    if (g->shake > 0)    g->shake--;
    if (g->flash_screen > 0) g->flash_screen--;
}

static void draw_all(app_t *a)
{
    g_t *g = &a->g;

    world_draw(g);

    switch (g->state) {
    case ST_READY:
        banner(g, _(gx_levels[g->level].name), _(gx_levels[g->level].tag), GX_H / 2 - 16);
        break;
    case ST_BOSS_IN:
        if ((g->state_t / 6) % 2) {
            banner(g, _("ALERTA"), _(gx_bosses[gx_levels[g->level].boss].name),
                   GX_H / 2 - 30);
        }
        break;
    case ST_CLEAR:
        banner(g, _("PLANETA LIBERADO"), _(gx_levels[g->level].name), GX_H / 2 - 16);
        break;
    case ST_WIN:
        banner(g, _("SISTEMA LIBERADO"), _("GRACIAS POR JUGAR"), GX_H / 2 - 24);
        break;
    default:
        break;
    }

    if (g->state != ST_TITLE) {
        hud_draw(g);
    }

    /* the white flash is applied by expand_gx(), which already walks these pixels */
}

/* --------------------------------------------------------------------------
 * Loop
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * The period adjusts itself
 *
 * If a frame comes out more expensive than the timer's period, the timer is
 * always overdue: lv_timer_handler() never returns any free time, the LVGL
 * task never gets to sleep, CPU 0's idle task does not run and the task_wdt
 * fires. On this board that leaves the screen frozen (the watchdog is in
 * warning mode, not panic), which is exactly the symptom to avoid.
 *
 * So instead of asking for 30 frames per second and praying, how long a frame
 * really takes is measured and the period is relaxed until it fits. The game
 * gets slower, but the board goes on responding. When there is time to spare,
 * it is tightened a little at a time down to the floor.
 * -------------------------------------------------------------------------- */
static void period_tune(app_t *a)
{
    int want = a->period;

    if (a->real_ms > want + want / 3) {
        want = a->real_ms;              /* we are not even close */
    } else if (a->real_ms <= want + 2 && want > G_FRAME_MS) {
        want -= 6;                      /* time to spare: tighten a little */
    }

    want = clampi(want, G_FRAME_MS, G_FRAME_MAX);

    /* Adaptive quality. The expensive decorations (the nebulae, the suspended
     * dust, the halo on each bullet) are precisely the first thing to go when
     * the board cannot keep up: they are only switched on if the game manages
     * to run at the rate it wanted. It starts off, not on, so the first second
     * on the board is not the most expensive one. */
    uint8_t detail = (want <= G_FRAME_MS + 6) ? 1 : 0;
    if (detail != a->g.detail) {
        a->g.detail = detail;
        aos_hal_log("2043", "adornos %s", detail ? "completos" : "livianos");
    }

    if (want != a->period) {
        aos_hal_log("2043", "cuadro real %d ms: periodo %d -> %d ms (%d.%d fps)",
                    a->real_ms, a->period, want, a->g.fps10 / 10, a->g.fps10 % 10);
        a->period = (int16_t)want;
        lv_timer_set_period(a->timer, (uint32_t)want);
    }
}

/* --------------------------------------------------------------------------
 * Leaving mid-flight
 *
 * While flying, the side button is the trigger and the back gesture is
 * disabled (dragging is how you fly). That left the game with no way out: you
 * had to restart the board.
 *
 * The way out is holding a finger still over the score, top left, for a
 * second. You never fly there — the ship is pinned thirteen pixels above the
 * finger, so reaching the top bar would mean taking it off the screen — and
 * since you also have to hold still, you do not fire by accident. The score
 * fills up with colour meanwhile, so it is clear something is happening.
 * -------------------------------------------------------------------------- */

static bool hold_zone(const g_t *g)
{
    return UNFX(g->touch_x) < HOLD_BOX_W && UNFX(g->touch_y) < G_HUD_H + 4;
}

static void hold_tick(app_t *a, uint64_t now)
{
    g_t *g = &a->g;

    if (!g->touching || !state_is_flying(g) || !hold_zone(g)) {
        a->hold_ms = 0;
        g->hold_pct = 0;
        return;
    }

    if (a->hold_ms == 0) {
        a->hold_ms = now;
        return;
    }

    uint32_t held = (uint32_t)(now - a->hold_ms);
    g->hold_pct = (int8_t)(held >= HOLD_MS ? 100 : (int)(held * 100 / HOLD_MS));

    if (held >= HOLD_MS) {
        a->hold_ms = 0;
        g->hold_pct = 0;
        g->touching = 0;        /* so the ship does not jump on the way back */
        g->fire_down = 0;
        /* The pause takes effect at the end of the frame: that way this one is
         * still drawn, with the bar already at zero, and the full bar is not
         * left frozen under the panel for as long as the pause lasts. */
        a->want_pause = true;
        aos_hal_beep(900, 60);
    }
}

static void frame(lv_timer_t *timer)
{
    app_t *a = (app_t *)lv_timer_get_user_data(timer);
    g_t *g = &a->g;

    if (a->want_exit) {
        /* aos_ui_back() destroys the app: after this call 'a' no longer
         * exists, so nothing else is touched */
        a->want_exit = false;
        aos_ui_back();
        return;
    }

    /* How long a frame takes end to end. It includes LVGL's drawing of the
     * previous frame, which is precisely the part we cannot time from here and
     * the one that costs the most.
     *
     * The first few frames do not count: the first one builds the whole screen
     * and, if it enters the average, it pushes the period up before the game
     * has had a chance to show how it really runs. */
    {
        uint64_t now = aos_hal_uptime_ms();
        if (a->frames < 1000) {
            a->frames++;
        }
        if (a->prev_ms && now > a->prev_ms && a->frames > 8) {
            /* the subtraction is narrowed to 32 bits on purpose: a 64-bit
             * division drags in __udivdi3, which the firmware does not export
             * to the apps */
            uint32_t dt = (uint32_t)(now - a->prev_ms);
            int inst = (int)(10000u / dt);
            g->fps10   = (int16_t)(g->fps10 ? (g->fps10 * 7 + inst) / 8 : inst);
            a->real_ms = (int16_t)(a->real_ms ? (a->real_ms * 7 + (int)dt) / 8
                                              : (int)dt);
        }
        a->prev_ms = now;
        hold_tick(a, now);
    }

    if (a->frames > 8 && ++a->tune_t >= 20) {
        a->tune_t = 0;
        period_tune(a);
    }

    if (g->state != ST_PAUSE) {
        /* The screen shake goes inside the upscaling: moving the canvas cost
         * two screens of drawing. The offset is one art pixel, that is, two
         * screen pixels, which is the same as what was seen before. */
        int sx = 0, sy = 0;
        if (g->shake > 0) {
            sx = (int)(g_rnd(g) % 3) - 1;
            sy = (int)(g_rnd(g) % 3) - 1;
        }
        int flash = g->flash_screen > 0 ? g->flash_screen * 3 : 0;
        if (flash > 16) {
            flash = 16;
        }

        step_state(a);
        draw_all(a);
        expand_gx(a->mem, a->big, sx, sy, flash);
        lv_obj_invalidate(a->canvas);
    }

    if (a->want_pause) {
        a->want_pause = false;
        g->state = ST_PAUSE;
        overlay_show(a, a->pause);
    }
}

/* --------------------------------------------------------------------------
 * Input
 * -------------------------------------------------------------------------- */

static void touch_event(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    g_t *g = &a->g;

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        g->touching = 0;
        return;
    }

    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);

    lv_area_t area;
    lv_obj_get_coords(a->touch, &area);
    int16_t nx = (int16_t)((point.x - area.x1) * FX_ONE / GX_SCALE);
    int16_t ny = (int16_t)((point.y - area.y1) * FX_ONE / GX_SCALE);

    /* if the finger moved, it is no longer a sustained touch but a movement */
    if (a->hold_ms) {
        int dx = UNFX(nx) - UNFX(g->touch_x);
        int dy = UNFX(ny) - UNFX(g->touch_y);
        if (dx * dx + dy * dy > HOLD_SLOP * HOLD_SLOP) {
            a->hold_ms = 0;
            g->hold_pct = 0;
        }
    }

    g->touch_x = nx;
    g->touch_y = ny;
    g->touching = 1;

    if (code == LV_EVENT_PRESSED && g->control == CTRL_TILT) {
        /* in sensor mode the finger moves nothing: a tap recalibrates the zero */
        g->tilt_zeroed = 0;
        g->touching = 0;
        aos_hal_beep(1000, 30);
    }
}

/* With AOS_APP_FLAG_NO_SWIPE the back gesture is handled by the app: dragging
 * is how you fly, so it only counts when you are not flying. */
static void touch_gesture(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_active();
    g_t *g = &a->g;

    if (!indev || lv_indev_get_gesture_dir(indev) != LV_DIR_RIGHT) {
        return;
    }
    if (g->state == ST_PLAY || g->state == ST_BOSS || g->state == ST_BOSS_IN ||
        g->state == ST_READY) {
        return;
    }
    lv_indev_wait_release(indev);
    a->want_exit = true;
}

static bool state_is_flying(const g_t *g)
{
    return g->state == ST_READY || g->state == ST_PLAY || g->state == ST_BOSS_IN ||
           g->state == ST_BOSS || g->state == ST_CLEAR || g->state == ST_DEAD ||
           g->state == ST_WIN;
}

/* The side button. While flying it is the trigger and nothing else: if it let
 * the long press through, holding it for a second in the middle of a firefight
 * would take you out of the game. For leaving there is the pause button. */
static bool app_button(aos_app_t *self, void *inst, int action)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return false;
    }
    g_t *g = &a->g;

    if (g->state == ST_PAUSE || !state_is_flying(g)) {
        return false;       /* let the runtime do the usual */
    }

    if (action == AOS_BUTTON_PRESS) {
        g->fire_down = 1;
        /* quick double tap: barrel roll, invulnerable for a moment */
        if (g->last_click_t > 0 && g->roll_cd <= 0 && g->roll <= 0) {
            g->roll = ROLL_FRAMES;
            g->roll_cd = ROLL_COOLDOWN;
            aos_hal_beep(900, 60);
        }
        g->last_click_t = 0;
    } else {
        g->fire_down = 0;
        g->last_click_t = 14;   /* window for the second tap */
    }
    return true;
}

static bool app_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return false;
    }
    g_t *g = &a->g;

    if (state_is_flying(g)) {
        g->state = ST_PAUSE;
        overlay_show(a, a->pause);
        return true;        /* consumed: pause instead of leaving */
    }
    /* From the pause, back is leaving.
     *
     * This used to return true "because the panel already has its own exit
     * button", and the result was that that button did not exit: exit_cb()
     * asks for aos_ui_back(), whose first act is to consult this callback, and
     * by consuming it the app was never closed. Besides, back from a pause
     * closing the game is what anybody expects. */
    return false;
}

/* --------------------------------------------------------------------------
 * LVGL panels
 *
 * The title, the pause and the game over are built with LVGL objects and not
 * with the 5x7 font: they are screens to read and touch, and there vector text
 * reads far better than pixelated. The game itself is all canvas.
 * -------------------------------------------------------------------------- */

static void overlay_hide_all(app_t *a)
{
    lv_obj_t *const panels[] = { a->title, a->pause, a->over };
    for (unsigned i = 0; i < sizeof(panels) / sizeof(panels[0]); i++) {
        if (panels[i]) {
            lv_obj_add_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void overlay_show(app_t *a, lv_obj_t *panel)
{
    overlay_hide_all(a);
    if (panel) {
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(panel);
    }
}

static lv_obj_t *make_panel(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, w, h);
    lv_obj_center(p);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);  /* so touches do not pass through */
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t color, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_y(l, y);
    return l;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, const char *sub,
                             int x, int y, int w, int h, uint32_t accent,
                             lv_event_cb_t cb, void *data)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1C1C24), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, sub ? &aos_montserrat_20 : &aos_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_align(l, sub ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, sub ? 10 : 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);

    if (sub) {
        lv_obj_t *s = lv_label_create(b);
        lv_label_set_text(s, sub);
        lv_obj_set_style_text_font(s, &aos_montserrat_14, 0);
        lv_obj_set_style_text_color(s, lv_color_hex(0x9AA3B8), 0);
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(s, lv_pct(100));
        lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 36);
        lv_obj_remove_flag(s, LV_OBJ_FLAG_CLICKABLE);
    }
    return b;
}

/* The chips are flat buttons whose text states the setting: "AUTO SI" /
 * "AUTO NO". There is no room for an LVGL switch with a label beside it. */
static void chip_set(lv_obj_t *chip, const char *text, bool on)
{
    lv_obj_t *l = lv_obj_get_child(chip, 0);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(on ? 0x0A0A12 : 0x9AA3B8), 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(on ? 0x30D158 : 0x1C1C24), 0);
}

static lv_obj_t *make_chip(lv_obj_t *parent, int x, int y, int w,
                           lv_event_cb_t cb, void *data)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, 32);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x3A3A46), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, data);

    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, &aos_montserrat_14, 0);
    /* The text is clipped INSIDE the chip. These chips are at absolute
     * positions with a fixed width and there are four of them in 368 px: there
     * is no room for them to grow. With an ellipsis, a long translation looks
     * ugly but stays contained; without this it draws over the chip next to
     * it. */
    /* Width AND height: with the width alone, the label keeps its content
     * height -36 px for a line of 18- and spills above and below a chip of
     * 32. */
    lv_obj_set_size(l, w - 8, 24);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static void title_refresh(app_t *a)
{
    g_t *g = &a->g;
    char buf[48];

    snprintf(buf, sizeof(buf), _("RECORD  %lu"), (unsigned long)g->hiscore);
    lv_label_set_text(a->title_hi, buf);

    chip_set(a->chip_auto, g->autofire ? _("AUTO SI") : _("AUTO NO"), g->autofire);
    chip_set(a->chip_invx, _("INV X"), g->inv_x);
    chip_set(a->chip_invy, _("INV Y"), g->inv_y);
    chip_set(a->chip_swap, _("EJES XY"), g->swap_axes);
}

static void prefs_save(app_t *a)
{
    g_t *g = &a->g;
    aos_hal_pref_set_i32(KEY_CTRL, g->control);
    aos_hal_pref_set_i32(KEY_AUTO, g->autofire);
    aos_hal_pref_set_i32(KEY_AXES, g->inv_x | (g->inv_y << 1) | (g->swap_axes << 2));
    aos_hal_pref_set_i32(KEY_SFX, s_sfx ? 1 : 0);
}

static void start_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    int control = (int)(lv_uintptr_t)lv_obj_get_user_data(lv_event_get_target_obj(event));

    a->g.control = (uint8_t)control;
    prefs_save(a);
    overlay_hide_all(a);
    game_start(&a->g);
}

static void chip_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_obj_t *chip = lv_event_get_target_obj(event);
    g_t *g = &a->g;

    if (chip == a->chip_auto)      g->autofire  = !g->autofire;
    else if (chip == a->chip_invx) g->inv_x     = !g->inv_x;
    else if (chip == a->chip_invy) g->inv_y     = !g->inv_y;
    else if (chip == a->chip_swap) g->swap_axes = !g->swap_axes;

    prefs_save(a);
    title_refresh(a);
    aos_hal_beep(1200, 20);
}

static void resume_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    overlay_hide_all(a);
    a->g.state = (uint8_t)(a->g.boss.alive ? ST_BOSS : ST_PLAY);
    a->g.fire_down = 0;
}

static void sfx_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    s_sfx = !s_sfx;
    chip_set(a->chip_sfx, s_sfx ? _("SONIDO SI") : _("SONIDO NO"), s_sfx);
    prefs_save(a);
}

static void fps_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    a->g.show_fps = !a->g.show_fps;
    chip_set(a->chip_fps, a->g.show_fps ? _("FPS SI") : _("FPS NO"), a->g.show_fps);
    aos_hal_pref_set_i32(KEY_FPS, a->g.show_fps);
}

static void retry_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    overlay_hide_all(a);
    game_start(&a->g);
}

static void menu_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    a->g.state = ST_TITLE;
    a->g.state_t = 0;
    a->g.level = 2;             /* the title's background is the debris field */
    gx_bg_reset(&a->g);
    title_refresh(a);
    overlay_show(a, a->title);
}

static void exit_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    a->want_exit = true;        /* it exits on the next frame, not here */
}

static void build_title(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root, AOS_SCREEN_W, AOS_SCREEN_H);
    a->title = p;

    /* A dark plate behind the text: the game's background goes on running, but
     * over a field of stars and rocks the text cannot be read. The screen's
     * edges are left clear so it can be seen that something is alive behind. */
    lv_obj_t *card = lv_obj_create(p);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 352, 396);
    lv_obj_set_pos(card, 8, 8);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x05060F), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_80, 0);
    lv_obj_set_style_radius(card, 26, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A3145), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    make_label(p, "2043", &aos_montserrat_48, 0xFF4A3D, 20);
    make_label(p, _("LA BATALLA DE CERES"), &aos_montserrat_16, 0xFFE45E, 74);
    make_label(p, _("COMO QUERES JUGAR"), &aos_montserrat_16, 0x9AA3B8, 104);

    lv_obj_t *b1 = make_button(p, _("TACTIL"), _("arrastra el dedo para volar"),
                               19, 130, 330, 68, 0x0A84FF, start_cb, a);
    lv_obj_set_user_data(b1, (void *)(lv_uintptr_t)CTRL_TOUCH);

    lv_obj_t *b2 = make_button(p, _("SENSOR"), _("inclina la placa para volar"),
                               19, 206, 330, 68, 0xBF5AF2, start_cb, a);
    lv_obj_set_user_data(b2, (void *)(lv_uintptr_t)CTRL_TILT);

    make_label(p, _("EL BOTON LATERAL DISPARA"), &aos_montserrat_14, 0x7BE9FF, 282);

    /* Widths sized for the LONGEST text in any language, not for Spanish. With
     * "AUTO NO" the first fitted in 76 px; "AUTO OFF" ran off the left of the
     * screen. Layout: margin 13, spacing 6, and 88+68+68+100 = 324, which with
     * the three gaps makes 342 and leaves 13 on each side. */
    a->chip_auto = make_chip(p,  13, 302, 88, chip_cb, a);
    a->chip_invx = make_chip(p, 107, 302, 68, chip_cb, a);
    a->chip_invy = make_chip(p, 181, 302, 68, chip_cb, a);
    a->chip_swap = make_chip(p, 255, 302, 100, chip_cb, a);

    make_label(p, _("EJES: SOLO EN MODO SENSOR"), &aos_montserrat_14,
               0x6A6A78, 340);
    a->title_hi = make_label(p, _("RECORD 0"), &aos_montserrat_20, 0xFFFFFF, 360);
    make_label(p, _("MANTENE EL MARCADOR PARA PAUSAR"), &aos_montserrat_14,
               0x7BE9FF, 380);
}

static void build_pause(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root, 268, 286);
    a->pause = p;
    lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);
    lv_obj_set_style_radius(p, 24, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(0x3A3A46), 0);
    lv_obj_set_style_border_width(p, 2, 0);

    make_label(p, _("PAUSA"), &aos_montserrat_28, 0xFFFFFF, 18);
    make_button(p, _("SEGUIR"), NULL, 24, 66, 220, 46, 0x30D158, resume_cb, a);
    a->chip_sfx = make_chip(p, 24, 122, 220, sfx_cb, a);
    a->chip_fps = make_chip(p, 24, 162, 220, fps_cb, a);
    make_button(p, _("SALIR AL RELOJ"), NULL, 24, 206, 220, 46, 0xFF453A, exit_cb, a);
}

static void build_over(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root, 288, 262);
    a->over = p;
    lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);
    lv_obj_set_style_radius(p, 24, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(0xFF453A), 0);
    lv_obj_set_style_border_width(p, 2, 0);

    make_label(p, _("FIN DEL JUEGO"), &aos_montserrat_28, 0xFF4A3D, 18);
    a->over_score = make_label(p, "", &aos_montserrat_20, 0xFFFFFF, 62);

    make_button(p, _("OTRA VEZ"), NULL, 24, 128, 240, 46, 0x30D158, retry_cb, a);
    make_button(p, _("MENU"), NULL, 24, 182, 116, 46, 0x0A84FF, menu_cb, a);
    make_button(p, _("SALIR"), NULL, 148, 182, 116, 46, 0xFF453A, exit_cb, a);
}

/* --------------------------------------------------------------------------
 * Life cycle
 * -------------------------------------------------------------------------- */

static void prefs_load(app_t *a)
{
    g_t *g = &a->g;
    int32_t v = 0;

    /* Default axes, with what was measured on the board: the axis running
     * across the screen is ay and not ax (hence the swap), and the right of
     * the screen is -ay (hence the inversion in X). The vertical sign has not
     * been measured yet; if it comes out backwards it is fixed with the INV Y
     * chip, without recompiling. Note: if the key is already stored in NVS,
     * this has no visible effect. */
    g->swap_axes = 1;
    g->inv_x     = 1;

    if (aos_hal_pref_get_i32(KEY_HI, &v) && v > 0) {
        g->hiscore = (uint32_t)v;
    }
    if (aos_hal_pref_get_i32(KEY_CTRL, &v)) {
        g->control = (uint8_t)(v == CTRL_TILT ? CTRL_TILT : CTRL_TOUCH);
    }
    if (aos_hal_pref_get_i32(KEY_AUTO, &v)) {
        g->autofire = (uint8_t)(v ? 1 : 0);
    }
    if (aos_hal_pref_get_i32(KEY_AXES, &v)) {
        g->inv_x     = (int8_t)(v & 1);
        g->inv_y     = (int8_t)((v >> 1) & 1);
        g->swap_axes = (int8_t)((v >> 2) & 1);
    }
    if (aos_hal_pref_get_i32(KEY_SFX, &v)) {
        s_sfx = (v != 0);
    }
    if (aos_hal_pref_get_i32(KEY_FPS, &v)) {
        g->show_fps = (uint8_t)(v ? 1 : 0);
    }
}

static void *g2043_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) {
        return NULL;
    }

    /* The canvas buffer is 82 KB: it goes through malloc() and not through
     * lv_malloc(), which on the board has a small pool of internal RAM. With
     * CONFIG_SPIRAM_USE_MALLOC this lands in PSRAM, which is where it
     * belongs. */
    /* A trail on the serial port. If the board hangs or restarts, the last
     * line says which stage it was in: without this you have to guess. */
    uint32_t heap_int = 0, heap_psram = 0;
    aos_hal_heap_info(&heap_int, &heap_psram);
    aos_hal_log("2043", "abriendo | interna %u B, psram %u B",
                (unsigned)heap_int, (unsigned)heap_psram);

    a->mem = (uint16_t *)malloc((size_t)GX_W * GX_H * sizeof(uint16_t));
    /* The same buffer already upscaled x2: it is what the canvas sees. See
     * expand_gx(). */
    a->big = (uint16_t *)malloc((size_t)GX_W * GX_H * GX_SCALE * GX_SCALE *
                                sizeof(uint16_t));
    if (!a->mem || !a->big) {
        aos_hal_log("2043", "sin memoria para el canvas de %u B",
                    (unsigned)((size_t)GX_W * GX_H * sizeof(uint16_t)));
        lv_free(a);
        return NULL;
    }
    memset(a->mem, 0, (size_t)GX_W * GX_H * sizeof(uint16_t));

    a->g.buf.px = a->mem;
    a->g.buf.w  = GX_W;
    a->g.buf.h  = GX_H;
    a->g.rng    = (uint32_t)aos_hal_uptime_ms() | 1u;

    prefs_load(a);

    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    a->canvas = lv_canvas_create(root);
    /* The canvas receives the ALREADY upscaled buffer and is drawn 1:1: no
     * STRETCH. */
    lv_canvas_set_buffer(a->canvas, a->big, GX_W * GX_SCALE, GX_H * GX_SCALE,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(a->canvas, GX_W * GX_SCALE, GX_H * GX_SCALE);
    lv_obj_set_pos(a->canvas, 0, 0);
    lv_image_set_antialias(a->canvas, false);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_SCROLLABLE);

    /* touch layer: in LVGL 9 every object is born clickable, so without this
     * the canvas would eat the finger */
    a->touch = lv_obj_create(root);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(a->touch, 0, 0);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESSING, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESS_LOST, a);
    lv_obj_add_event_cb(a->touch, touch_gesture, LV_EVENT_GESTURE, a);

    build_title(a, root);
    build_pause(a, root);
    build_over(a, root);

#ifdef AOS_SIM_BUILTIN
    /* Shortcuts for designing without playing twenty minutes to reach the
     * boss. They only exist in the simulator: there are no environment
     * variables on the board.
     *
     *   G2043_LEVEL=2 ./amoledos_sim     starts on the third planet
     *   G2043_BOSS=1  ./amoledos_sim     and jumps straight to the boss
     */
    const char *env_level = getenv("G2043_LEVEL");
    if (env_level) {
        a->g.control = CTRL_TOUCH;
        a->g.autofire = 1;
        game_start(&a->g);
        level_start(&a->g, atoi(env_level));
        if (getenv("G2043_BOSS")) {
            a->g.level_t = gx_levels[a->g.level].length;
            a->g.wave_next = gx_levels[a->g.level].wave_count;
            a->g.state = ST_PLAY;
            a->g.state_t = 0;
        }
        const char *test = getenv("G2043_TEST");
        if (test && test[0] == 'p') {
            /* one capsule of each type, to look at them all together */
            for (int i = 0; i < PU_COUNT && i < G_MAX_PICKUPS; i++) {
                g_drop_pickup(&a->g, FX(24 + i * 26), FX(40 + (i & 1) * 20), i);
            }
        } else if (test && test[0] == 'o') {
            a->g.lives = 1;         /* to reach the ending panel quickly */
            a->g.energy = 30;
        }
        title_refresh(a);
        chip_set(a->chip_sfx, s_sfx ? _("SONIDO SI") : _("SONIDO NO"), s_sfx);
        chip_set(a->chip_fps, a->g.show_fps ? _("FPS SI") : _("FPS NO"), a->g.show_fps);
        a->period = G_FRAME_MS;
        a->timer = lv_timer_create(frame, G_FRAME_MS, a);
        return a;
    }
#endif

    /* it starts in the menu, with the debris field behind */
    a->g.state = ST_TITLE;
    a->g.level = 2;
    a->g.lives = G_LIVES_START;
    a->g.px = FX(GX_W / 2);
    a->g.py = FX(G_PLAY_Y1 - 40);
    gx_bg_reset(&a->g);
    title_refresh(a);
    chip_set(a->chip_sfx, s_sfx ? _("SONIDO SI") : _("SONIDO NO"), s_sfx);
    chip_set(a->chip_fps, a->g.show_fps ? _("FPS SI") : _("FPS NO"), a->g.show_fps);
    overlay_show(a, a->title);

    aos_hal_heap_info(&heap_int, &heap_psram);
    aos_hal_log("2043", "listo | interna %u B, psram %u B | canvas %dx%d x%d",
                (unsigned)heap_int, (unsigned)heap_psram, GX_W, GX_H, GX_SCALE);

    a->period = G_FRAME_MS;
    a->timer = lv_timer_create(frame, G_FRAME_MS, a);
    return a;
}

static void g2043_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return;
    }
    if (a->timer) {
        lv_timer_delete(a->timer);
    }
    if (a->g.score >= a->g.hiscore) {
        aos_hal_pref_set_i32(KEY_HI, (int32_t)a->g.hiscore);
    }
    free(a->mem);
    free(a->big);
    lv_free(a);
}

/* Leaving mid-flight should not cost a ship: it pauses. */
static void g2043_hide(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (a && state_is_flying(&a->g)) {
        a->g.state = ST_PAUSE;
        a->g.fire_down = 0;
        overlay_show(a, a->pause);
    }
}

static bool g2043_init(aos_app_t *app)
{
    app->desc.id      = "demo.2043";
    app->desc.name    = "2043";
    app->desc.icon    = LV_SYMBOL_GPS;
    app->desc.icon_vec = AOS_ICON_SHIP;
    app->desc.color_a = 0xFF4A3D;
    app->desc.color_b = 0x6A2FB5;
    app->desc.order   = 145;
    app->desc.flags   = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                        AOS_APP_FLAG_NO_SWIPE;

    app->create  = g2043_create;
    app->destroy = g2043_destroy;
    app->hide    = g2043_hide;
    app->back    = app_back;
    app->button  = app_button;
    return true;
}

AOS_APP_ENTRY(g2043_init);
