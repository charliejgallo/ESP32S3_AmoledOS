/*
 * ARKANOS - the simulation
 *
 * All in integers and in 1/16-of-a-pixel fixed point. No floating point except
 * the accelerometer reading, which already arrives as a float from the HAL.
 *
 * The ball's collision is resolved axis by axis and in half-pixel micro-steps:
 * move in X first, see what was hit, and then move in Y. It is the cheap way
 * of making the ball bounce correctly off corners without solving a continuous
 * collision, and of stopping it going through an 8 px tall brick when the
 * speed rises to four pixels per frame.
 */
#include "arkanos.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Utilities
 * -------------------------------------------------------------------------- */

uint32_t ak_rnd(ak_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

int ak_rnd_range(ak_t *g, int lo, int hi)
{
    if (hi <= lo) {
        return lo;
    }
    return lo + (int)(ak_rnd(g) % (uint32_t)(hi - lo + 1));
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* --------------------------------------------------------------------------
 * Effects
 * -------------------------------------------------------------------------- */

void ak_boom(ak_t *g, int x, int y, int n, uint16_t col)
{
    for (int i = 0; i < n; i++) {
        ak_bit_t *b = NULL;
        for (int k = 0; k < AK_MAX_BITS; k++) {
            if (!g->bit[k].life) {
                b = &g->bit[k];
                break;
            }
        }
        if (!b) {
            return;
        }
        b->x  = FX(x);
        b->y  = FX(y);
        b->vx = (int16_t)ak_rnd_range(g, -26, 26);
        b->vy = (int16_t)ak_rnd_range(g, -30, 6);
        b->life0 = (uint8_t)ak_rnd_range(g, 12, 22);
        b->life  = b->life0;
        b->col   = col;
    }
}

void ak_popup(ak_t *g, int x, int y, const char *txt, uint16_t col)
{
    for (int i = 0; i < AK_MAX_POPS; i++) {
        if (g->pop[i].life) {
            continue;
        }
        ak_pop_t *p = &g->pop[i];
        int j = 0;
        while (txt[j] && j < (int)sizeof(p->txt) - 1) {
            p->txt[j] = txt[j];
            j++;
        }
        p->txt[j] = '\0';
        p->x = (int16_t)clampi(x, AK_FIELD_X0 + ak_text_w(p->txt) / 2,
                               AK_FIELD_X1 - ak_text_w(p->txt) / 2);
        p->y = (int16_t)y;
        p->col = col;
        p->life = 26;
        return;
    }
}

static void ring_add(ak_t *g, int x, int y, uint16_t col, int life)
{
    for (int i = 0; i < AK_MAX_RINGS; i++) {
        if (g->ring[i].life) {
            continue;
        }
        g->ring[i].x = (int16_t)x;
        g->ring[i].y = (int16_t)y;
        g->ring[i].col = col;
        g->ring[i].life0 = (uint8_t)life;
        g->ring[i].life = (uint8_t)life;
        return;
    }
}

/* --------------------------------------------------------------------------
 * Bricks
 * -------------------------------------------------------------------------- */

static void cap_drop(ak_t *g, int x, int y, int kind)
{
    for (int i = 0; i < AK_MAX_CAPS; i++) {
        if (g->cap[i].alive) {
            continue;
        }
        g->cap[i].alive = 1;
        g->cap[i].kind  = (uint8_t)kind;
        g->cap[i].x     = FX(x);
        g->cap[i].y     = FX(y);
        g->cap[i].t     = 0;
        return;
    }
}

/* Weighted draw: LIFE has to be rare and WIDE common, or the game gives itself
 * away. The numbers are how many tickets each type puts in. */
static int cap_pick(ak_t *g)
{
    static const uint8_t peso[CAP_COUNT] = {
        [CAP_WIDE] = 22, [CAP_SLOW] = 14, [CAP_MULTI] = 16, [CAP_LASER] = 16,
        [CAP_CATCH] = 14, [CAP_LIFE] = 4, [CAP_POINTS] = 14,
    };
    int total = 0;
    for (int i = 0; i < CAP_COUNT; i++) {
        total += peso[i];
    }
    int r = ak_rnd_range(g, 0, total - 1);
    for (int i = 0; i < CAP_COUNT; i++) {
        if (r < peso[i]) {
            return i;
        }
        r -= peso[i];
    }
    return CAP_POINTS;
}

static void brick_damage(ak_t *g, int row, int col, int dmg, int depth,
                         ak_ball_t *by);

/* Score for the brick that breaks. Only the ball builds a combo: a laser or a
 * bomb's blast have no business multiplying. */
static void brick_score(ak_t *g, ak_ball_t *b, const ak_brick_def_t *d,
                        int bx, int by_px)
{
    uint32_t pts = d->score;

    if (b) {
        if (b->combo < 40) {
            b->combo++;
        }
        if (b->combo >= 3) {
            pts += pts * (b->combo - 2) / 4;
        }
        if (b->combo == 6 || b->combo == 12 || b->combo == 20) {
            char txt[8];
            txt[0] = 'X';
            ak_num(txt + 1, b->combo, 1);
            ak_popup(g, bx + AK_BRICK_W / 2, by_px, txt, ak_rgb(0xFFE45E));
            ak_sfx(1500 + b->combo * 40, 26);
        }
    }
    g->score += pts;
    g->hud_dirty = 1;
}

static void brick_break(ak_t *g, int row, int col, int depth, ak_ball_t *by)
{
    const ak_brick_def_t *d = ak_brick(g->grid[row][col]);
    int bx, byp;
    ak_brick_box(row, col, &bx, &byp);
    int cx = bx + AK_BRICK_W / 2;
    int cy = byp + AK_BRICK_H / 2;
    bool bomb = (d->flags & AK_BF_BOMB) != 0;
    bool myst = (d->flags & AK_BF_MYST) != 0;
    uint16_t col565 = ak_rgb(d->color);

    brick_score(g, by, d, bx, byp);

    g->grid[row][col] = BK_NONE;
    g->hp[row][col]   = 0;
    g->hit[row][col]  = 0;
    if (g->left) {
        g->left--;
    }
    ak_bg_brick(g, row, col);

    ak_boom(g, cx, cy, bomb ? 10 : 5, col565);

    const ak_level_t *lv = ak_level_get(g->level);
    if (myst || (int)(ak_rnd(g) % 100u) < lv->drop) {
        cap_drop(g, cx, cy, cap_pick(g));
    }

    if (bomb) {
        ring_add(g, cx, cy, ak_rgb(0xFFE45E), 12);
        ak_sfx(140, 70);
        if (depth < 3) {
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    if (!dr && !dc) {
                        continue;
                    }
                    brick_damage(g, row + dr, col + dc, 3, depth + 1, NULL);
                }
            }
        }
    }
}

