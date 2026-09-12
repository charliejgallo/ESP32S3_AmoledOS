/*
 * TOPOS - the rules (see topos.h)
 *
 * Neither LVGL nor the HAL come in here: the harness plays tens of thousands
 * of frames of this with 'cc' and nothing else. Every time is in real
 * milliseconds (the app passes the frame's measured dt), so a frame the board
 * takes longer to draw does not make the 60 seconds last 70.
 */
#include "topos.h"

#include <string.h>

/* Phase lengths, ms */
#define TAUNT_MS        380     /* tongue out: the last chance to hit it     */
#define SINK_MS         160
#define HIT_MS          TP_HIT_MS   /* dizzy, before sinking                 */
#define HIT_SINK_MS     150
#define BONK_MS         260     /* startled, after the hat flies off         */
#define FIZZLE_MS       450
#define COOL_MS         260     /* an empty hole rests this long             */
#define COOL_BOOM_MS    900     /* and longer after a blast                  */
#define STUN_MS         700     /* the mallet is useless after a bomb        */
#define COUNT_STEP_MS   650
#define ENDING_MS       1600
#define MALLET_MS       240

#define CLASSIC_MS      60000
#define FRENZY_MS       30000
#define LEVEL_HITS      8       /* survival: moles per level                 */

/* Points */
#define PTS_MOLE        10
#define PTS_HELMET      20      /* the mole under the hat                    */
#define PTS_HAT         5       /* knocking the hat off                      */
#define PTS_GOLD        50
#define PTS_BOMB        25      /* lost, in the timed modes                  */

/* --------------------------------------------------------------------------
 * Random and difficulty
 * -------------------------------------------------------------------------- */