static void brick_damage(ak_t *g, int row, int col, int dmg, int depth,
                         ak_ball_t *by)
{
    if (row < 0 || row >= AK_ROWS || col < 0 || col >= AK_COLS || dmg <= 0) {
        return;
    }
    uint8_t kind = g->grid[row][col];
    if (kind == BK_NONE) {
        return;
    }
    const ak_brick_def_t *d = ak_brick(kind);

    if (d->flags & AK_BF_SOLID) {
        g->hit[row][col] = 3;
        return;
    }
    if (g->hp[row][col] > dmg) {
        g->hp[row][col] = (uint8_t)(g->hp[row][col] - dmg);
        g->hit[row][col] = 4;
        ak_bg_brick(g, row, col);       /* the brick looks more worn */
        g->score += 10;
        g->hud_dirty = 1;
        ak_sfx(660, 18);
        return;
    }
    brick_break(g, row, col, depth, by);
}

/* --------------------------------------------------------------------------
 * Ball against bricks
 * -------------------------------------------------------------------------- */

static int row_of(int y)
{
    int r = y - AK_BRICK_Y0;
    return r < 0 ? -1 : r / AK_BRICK_H;
}

static int col_of(int x)
{
    int c = x - AK_BRICK_X0;
    return c < 0 ? -1 : c / AK_BRICK_W;
}

/* Hits every cell the ball's box touches and says whether there was one. They
 * are all hit and not just one because when the ball goes in right along the
 * seam between two bricks, breaking only one leaves the bounce looking odd. */
static bool ball_hits(ak_t *g, ak_ball_t *b)
{
    int px = UNFX(b->x), py = UNFX(b->y);
    int r0 = row_of(py - AK_BALL_R), r1 = row_of(py + AK_BALL_R);
    int c0 = col_of(px - AK_BALL_R), c1 = col_of(px + AK_BALL_R);

    if (r1 < 0 || c1 < 0) {
        return false;
    }
    if (r0 < 0) r0 = 0;
    if (c0 < 0) c0 = 0;
    if (r1 >= AK_ROWS) r1 = AK_ROWS - 1;
    if (c1 >= AK_COLS) c1 = AK_COLS - 1;

    bool any = false;
    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            if (g->grid[r][c] == BK_NONE) {
                continue;
            }
            any = true;
            brick_damage(g, r, c, 1, 0, b);
        }
    }
    if (any) {
        ak_sfx(300 + (py % 8) * 40, 16);
    }
    return any;
}

static void ball_launch(ak_t *g, ak_ball_t *b)
{
    int a = ak_rnd_range(g, 52, 76);        /* nearly vertical, gracefully */
    b->held = 0;
    b->vx = (int16_t)(g->speed * ak_cos(a) / 256);
    b->vy = (int16_t)(-g->speed * ak_sin(a) / 256);
    ak_sfx(880, 40);
}

static void ball_from_paddle(ak_t *g, ak_ball_t *b)
{
    int px = UNFX(b->x);
    int pad = UNFX(g->pad_x);
    int half = g->pad_w / 2 + AK_BALL_R;

    int off = clampi((px - pad) * 100 / (half > 0 ? half : 1), -100, 100);
    int a = 64 - off * 38 / 100;            /* 26..102 brads */

    /* a bit of spin: the moving paddle pushes the ball */
    a -= clampi(UNFX(g->pad_vx) * 2, -10, 10);
    a = clampi(a, 22, 106);

    b->vx = (int16_t)(g->speed * ak_cos(a) / 256);
    b->vy = (int16_t)(-g->speed * ak_sin(a) / 256);
    b->y  = FX(AK_PAD_Y - AK_BALL_R - 1);
    b->combo = 0;

    if (g->t_catch) {
        b->held = 1;
        b->hold_off = (int16_t)(b->x - g->pad_x);
        ak_sfx(520, 30);
    } else {
        ak_sfx(440, 22);
    }
}