uint32_t tp_rand(tp_game_t *g)
{
    uint32_t x = g->rng ? g->rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

int tp_rand_range(tp_game_t *g, int lo, int hi)
{
    if (hi <= lo) {
        return lo;
    }
    return lo + (int)(tp_rand(g) % (uint32_t)(hi - lo + 1));
}

static int mini(int a, int b) { return a < b ? a : b; }
static int maxi(int a, int b) { return a > b ? a : b; }

static int lerp(int a, int b, int d)
{
    return a + (b - a) * d / 1000;
}

/* 0..1000. Each mode climbs by a different clock:
 *   classic    by the time played, all the way in its 60 seconds
 *   survival   by level, one step every LEVEL_HITS moles, flat at level 11
 *   frenzy     by the time played too, but starting a third of the way up */
int tp_difficulty(const tp_game_t *g)
{
    switch (g->mode) {
    case MODE_SURVIVAL:
        return mini(1000, (g->level - 1) * 100);
    case MODE_FRENZY:
        return mini(1000, 350 + (int)(g->elapsed_ms * 650u / FRENZY_MS));
    default:
        return mini(1000, (int)(g->elapsed_ms / (CLASSIC_MS / 1000)));
    }
}

typedef struct {
    int up_ms;          /* how long a mole waits before taunting             */
    int gap_ms;         /* between appearances                               */
    int rise_ms;
    int max_act;        /* at once                                           */
    int p_helmet;       /* per mille                                         */
    int p_bomb;
    int p_gold;
    int max_bombs;
} par_t;

/* Everything that makes the game harder, as a function of the difficulty.
 * With these numbers, in classic: bombs from second 5, hard hats from 7, two
 * moles at once from 20 and three from 40. */
static void params(const tp_game_t *g, par_t *p)
{
    int d = tp_difficulty(g);

    p->up_ms     = lerp(1450, 560, d);
    p->gap_ms    = lerp(1000, 340, d);
    p->rise_ms   = lerp(190, 110, d);
    p->max_act   = mini(3, 1 + d / 330);
    p->p_helmet  = d < 120 ? 0 : lerp(70, 300, d);
    p->p_bomb    = d < 80 ? 0 : lerp(60, 200, d);
    p->p_gold    = d < 150 ? 0 : 22;
    p->max_bombs = d < 500 ? 1 : 2;

    if (g->mode == MODE_FRENZY) {
        p->max_act += 1;
        p->gap_ms   = p->gap_ms * 3 / 4;
        p->p_gold   = 45;
        p->p_bomb  += 30;
    } else if (g->mode == MODE_SURVIVAL) {
        /* a bomb costs a heart here, not points: a few less of them */
        p->p_bomb = p->p_bomb * 3 / 4;
    }
}

/* --------------------------------------------------------------------------
 * Effects
 * -------------------------------------------------------------------------- */

static tp_fx_t *fx_add(tp_game_t *g, int kind, int x, int y)
{
    tp_fx_t *slot = NULL;
    for (int i = 0; i < TP_MAX_FX; i++) {
        if (g->fx[i].kind == FX_NONE) {
            slot = &g->fx[i];
            break;
        }
    }
    if (!slot) {
        /* all busy: the oldest one goes. It is always a puff nearly done. */
        slot = &g->fx[0];
        for (int i = 1; i < TP_MAX_FX; i++) {
            if (g->fx[i].t > slot->t) {
                slot = &g->fx[i];
            }
        }
    }
    memset(slot, 0, sizeof(*slot));
    slot->kind = (uint8_t)kind;
    slot->x    = (int16_t)x;
    slot->y    = (int16_t)y;
    slot->seed = (uint8_t)tp_rand(g);
    return slot;
}

static void popup(tp_game_t *g, int x, int y, int type, int value, int colour)
{
    tp_fx_t *f = fx_add(g, FX_POPUP, x, y);
    f->type = (uint8_t)type;
    f->a    = (int16_t)value;
    f->c    = (uint8_t)colour;
}

static int fx_life(int kind)
{
    switch (kind) {
    case FX_HELMET: return 1100;
    case FX_BOOM:   return 760;
    case FX_SMOKE:  return 800;
    case FX_POPUP:  return 800;
    case FX_RAYS:   return 140;
    case FX_CLANG:  return 180;
    case FX_DUST:   return 320;
    case FX_CRUMBS: return 480;
    default:        return 0;
    }
}

static void fx_step(tp_game_t *g, int dt)
{
    for (int i = 0; i < TP_MAX_FX; i++) {
        tp_fx_t *f = &g->fx[i];
        if (f->kind == FX_NONE) {
            continue;
        }
        f->t = (uint16_t)mini(f->t + dt, 60000);

        if (f->kind == FX_HELMET) {
            /* 1/16 px, velocities per 33 ms frame, gravity 3/16 px per frame */
            f->x  = (int16_t)(f->x + f->vx * dt / 33);
            f->y  = (int16_t)(f->y + f->vy * dt / 33);
            f->vy = (int16_t)(f->vy + 3 * dt / 33);
            f->a  = (int16_t)((f->a + (f->vx >= 0 ? 38 : -38) * dt / 33) & 0xFF);
            if (f->y > (TP_H + 24) * 16) {
                f->kind = FX_NONE;
                continue;
            }
        }
        if (f->t >= fx_life(f->kind)) {
            f->kind = FX_NONE;
        }
    }
}

/* --------------------------------------------------------------------------
 * Holes
 * -------------------------------------------------------------------------- */

static int hole_cx(int i) { return TP_COL_X(i % TP_COLS); }
static int hole_cy(int i) { return TP_ROW_Y(i / TP_COLS); }

static int travel(const tp_hole_t *h)
{
    return h->occ == OCC_BOMB ? TP_BOMB_RISE : TP_MOLE_RISE;
}

/* Something that can be hit (or, for a bomb, set off) right now. A rising
 * one counts from a third of the way out: waiting for it to be all the way up
 * felt like the tap not registering. */
static bool hittable(const tp_hole_t *h)
{
    if (h->occ == OCC_NONE) {
        return false;
    }
    switch (h->phase) {
    case PH_RISE:
        return h->rise >= travel(h) / 3;
    case PH_UP:
    case PH_TAUNT:
    case PH_BONK:
        return true;
    default:
        return false;
    }
}

/* Out and still a threat: what the spawner counts against max_act. */
static int active(const tp_game_t *g, int *bombs)
{
    int n = 0, nb = 0;
    for (int i = 0; i < TP_HOLES; i++) {
        const tp_hole_t *h = &g->holes[i];
        if (h->phase == PH_RISE || h->phase == PH_UP ||
            h->phase == PH_TAUNT || h->phase == PH_BONK) {
            n++;
            if (h->occ == OCC_BOMB) {
                nb++;
            }
        }
    }
    if (bombs) {
        *bombs = nb;
    }
    return n;
}

static void start_sink(tp_hole_t *h, int ms)
{
    h->phase     = PH_SINK;
    h->t         = 0;
    h->sink_from = h->rise;
    h->rise_ms   = (uint16_t)ms;        /* RISE and SINK: the move's length */
}

static void spawn(tp_game_t *g, const par_t *p)
{
    int cand[TP_HOLES], n = 0;
    for (int i = 0; i < TP_HOLES; i++) {
        if (g->holes[i].phase == PH_EMPTY && i != g->last_hole) {
            cand[n++] = i;
        }
    }
    if (n == 0) {
        for (int i = 0; i < TP_HOLES; i++) {
            if (g->holes[i].phase == PH_EMPTY) {
                cand[n++] = i;
            }
        }
    }
    if (n == 0) {
        return;
    }
    int i = cand[tp_rand(g) % (uint32_t)n];
    tp_hole_t *h = &g->holes[i];

    int bombs = 0;
    active(g, &bombs);
    int r = tp_rand_range(g, 0, 999);
    int occ;
    if (r < p->p_bomb && bombs < p->max_bombs) {
        occ = OCC_BOMB;
    } else if ((r -= p->p_bomb) < p->p_gold) {
        occ = OCC_GOLD;
    } else if ((r -= p->p_gold) < p->p_helmet) {
        occ = OCC_HELMET;
    } else {
        occ = OCC_MOLE;
    }

    memset(h, 0, sizeof(*h));
    h->occ     = (uint8_t)occ;
    h->phase   = PH_RISE;
    h->helmet  = occ == OCC_HELMET;
    h->seed    = (uint8_t)tp_rand(g);
    h->rise_ms = (uint16_t)p->rise_ms;

    int up = p->up_ms * tp_rand_range(g, 85, 115) / 100;
    if (occ == OCC_GOLD) {
        up = up * 7 / 10;               /* a prize has to be earned          */
    } else if (occ == OCC_HELMET) {
        up += 250;                      /* two taps need a little more time  */
    }
    h->up_ms = (uint16_t)up;
    if (occ == OCC_BOMB) {
        /* A bomb stays out longer than a mole: it is a hazard for a hurried
         * finger, and the red blinking at the end needs time to be seen. */
        h->fuse_ms = (uint16_t)(up + 700);
    }
    g->last_hole = (int8_t)i;

    fx_add(g, FX_CRUMBS, hole_cx(i), hole_cy(i) - 1);
    if (occ == OCC_BOMB) {
        tp_sfx(2400, 10);               /* a fuse catching                   */
    } else {
        tp_sfx(880, 8);                 /* a soft pop, for the corner of the eye */
    }
}

/* --------------------------------------------------------------------------
 * Scoring
 * -------------------------------------------------------------------------- */

static int mult_for(const tp_game_t *g)
{
    if (g->mode == MODE_FRENZY) {
        return 1 + mini(g->streak / 4, 4);          /* x5 at 16 in a row */
    }
    return 1 + mini(g->streak / 5, 3);              /* x4 at 15 in a row */
}

static void break_streak(tp_game_t *g)
{
    g->streak = 0;
    g->mult   = 1;
}

static void end_game(tp_game_t *g, int ev)
{
    g->state    = GS_ENDING;
    g->phase_ms = 0;
    g->events  |= (uint16_t)ev;

    /* Whatever is out celebrates: moles stick their tongues out, bombs go
     * out. Nothing counts from here on. */
    for (int i = 0; i < TP_HOLES; i++) {
        tp_hole_t *h = &g->holes[i];
        if (h->phase != PH_RISE && h->phase != PH_UP && h->phase != PH_BONK) {
            continue;
        }
        h->rise = (int8_t)travel(h);
        h->t    = 0;
        if (h->occ == OCC_BOMB) {
            h->phase = PH_FIZZLE;
        } else {
            h->phase = PH_TAUNT;
        }
    }
}

static void lose_life(tp_game_t *g)
{
    if (g->lives) {
        g->lives--;
    }
    g->flash_ms = 600;
    tp_sfx(330, 90);
    tp_sfx(220, 140);
    if (g->lives == 0) {
        end_game(g, EV_DEAD);
    }
}

/* Nobody hit it and it went home. */
static void escape(tp_game_t *g, int i)
{
    const tp_hole_t *h = &g->holes[i];
    if (g->state != GS_PLAY || h->occ == OCC_GOLD || h->occ == OCC_BOMB) {
        return;                     /* a prize that got away costs nothing */
    }
    g->escaped++;
    break_streak(g);
    if (g->mode == MODE_SURVIVAL) {
        lose_life(g);
    }
}

static void whack(tp_game_t *g, int i)
{
    tp_hole_t *h = &g->holes[i];
    const int cx = hole_cx(i);
    const int top = TP_HEAD_TOP(hole_cy(i), h->rise);

    g->hits++;
    g->streak++;
    if (g->streak > g->best_streak) {
        g->best_streak = g->streak;
    }
    int old_mult = g->mult;
    g->mult = (uint8_t)mult_for(g);

    int base = h->occ == OCC_GOLD ? PTS_GOLD :
               h->occ == OCC_HELMET ? PTS_HELMET : PTS_MOLE;
    int pts = base * g->mult;
    g->score += (uint32_t)pts;

    h->phase   = PH_HIT;
    h->t       = 0;
    h->was_hit = 1;

    fx_add(g, FX_RAYS, cx, top + 6);
    popup(g, cx, top - 6, POP_NUM, pts,
          h->occ == OCC_GOLD ? POPC_GOLD : POPC_WHITE);
    if (g->mult > old_mult) {
        popup(g, cx, top - 18, POP_MULT, g->mult, POPC_ORANGE);
        tp_sfx(1568, 40);
    }

    /* the thump, and a note that climbs with the streak */
    tp_sfx(190, 28);
    tp_sfx(700 + 70 * mini(g->streak, 12), 28);

    if (h->occ == OCC_GOLD) {
        g->golds++;
        tp_sfx(1047, 40);
        tp_sfx(1319, 40);
        tp_sfx(1568, 60);
        if (g->mode == MODE_SURVIVAL) {
            if (g->lives < TP_LIVES) {
                g->lives++;
                g->events |= EV_HEART;
            } else {
                g->score += PTS_GOLD;
            }
        } else {
            int bonus = g->mode == MODE_CLASSIC ? 3 : 2;
            g->time_ms += bonus * 1000;
            popup(g, cx, top - 18, POP_SECS, bonus, POPC_CYAN);
        }
    }

    if (g->mode == MODE_SURVIVAL) {
        int level = 1 + g->hits / LEVEL_HITS;
        if (level != g->level) {
            g->level  = (uint8_t)mini(level, 99);
            g->events |= EV_LEVEL;
        }
    }
}

static void bonk(tp_game_t *g, int i)
{
    tp_hole_t *h = &g->holes[i];
    const int cx = hole_cx(i);
    const int top = TP_HEAD_TOP(hole_cy(i), h->rise);

    h->helmet = 0;
    h->phase  = PH_BONK;
    h->t      = 0;
    /* the second tap needs time: never less than 900 ms from here */
    if (h->up_ms < h->up_t + 900) {
        h->up_ms = (uint16_t)(h->up_t + 900);
    }
    g->helmets++;
    g->score += (uint32_t)(PTS_HAT * g->mult);
    popup(g, cx + 14, top - 8, POP_NUM, PTS_HAT * g->mult, POPC_WHITE);

    /* The hat takes off from exactly where it was drawn (tp_draw.c puts it
     * at top + 2), so it does not jump on its first frame. */
    tp_fx_t *f = fx_add(g, FX_HELMET, cx * 16, (top + 2) * 16);
    int side = (tp_rand(g) & 1) ? 1 : -1;
    f->vx = (int16_t)(side * tp_rand_range(g, 12, 22));
    f->vy = (int16_t)(-tp_rand_range(g, 44, 56));

    fx_add(g, FX_CLANG, cx, top - 4);
    tp_sfx(1900, 18);
    tp_sfx(1250, 35);
}

static void explode(tp_game_t *g, int i)
{
    tp_hole_t *h = &g->holes[i];
    const int cx = hole_cx(i);
    const int bcy = TP_BOMB_CY(hole_cy(i), h->rise);

    memset(h, 0, sizeof(*h));
    h->phase   = PH_COOL;
    h->cool_ms = COOL_BOOM_MS;

    fx_add(g, FX_BOOM, cx, bcy - 2);
    g->bombs++;
    break_streak(g);
    g->stun_ms  = STUN_MS;
    g->flash_ms = 600;

    tp_sfx(150, 70);
    tp_sfx(105, 110);
    tp_sfx(75, 160);

    if (g->mode == MODE_SURVIVAL) {
        lose_life(g);
    } else {
        int loss = mini((int)g->score, PTS_BOMB);
        g->score -= (uint32_t)loss;
        popup(g, cx, bcy - 20, POP_NUM, -PTS_BOMB, POPC_RED);
        if (g->mode == MODE_FRENZY) {
            g->time_ms -= 2000;
        }
    }
}

static void hole_step(tp_game_t *g, int i, int dt)
{
    tp_hole_t *h = &g->holes[i];
    if (h->phase == PH_EMPTY) {
        return;
    }
    h->t = (uint16_t)mini(h->t + dt, 60000);
    const int tr = travel(h);

    switch (h->phase) {
    case PH_RISE: {
        /* ease-out to two pixels past the top, then settle: a pop, not a
         * slide */
        int u = mini(h->t * 256 / maxi(h->rise_ms, 1), 256);
        int r;
        if (u < 205) {
            int v = u * 256 / 205;
            r = (tr + 2) * (65536 - (256 - v) * (256 - v)) / 65536;
        } else {
            r = tr + 2 - 2 * (u - 205) / 51;
        }
        h->rise = (int8_t)r;
        if (h->t >= h->rise_ms) {
            h->rise  = (int8_t)tr;
            h->phase = PH_UP;
            h->t     = 0;
        }
        break;
    }
    case PH_UP:
        if (h->occ == OCC_BOMB) {
            h->fuse_t = (uint16_t)mini(h->fuse_t + dt, h->fuse_ms);
            if (h->fuse_t >= h->fuse_ms) {
                h->phase = PH_FIZZLE;
                h->t     = 0;
                tp_fx_t *f = fx_add(g, FX_SMOKE, hole_cx(i) + 10,
                                    TP_BOMB_CY(hole_cy(i), h->rise) - 20);
                (void)f;
                tp_sfx(420, 30);
                tp_sfx(300, 40);
            }
        } else {
            h->up_t = (uint16_t)mini(h->up_t + dt, 60000);
            if (h->up_t >= h->up_ms) {
                h->phase = PH_TAUNT;
                h->t     = 0;
                if (g->state == GS_PLAY) {
                    tp_sfx(660, 50);        /* nyah- */
                    tp_sfx(520, 70);        /*  -nyah */
                }
            }
        }
        break;
    case PH_BONK:
        h->up_t = (uint16_t)mini(h->up_t + dt, 60000);
        if (h->t >= BONK_MS) {
            h->phase = PH_UP;
            h->t     = 0;
        }
        break;
    case PH_TAUNT:
        if (h->t >= TAUNT_MS) {
            escape(g, i);
            start_sink(h, SINK_MS);
        }
        break;
    case PH_HIT:
        if (h->t >= HIT_MS) {
            start_sink(h, HIT_SINK_MS);
        }
        break;
    case PH_FIZZLE:
        if (h->t >= FIZZLE_MS) {
            start_sink(h, SINK_MS + 80);
        }
        break;
    case PH_SINK: {
        /* accelerating: it drops into the hole */
        int u = mini(h->t * 256 / maxi(h->rise_ms, 1), 256);
        h->rise = (int8_t)(h->sink_from * (65536 - u * u) / 65536);
        if (h->t >= h->rise_ms) {
            memset(h, 0, sizeof(*h));
            h->phase   = PH_COOL;
            h->cool_ms = COOL_MS;
        }
        break;
    }
    case PH_COOL:
        if (h->t >= h->cool_ms) {
            h->phase = PH_EMPTY;
            h->t     = 0;
        }
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * The game
 * -------------------------------------------------------------------------- */

void tp_game_init(tp_game_t *g, uint32_t seed)
{
    g->rng   = seed | 1u;
    g->state = GS_TITLE;
    g->mode  = MODE_CLASSIC;
    memset(g->holes, 0, sizeof(g->holes));
    memset(g->fx, 0, sizeof(g->fx));
    g->mallet    = 0;
    g->last_hole = -1;
    g->mult      = 1;
    g->lives     = TP_LIVES;
    g->level     = 1;
}

void tp_game_start(tp_game_t *g, int mode)
{
    g->mode      = (uint8_t)(mode >= 0 && mode < TP_MODES ? mode : MODE_CLASSIC);
    g->state     = GS_COUNT;
    g->phase_ms  = 0;
    g->count_num = 3;
    g->events   |= EV_COUNT;

    memset(g->holes, 0, sizeof(g->holes));
    memset(g->fx, 0, sizeof(g->fx));
    g->mallet      = 0;
    g->score       = 0;
    g->hits        = 0;
    g->streak      = 0;
    g->best_streak = 0;
    g->bombs       = 0;
    g->escaped     = 0;
    g->helmets     = 0;
    g->golds       = 0;
    g->mult        = 1;
    g->lives       = TP_LIVES;
    g->level       = 1;
    g->time_ms     = g->mode == MODE_CLASSIC ? CLASSIC_MS :
                     g->mode == MODE_FRENZY  ? FRENZY_MS  : 0;
    g->elapsed_ms  = 0;
    g->spawn_ms    = 0;
    g->stun_ms     = 0;
    g->flash_ms    = 0;
    g->last_hole   = -1;
    g->bot_ms      = 0;

    tp_sfx(660, 80);
}

void tp_game_step(tp_game_t *g, int dt)
{
    if (dt <= 0) {
        return;
    }
    if (dt > 100) {
        dt = 100;           /* a stall is not a reason to lose a mole */
    }

    if (g->state == GS_COUNT) {
        g->phase_ms = (uint16_t)(g->phase_ms + dt);
        int num = 3 - g->phase_ms / COUNT_STEP_MS;
        if (num <= 0) {
            g->state     = GS_PLAY;
            g->count_num = 0;
            g->events   |= EV_GO;
            g->spawn_ms  = 350;
            tp_sfx(1320, 150);
        } else if (num != g->count_num) {
            g->count_num = (uint8_t)num;
            g->events   |= EV_COUNT;
            tp_sfx(660, 80);
        }
        return;
    }
    if (g->state != GS_PLAY && g->state != GS_ENDING) {
        return;
    }

    for (int i = 0; i < TP_HOLES; i++) {
        hole_step(g, i, dt);
    }
    fx_step(g, dt);

    if (g->mallet) {
        g->mt = (uint16_t)(g->mt + dt);
        if (g->mt >= MALLET_MS) {
            g->mallet = 0;
        }
    }
    g->stun_ms  = (uint16_t)maxi(0, g->stun_ms - dt);
    g->flash_ms = (uint16_t)maxi(0, g->flash_ms - dt);

    if (g->state == GS_ENDING) {
        g->phase_ms = (uint16_t)mini(g->phase_ms + dt, 60000);
        if (g->phase_ms >= ENDING_MS) {
            g->state   = GS_OVER;
            g->events |= EV_OVER;
        }
        return;
    }

    /* --- playing --- */
    g->elapsed_ms += (uint32_t)dt;

    if (g->mode != MODE_SURVIVAL) {
        int32_t before = g->time_ms;
        g->time_ms -= dt;
        if (before > 10000 && g->time_ms <= 10000) {
            g->events |= EV_HURRY;
        }
        /* the last five seconds tick */
        if (g->time_ms > 0 && g->time_ms < 5000 &&
            before / 1000 != g->time_ms / 1000) {
            tp_sfx(1000, 20);
        }
        if (g->time_ms <= 0) {
            g->time_ms = 0;
            end_game(g, EV_TIMEUP);
            return;
        }
    }

    par_t p;
    params(g, &p);
    int act = active(g, NULL);

    g->spawn_ms -= dt;
    if (g->spawn_ms <= 0) {
        if (act < p.max_act) {
            spawn(g, &p);
            act++;
            /* frenzy comes in bursts */
            if (g->mode == MODE_FRENZY && act < p.max_act &&
                tp_rand_range(g, 0, 99) < 35) {
                spawn(g, &p);
            }
            g->spawn_ms = p.gap_ms * tp_rand_range(g, 75, 125) / 100;
        } else {
            g->spawn_ms = 60;
        }
    }
    /* never an empty lawn for long */
    if (act == 0 && g->spawn_ms > 250) {
        g->spawn_ms = 250;
    }
}

static int cell_at(int x, int y)
{
    /* top to bottom: where two cells overlap (4 px), the upper one wins,
     * which is the hole the finger is closer to */
    for (int r = 0; r < TP_ROWS; r++) {
        int cy = TP_ROW_Y(r);
        if (y < cy - TP_CELL_UP || y >= cy + TP_CELL_DN) {
            continue;
        }
        for (int c = 0; c < TP_COLS; c++) {
            int cx = TP_COL_X(c);
            if (x >= cx - TP_CELL_HW && x < cx + TP_CELL_HW) {
                return r * TP_COLS + c;
            }
        }
    }
    return -1;
}

void tp_game_tap(tp_game_t *g, int x, int y)
{
    if (g->state != GS_PLAY) {
        return;
    }
    g->mx     = (int16_t)x;
    g->my     = (int16_t)y;
    g->mt     = 0;
    g->mallet = 2;

    if (g->stun_ms) {
        fx_add(g, FX_DUST, x, y);
        tp_sfx(140, 25);
        return;
    }

    int i = cell_at(x, y);
    if (i < 0) {
        fx_add(g, FX_DUST, x, y);   /* off the field: nothing to lose */
        tp_sfx(180, 15);
        return;
    }
    tp_hole_t *h = &g->holes[i];
    if (!hittable(h)) {
        break_streak(g);
        fx_add(g, FX_DUST, x, y);
        tp_sfx(180, 20);
        return;
    }

    /* The mallet lands on what it hit, not where the finger was: a tap near
     * the edge of the cell still looks like a clean hit. */
    g->mallet = 1;
    const int cx = hole_cx(i), cy = hole_cy(i);
    g->mx = (int16_t)cx;
    if (h->occ == OCC_BOMB) {
        g->my = (int16_t)(TP_BOMB_CY(cy, h->rise) - 8);
        explode(g, i);
        return;
    }
    int top = TP_HEAD_TOP(cy, h->rise);
    g->my = (int16_t)(top + (h->helmet ? -4 : 2));
    if (h->helmet) {
        bonk(g, i);
    } else {
        whack(g, i);
    }
}

/* --------------------------------------------------------------------------
 * The bot
 *
 * Plays like a decent human: a reaction time, a target at a time, and it
 * gets distracted -moles escape- or clumsy -it hits a bomb, or taps an empty
 * hole- now and then, so that every path of the game is walked.
 * -------------------------------------------------------------------------- */

void tp_game_bot(tp_game_t *g, int dt)
{
    if (g->state != GS_PLAY) {
        return;
    }
    if (g->bot_ms > dt) {
        g->bot_ms = (uint16_t)(g->bot_ms - dt);
        return;
    }
    g->bot_ms = (uint16_t)tp_rand_range(g, 110, 260);

    int r = tp_rand_range(g, 0, 99);
    if (r < 6) {
        g->bot_ms = 700;                /* distracted */
        return;
    }

    int start = tp_rand_range(g, 0, TP_HOLES - 1);
    int target = -1, bomb = -1, empty = -1;
    for (int k = 0; k < TP_HOLES; k++) {
        int i = (start + k) % TP_HOLES;
        const tp_hole_t *h = &g->holes[i];
        if (hittable(h)) {
            if (h->occ == OCC_BOMB) {
                bomb = i;
            } else if (target < 0) {
                target = i;
            }
        } else if (h->phase == PH_EMPTY && empty < 0) {
            empty = i;
        }
    }
    if (r < 10 && bomb >= 0) {
        target = bomb;                  /* clumsy */
    } else if (r < 14 && empty >= 0) {
        target = empty;                 /* a miss */
    }
    if (target < 0) {
        return;
    }

    const tp_hole_t *h = &g->holes[target];
    int cx = hole_cx(target), cy = hole_cy(target);
    int y = h->occ == OCC_BOMB ? TP_BOMB_CY(cy, h->rise)
                               : TP_HEAD_TOP(cy, h->rise) + 12;
    if (h->occ == OCC_NONE) {
        y = cy - 4;
    }
    tp_game_tap(g, cx + tp_rand_range(g, -9, 9), y + tp_rand_range(g, -4, 4));
}