static void ball_step(ak_t *g, ak_ball_t *b)
{
    /* trail: stored before moving, in whole pixels */
    for (int i = AK_TRAIL - 1; i > 0; i--) {
        b->tx[i] = b->tx[i - 1];
        b->ty[i] = b->ty[i - 1];
    }
    b->tx[0] = (int16_t)UNFX(b->x);
    b->ty[0] = (int16_t)UNFX(b->y);
    if (b->tn < AK_TRAIL) {
        b->tn++;
    }

    if (b->held) {
        b->x = (int16_t)(g->pad_x + b->hold_off);
        b->y = FX(AK_PAD_Y - AK_BALL_R - 1);
        b->x = (int16_t)clampi(b->x, FX(AK_FIELD_X0 + AK_BALL_R),
                               FX(AK_FIELD_X1 - AK_BALL_R - 1));
        return;
    }

    int rx = b->vx, ry = b->vy;

    while ((rx || ry) && b->alive) {
        int dx = clampi(rx, -8, 8);
        int dy = clampi(ry, -8, 8);
        rx -= dx;
        ry -= dy;

        if (dx) {
            b->x = (int16_t)(b->x + dx);
            int px = UNFX(b->x);
            if (px - AK_BALL_R < AK_FIELD_X0) {
                b->x = FX(AK_FIELD_X0 + AK_BALL_R);
                b->vx = (int16_t)-b->vx;
                rx = -rx;
                ak_sfx(240, 12);
            } else if (px + AK_BALL_R >= AK_FIELD_X1) {
                b->x = FX(AK_FIELD_X1 - AK_BALL_R - 1);
                b->vx = (int16_t)-b->vx;
                rx = -rx;
                ak_sfx(240, 12);
            } else if (ball_hits(g, b)) {
                b->x = (int16_t)(b->x - dx);
                b->vx = (int16_t)-b->vx;
                rx = -rx;
            }
        }

        if (dy) {
            b->y = (int16_t)(b->y + dy);
            int px = UNFX(b->x), py = UNFX(b->y);

            if (py - AK_BALL_R < AK_FIELD_Y0) {
                b->y = FX(AK_FIELD_Y0 + AK_BALL_R);
                b->vy = (int16_t)-b->vy;
                ry = -ry;
                ak_sfx(240, 12);
            } else if (b->vy > 0 &&
                       py + AK_BALL_R >= AK_PAD_Y &&
                       py - AK_BALL_R < AK_PAD_Y + AK_PAD_H &&
                       px >= UNFX(g->pad_x) - g->pad_w / 2 - AK_BALL_R &&
                       px <= UNFX(g->pad_x) + g->pad_w / 2 + AK_BALL_R) {
                ball_from_paddle(g, b);
                return;             /* the rest of the movement is lost: it is one frame */
            } else if (py - AK_BALL_R > AK_DEATH_Y) {
                b->alive = 0;
                return;
            } else if (ball_hits(g, b)) {
                b->y = (int16_t)(b->y - dy);
                b->vy = (int16_t)-b->vy;
                ry = -ry;
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * Capsules, laser and particles
 * -------------------------------------------------------------------------- */

static void speed_set(ak_t *g, int nuevo)
{
    int viejo = g->speed;
    if (nuevo == viejo || viejo <= 0) {
        g->speed = (uint8_t)nuevo;
        return;
    }
    g->speed = (uint8_t)nuevo;
    for (int i = 0; i < AK_MAX_BALLS; i++) {
        ak_ball_t *b = &g->ball[i];
        if (!b->alive || b->held) {
            continue;
        }
        b->vx = (int16_t)(b->vx * nuevo / viejo);
        b->vy = (int16_t)(b->vy * nuevo / viejo);
    }
}

static void ball_split(ak_t *g)
{
    int origen = -1;
    for (int i = 0; i < AK_MAX_BALLS; i++) {
        if (g->ball[i].alive) {
            origen = i;
            break;
        }
    }
    if (origen < 0) {
        return;
    }
    ak_ball_t *src = &g->ball[origen];

    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < AK_MAX_BALLS; i++) {
            if (g->ball[i].alive) {
                continue;
            }
            ak_ball_t *b = &g->ball[i];
            *b = *src;
            b->alive = 1;
            b->held = 0;
            b->combo = 0;
            b->tn = 0;
            /* they open out in a fan by rotating the vector with the sine table */
            int a = (k == 0) ? 22 : -22;
            int cs = ak_cos(a & 0xFF), sn = ak_sin(a & 0xFF);
            int vx = src->vx ? src->vx : (int16_t)(g->speed / 2);
            int vy = src->vy ? src->vy : (int16_t)(-g->speed);
            b->vx = (int16_t)((vx * cs - vy * sn) / 256);
            b->vy = (int16_t)((vx * sn + vy * cs) / 256);
            break;
        }
    }
    if (src->held) {
        ball_launch(g, src);
    }
}

static void cap_take(ak_t *g, int kind)
{
    const ak_cap_def_t *d = ak_cap_def(kind);

    switch (kind) {
    case CAP_WIDE:
        g->pad_w_want = AK_PAD_W_MAX;
        g->t_wide = 700;
        break;
    case CAP_SLOW:
        g->t_slow = 500;
        speed_set(g, g->speed0 * 3 / 4);
        break;
    case CAP_MULTI:
        ball_split(g);
        break;
    case CAP_LASER:
        g->t_laser = 700;
        break;
    case CAP_CATCH:
        g->t_catch = 700;
        break;
    case CAP_LIFE:
        if (g->lives < 9) {
            g->lives++;
        }
        break;
    case CAP_POINTS:
    default:
        g->score += 500;
        break;
    }
    g->hud_dirty = 1;
    ak_sfx(1180, 40);
    ak_popup(g, UNFX(g->pad_x), AK_PAD_Y - 14, _(d->nombre), ak_rgb(d->color));
    ring_add(g, UNFX(g->pad_x), AK_PAD_Y, ak_rgb(d->color), 10);
}

static void caps_step(ak_t *g)
{
    for (int i = 0; i < AK_MAX_CAPS; i++) {
        ak_cap_t *c = &g->cap[i];
        if (!c->alive) {
            continue;
        }
        c->t++;
        c->y = (int16_t)(c->y + 22);

        int px = UNFX(c->x), py = UNFX(c->y);
        if (py + 4 >= AK_PAD_Y && py - 4 <= AK_PAD_Y + AK_PAD_H &&
            px >= UNFX(g->pad_x) - g->pad_w / 2 - 6 &&
            px <= UNFX(g->pad_x) + g->pad_w / 2 + 6) {
            c->alive = 0;
            cap_take(g, c->kind);
        } else if (py > AK_H + 6) {
            c->alive = 0;
        }
    }
}

static void shots_step(ak_t *g)
{
    for (int i = 0; i < AK_MAX_SHOTS; i++) {
        ak_shot_t *s = &g->shot[i];
        if (!s->alive) {
            continue;
        }
        s->y = (int16_t)(s->y - 88);
        int px = UNFX(s->x), py = UNFX(s->y);

        if (py < AK_FIELD_Y0) {
            s->alive = 0;
            continue;
        }
        int r = row_of(py), c = col_of(px);
        if (r >= 0 && r < AK_ROWS && c >= 0 && c < AK_COLS &&
            g->grid[r][c] != BK_NONE) {
            brick_damage(g, r, c, 1, 0, NULL);
            s->alive = 0;
        }
    }
}

static void shots_fire(ak_t *g)
{
    int px = UNFX(g->pad_x);
    int half = g->pad_w / 2 - 2;
    int lanzados = 0;

    for (int i = 0; i < AK_MAX_SHOTS && lanzados < 2; i++) {
        if (g->shot[i].alive) {
            continue;
        }
        g->shot[i].alive = 1;
        g->shot[i].x = FX(px + (lanzados == 0 ? -half : half));
        g->shot[i].y = FX(AK_PAD_Y - 2);
        lanzados++;
    }
    if (lanzados) {
        g->laser_cd = 9;
        ak_sfx(1700, 16);
    }
}

static void bits_step(ak_t *g)
{
    for (int i = 0; i < AK_MAX_BITS; i++) {
        ak_bit_t *b = &g->bit[i];
        if (!b->life) {
            continue;
        }
        b->life--;
        b->vy = (int16_t)(b->vy + 3);       /* gravity */
        b->x  = (int16_t)(b->x + b->vx);
        b->y  = (int16_t)(b->y + b->vy);
        if (UNFX(b->y) > AK_H || UNFX(b->x) < 0 || UNFX(b->x) >= AK_W) {
            b->life = 0;
        }
    }
}

/* --------------------------------------------------------------------------
 * Paddle and input
 * -------------------------------------------------------------------------- */

static void paddle_step(ak_t *g)
{
    /* the width changes by one pixel per frame: all at once it looks ugly and
     * it can also leave the ball inside the paddle */
    if (g->pad_w < g->pad_w_want) {
        g->pad_w++;
    } else if (g->pad_w > g->pad_w_want) {
        g->pad_w--;
    }

    int target = g->pad_x;

    if (g->autoplay) {
        /* The simulator's autopilot: it follows whichever descending ball is
         * lowest, offset a little so it does not always bounce the same. */
        int mejor = -1, mejor_y = -32768;
        for (int i = 0; i < AK_MAX_BALLS; i++) {
            ak_ball_t *b = &g->ball[i];
            if (!b->alive) {
                continue;
            }
            if (b->held || (b->vy > 0 && b->y > mejor_y)) {
                mejor_y = b->y;
                mejor = i;
            }
        }
        target = (mejor >= 0) ? g->ball[mejor].x : FX(AK_W / 2);
        /* It aims off centre, and the offset keeps changing: if the ball
         * always goes out vertically it bounces in the same column and the
         * test bench takes forever to empty a wall. */
        target += (int16_t)(((g->state_t / 23) % 21) - 10) * FX_ONE;
    } else if (g->control == CTRL_TILT) {
        /* The IMU hangs off the same I2C bus as the touch panel and polling it
         * in parallel with the touch driver has already saturated CPU 0 once
         * (it is written down in DECISIONES). At ten reads a second the paddle
         * responds just as well and the bus is left alone. */
        if (g->imu_skip <= 0) {
            aos_imu_t imu;
            g->imu_skip = 3;
            if (aos_hal_imu_read(&imu)) {
                /* Which of the two axes runs across the screen is said by
                 * g->tilt_axis, which is a stored setting. See the comment in
                 * arkanos.h: the mapping the code assumed was backwards and
                 * that was only found out with the board in hand. */
                float raw = (g->tilt_axis & 2) ? imu.ay : imu.ax;
                if (!g->tilt_zeroed) {
                    g->tilt_zero = raw;
                    g->tilt_zeroed = 1;
                }
                /* in milli-g: the only thing we ask floating point for */
                int mg = (int)((raw - g->tilt_zero) * 1000.0f);
                if (g->tilt_axis & 1) {
                    mg = -mg;
                }
                g->tilt_mx = (int16_t)clampi(mg, -2000, 2000);
            }
        }
        g->imu_skip--;

        int mx = g->tilt_mx;
        /* dead zone: without it the paddle never quite holds still */
        mx = (mx > 35) ? mx - 35 : (mx < -35) ? mx + 35 : 0;

        /* Absolute position and not velocity: with velocity, a barely
         * miscalibrated zero leaves the paddle drifting into the wall. This
         * way, a level board is always the centre. */
        target = FX(AK_W / 2) + clampi(mx, -230, 230) * FX(AK_W / 2 - 12) / 230;
    } else if (g->touching) {
        target = g->touch_x;
    }

    int lo = FX(AK_FIELD_X0 + g->pad_w / 2);
    int hi = FX(AK_FIELD_X1 - g->pad_w / 2);
    target = clampi(target, lo, hi);

    /* Speed cap: without it, a touch at the far end teleports the paddle and
     * the ball goes through it without touching. */
    int paso = clampi(target - g->pad_x, FX(-14), FX(14));
    g->pad_vx = (int16_t)paso;
    g->pad_x  = (int16_t)(g->pad_x + paso);
}

/* --------------------------------------------------------------------------
 * Levels and game
 * -------------------------------------------------------------------------- */

static void balls_reset(ak_t *g)
{
    memset(g->ball, 0, sizeof(g->ball));
    g->ball[0].alive = 1;
    g->ball[0].held = 1;
    g->ball[0].hold_off = 0;
    g->ball[0].x = g->pad_x;
    g->ball[0].y = FX(AK_PAD_Y - AK_BALL_R - 1);
}

void ak_load_level(ak_t *g, int level)
{
    const ak_level_t *lv;

    if (level >= ak_level_count()) {
        level = 0;
        g->lap++;
    }
    g->level = (uint8_t)level;
    lv = ak_level_get(level);

    memset(g->grid, 0, sizeof(g->grid));
    memset(g->hp, 0, sizeof(g->hp));
    memset(g->hit, 0, sizeof(g->hit));
    memset(g->cap, 0, sizeof(g->cap));
    memset(g->shot, 0, sizeof(g->shot));
    memset(g->bit, 0, sizeof(g->bit));
    memset(g->pop, 0, sizeof(g->pop));
    memset(g->ring, 0, sizeof(g->ring));

    g->left = 0;
    for (int r = 0; r < lv->nrows && r < AK_ROWS; r++) {
        for (int c = 0; c < AK_COLS; c++) {
            char ch = lv->rows[r][c];
            if (!ch) {
                break;
            }
            int kind = ak_brick_from_char(ch);
            g->grid[r][c] = (uint8_t)kind;
            g->hp[r][c] = ak_brick(kind)->hp;
            if (kind != BK_NONE && !(ak_brick(kind)->flags & AK_BF_SOLID)) {
                g->left++;
            }
        }
    }

    g->speed0 = (uint8_t)clampi(lv->speed + g->lap * 8, 32, 110);
    g->speed  = g->speed0;
    g->pad_w      = AK_PAD_W_STD;
    g->pad_w_want = AK_PAD_W_STD;
    g->pad_x      = FX(AK_W / 2);
    g->pad_vx     = 0;
    g->t_wide = g->t_laser = g->t_catch = g->t_slow = 0;
    g->laser_cd = 0;
    g->tilt_zeroed = 0;

    balls_reset(g);
    ak_bg_build(g);
    g->hud_dirty = 1;
    g->banner = 60;
    g->state = ST_READY;
    g->state_t = 0;
}

void ak_game_start(ak_t *g)
{
    g->score = 0;
    g->lives = 3;
    g->lap = 0;
    g->level = 0;
    ak_load_level(g, 0);
}

/* --------------------------------------------------------------------------
 * One frame
 * -------------------------------------------------------------------------- */

static void timers_step(ak_t *g)
{
    if (g->t_wide) {
        if (--g->t_wide == 0) {
            g->pad_w_want = AK_PAD_W_STD;
        }
    }
    if (g->t_laser) {
        g->t_laser--;
    }
    if (g->t_catch) {
        g->t_catch--;
    }
    if (g->t_slow) {
        if (--g->t_slow == 0) {
            speed_set(g, g->speed0);
        }
    }
    if (g->laser_cd > 0) {
        g->laser_cd--;
    }
    for (int r = 0; r < AK_ROWS; r++) {
        for (int c = 0; c < AK_COLS; c++) {
            if (g->hit[r][c]) {
                g->hit[r][c]--;
            }
        }
    }
    for (int i = 0; i < AK_MAX_POPS; i++) {
        if (g->pop[i].life) {
            g->pop[i].life--;
            if ((g->pop[i].life & 1) == 0) {
                g->pop[i].y--;
            }
        }
    }
    for (int i = 0; i < AK_MAX_RINGS; i++) {
        if (g->ring[i].life) {
            g->ring[i].life--;
        }
    }
}

static void life_lost(ak_t *g)
{
    ak_boom(g, UNFX(g->pad_x), AK_PAD_Y, 12, ak_rgb(0xFF4A3D));
    ak_sfx(120, 180);
    g->lives--;
    g->hud_dirty = 1;

    /* The upgrades are lost with the life, as in the original: otherwise a
     * wide paddle with a laser makes any mistake free. */
    g->t_wide = g->t_laser = g->t_catch = 0;
    g->pad_w_want = AK_PAD_W_STD;
    if (g->t_slow) {
        g->t_slow = 0;
        speed_set(g, g->speed0);
    }
    memset(g->cap, 0, sizeof(g->cap));
    memset(g->shot, 0, sizeof(g->shot));

    g->state = (g->lives <= 0) ? ST_OVER : ST_LOST;
    g->state_t = 0;
}

void ak_step(ak_t *g)
{
    g->state_t++;
    timers_step(g);
    bits_step(g);

    if (g->banner) {
        g->banner--;
    }

    switch (g->state) {
    case ST_READY:
        paddle_step(g);
        for (int i = 0; i < AK_MAX_BALLS; i++) {
            if (g->ball[i].alive) {
                ball_step(g, &g->ball[i]);
            }
        }
        /* the panel goes by itself, or sooner if the player has already
         * pressed */
        if (g->state_t > 46 || g->fire_edge) {
            g->fire_edge = 0;
            g->state = ST_PLAY;
            g->state_t = 0;
        }
        break;

    case ST_PLAY: {
        paddle_step(g);
        caps_step(g);
        shots_step(g);

        if (g->autoplay) {
            g->fire_edge = 1;
        }
        if (g->fire_edge) {
            g->fire_edge = 0;
            bool solto = false;
            for (int i = 0; i < AK_MAX_BALLS; i++) {
                if (g->ball[i].alive && g->ball[i].held) {
                    ball_launch(g, &g->ball[i]);
                    solto = true;
                }
            }
            if (!solto && g->t_laser && g->laser_cd <= 0) {
                shots_fire(g);
            }
        }

        int vivas = 0;
        for (int i = 0; i < AK_MAX_BALLS; i++) {
            if (!g->ball[i].alive) {
                continue;
            }
            ball_step(g, &g->ball[i]);
            if (g->ball[i].alive) {
                vivas++;
            } else {
                ak_sfx(200, 60);
            }
        }

        if (g->left == 0) {
            g->state = ST_CLEAR;
            g->state_t = 0;
            g->score += 1000 + g->lives * 250;
            g->hud_dirty = 1;
            ak_sfx(900, 60);
        } else if (vivas == 0) {
            life_lost(g);
        }
        break;
    }

    case ST_LOST:
        paddle_step(g);
        if (g->state_t == 12) {
            ak_sfx(300, 60);
        }
        if (g->state_t > 40) {
            balls_reset(g);
            g->state = ST_PLAY;
            g->state_t = 0;
        }
        break;

    case ST_CLEAR:
        paddle_step(g);
        caps_step(g);
        if (g->state_t == 10) {
            ak_sfx(1200, 60);
        } else if (g->state_t == 24) {
            ak_sfx(1500, 60);
        } else if (g->state_t == 38) {
            ak_sfx(1800, 120);
        }
        if (g->state_t > 70) {
            if (g->level + 1 >= ak_level_count()) {
                g->state = ST_WIN;
                g->state_t = 0;
            } else {
                ak_load_level(g, g->level + 1);
            }
        }
        break;

    case ST_OVER:
    case ST_WIN:
    case ST_TITLE:
    case ST_PAUSE:
    default:
        break;
    }

    if (g->score > g->hiscore) {
        g->hiscore = g->score;
    }
}
