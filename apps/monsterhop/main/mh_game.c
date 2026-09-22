/*
 * MONSTER HOP - the rules (see mh_game.h)
 */
#include "mh_game.h"
#include "mh_gfx.h"

#include <math.h>
#include <string.h>

#define FLOOR_M     0.50923f
#define HOP_T       0.18f
#define SUPER_T     0.36f
#define HOP_ARC     0.30f
#define SUPER_ARC   0.85f
#define PUSH_T      0.30f
#define USE_T       0.40f
#define BUMP_T      0.14f
#define DIE_T       1.30f
#define QUICK_T     0.70f       /* quicksand swallows who stays this long   */
#define SUPER_COOL  0.45f
#define INVULN_T    1.60f       /* after a respawn                          */
#define CRATE_T     0.22f

static const int DXv[4] = { 0, 1, 0, -1 };
static const int DYv[4] = { 1, 0, -1, 0 };

static float absf(float v) { return v < 0 ? -v : v; }

static void fx_add(mh_game_t *g, int kind, float x, float y, float z)
{
    int best = 0;
    float bt = -1;
    for (int i = 0; i < MH_MAX_FX; i++) {
        if (!g->fx[i].kind) { best = i; bt = 1e9f; break; }
        if (g->fx[i].t > bt) { bt = g->fx[i].t; best = i; }
    }
    mh_fx_t *f = &g->fx[best];
    f->kind = (uint8_t)kind;
    f->x = x;
    f->y = y;
    f->z = z;
    f->t = 0;
}

static void mark_dirty(mh_game_t *g, int x, int y)
{
    if (g->n_dirty < 16) {
        g->dirty[g->n_dirty][0] = x;
        g->dirty[g->n_dirty][1] = y;
        g->n_dirty++;
    }
}

/* ---- the level's static questions ---- */

static int crate_at(const mh_game_t *g, int x, int y)
{
    for (int i = 0; i < g->n_crate; i++) {
        if (!g->crate[i].sunk && g->crate[i].x == x && g->crate[i].y == y) return i;
    }
    return -1;
}

static int lever_at(const mh_game_t *g, int x, int y)
{
    for (int i = 0; i < g->n_lever; i++)
        if (g->lever[i].x == x && g->lever[i].y == y) return i;
    return -1;
}

static int chest_at(const mh_game_t *g, int x, int y)
{
    for (int i = 0; i < g->n_chest; i++)
        if (g->chest[i].x == x && g->chest[i].y == y) return i;
    return -1;
}

int mh_stand_floor(const mh_game_t *g, int x, int y, bool *solid)
{
    const mh_level_t *lv = g->lv;
    if (solid) *solid = false;
    if (!mh_in(lv, x, y)) {
        if (solid) *solid = true;
        return -9;
    }
    const mh_cell_t *c = mh_cell(lv, x, y);
    if ((c->flags & (CF_SOLID | CF_HIGH)) || lever_at(g, x, y) >= 0 || chest_at(g, x, y) >= 0 ||
        (x == g->exit_x && y == g->exit_y && !g->exit_open)) {
        if (solid) *solid = true;
        return -9;
    }
    int k = crate_at(g, x, y);
    if (k >= 0) return g->crate[k].z + 1;
    switch (c->kind) {
    case CK_GROUND:
    case CK_BRIDGE:
    case CK_QUICK:
        return c->h;
    default:
        return -9;
    }
}

/* ---- lanes, platforms, traps: functions of the clock ---- */

int mh_lane_movers(const mh_game_t *g, int li, float *pos, int max)
{
    const mh_lane_t *l = &g->lane[li];
    if (l->kind == LANE_LILY) {
        /* lilies do not move: one every other cell */
        int n = 0;
        for (int k = 0; k < l->len && n < max; k += 2) pos[n++] = (float)k;
        return n;
    }
    float period = (float)l->size + l->gap;
    float span = (float)l->len + period;
    float u = g->t / l->step;
    int n = 0;
    for (int k = 0; k < l->n && n < max; k++) {
        float p = fmodf(u + (float)k * period, span);
        pos[n++] = p - (float)l->size;      /* the mover's rear, from -size to len+gap */
    }
    return n;
}

/* world position of a point `along` cells into a lane */
static void lane_point(const mh_lane_t *l, float along, float *x, float *y)
{
    float fx = (float)l->x + 0.5f, fy = (float)l->y + 0.5f;
    *x = fx + DXv[l->dir] * along;
    *y = fy + DYv[l->dir] * along;
}

static bool lily_up(const mh_game_t *g, int li, int k, float *sink)
{
    const mh_lane_t *l = &g->lane[li];
    float per = l->step * 4.0f;            /* the cycle: 60 % up, 40 % going and gone */
    float ph = fmodf(g->t + (float)k * per * 0.37f + l->gap, per) / per;
    if (sink) *sink = ph < 0.6f ? 0 : (ph - 0.6f) / 0.4f;
    return ph < 0.78f;
}

static float path_len(const mh_level_t *lv, int pi)
{
    const mh_path_t *p = &lv->path[pi];
    float len = 0;
    for (int i = 0; i + 1 < p->n; i++) {
        len += absf((float)lv->pt[p->first + i + 1][0] - lv->pt[p->first + i][0]) +
               absf((float)lv->pt[p->first + i + 1][1] - lv->pt[p->first + i][1]);
    }
    if (!(p->flags & 1) && p->n > 1) {
        len += absf((float)lv->pt[p->first][0] - lv->pt[p->first + p->n - 1][0]) +
               absf((float)lv->pt[p->first][1] - lv->pt[p->first + p->n - 1][1]);
    }
    return len;
}

/* a position `s` cells along a path (loops, or back and forth) */
static void path_at(const mh_level_t *lv, int pi, float s, float *x, float *y)
{
    const mh_path_t *p = &lv->path[pi];
    if (p->n == 0) { *x = *y = 0; return; }
    if (p->n == 1) {
        *x = lv->pt[p->first][0] + 0.5f;
        *y = lv->pt[p->first][1] + 0.5f;
        return;
    }
    float L = path_len(lv, pi);
    if (L <= 0) L = 1;
    bool pp = (p->flags & 1) != 0;
    if (pp) {
        s = fmodf(s, 2 * L);
        if (s > L) s = 2 * L - s;
    } else {
        s = fmodf(s, L);
    }
    int n = pp ? p->n - 1 : p->n;
    for (int i = 0; i < n; i++) {
        int a = p->first + i, b = p->first + (i + 1) % p->n;
        float ax = lv->pt[a][0], ay = lv->pt[a][1], bx = lv->pt[b][0], by = lv->pt[b][1];
        float seg = absf(bx - ax) + absf(by - ay);
        if (s <= seg || i == n - 1) {
            float f = seg > 0 ? s / seg : 0;
            if (f > 1) f = 1;
            *x = ax + (bx - ax) * f + 0.5f;
            *y = ay + (by - ay) * f + 0.5f;
            return;
        }
        s -= seg;
    }
}

void mh_plat_pos(const mh_game_t *g, int pi, float t, float *x, float *y)
{
    const mh_plat_t *p = &g->plat[pi];
    float run = t;
    if (p->group && !(g->groups & (1u << p->group))) run = 0;   /* waits for its lever */
    path_at(g->lv, p->path, run / p->step, x, y);
}

bool mh_trap_active(const mh_game_t *g, int ti, float *phase)
{
    const mh_trap_t *tr = &g->trap[ti];
    float ph = fmodf(g->t + tr->phase, tr->period) / tr->period;
    if (phase) *phase = ph;
    switch (tr->kind) {
    case TRAP_SPIKES: return ph >= 0.5f && ph < 0.95f;
    case TRAP_VENT:   return ph >= 0.55f && ph < 0.85f;
    case TRAP_BEAR:   return !tr->sprung;
    default:          return false;
    }
}

/* ---- setting up ---- */

static void out(mh_game_t *g, int kind, int i)
{
    if (!g->link || g->n_out >= MH_MAX_OUT) return;
    g->out[g->n_out++] = (uint16_t)(kind << 8 | (i & 0xFF));
}

static void apply_groups(mh_game_t *g)
{
    mh_level_t *lv = g->lv;
    for (int i = 0; i < lv->n_ents; i++) {
        const mh_ent_def_t *e = &lv->ent[i];
        if (e->type != ENT_GROUPCELL || !mh_in(lv, e->x, e->y)) continue;
        /* a bridge deck that a lever puts over water or a pit (dir holds
         * what the cell is without it) */
        mh_cell_t *c = mh_cell(lv, e->x, e->y);
        bool on = (g->groups & (1u << e->a)) != 0;
        c->kind = on ? (uint8_t)CK_BRIDGE : e->dir;
        c->deck = on ? (uint8_t)e->p0 : 0;
        if (on) c->h = e->c;
        mark_dirty(g, e->x, e->y);
    }
}

static void place_hero(mh_game_t *g, int x, int y, int dir)
{
    mh_hero_t *h = &g->h;
    memset(h, 0, sizeof(*h));
    h->cx = h->tcx = x;
    h->cy = h->tcy = y;
    bool solid;
    int f = mh_stand_floor(g, x, y, &solid);
    h->floor = f < 0 ? 0 : f;
    h->x = x + 0.5f;
    h->y = y + 0.5f;
    h->z = h->floor * FLOOR_M;
    h->dir = dir;
    h->state = H_IDLE;
    h->ride = -1;
}

void mh_game_init(mh_game_t *g, mh_level_t *lv, int diff, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->lv = lv;
    g->diff = diff;
    g->seed = seed ? seed : 1;
    g->lives = diff == DIFF_EASY ? 5 : diff == DIFF_NORMAL ? 3 : 1;
    g->timer = diff != DIFF_EASY;
    g->time_left = (float)lv->time_s * (diff == DIFF_HARD ? 0.85f : 1.0f);
    g->cp = -1;
    g->start_x = lv->start_x;
    g->start_y = lv->start_y;
    g->exit_x = g->exit_y = -1;
    for (int i = 0; i < lv->n_ents; i++) {
        const mh_ent_def_t *e = &lv->ent[i];
        switch (e->type) {
        case ENT_KEY: case ENT_COIN: case ENT_HEART: case ENT_HOURGLASS: case ENT_STICKER:
            if (g->n_pick < MH_MAX_PICK) {
                mh_pick_t *p = &g->pick[g->n_pick++];
                p->type = e->type;
                p->x = e->x;
                p->y = e->y;
                p->z = e->z;
            }
            break;
        case ENT_CHEST:
            if (g->n_chest < MH_MAX_CHEST) {
                mh_chest_t *c = &g->chest[g->n_chest++];
                c->x = e->x; c->y = e->y; c->z = e->z; c->dir = e->dir;
                c->coins = e->a; c->bonus = e->b;
            }
            break;
        case ENT_CHECKPOINT:
            if (g->n_cp < MH_MAX_CP) {
                mh_cp_t *c = &g->cpt[g->n_cp++];
                c->x = e->x; c->y = e->y; c->z = e->z;
            }
            break;
        case ENT_EXIT:
            g->exit_x = e->x; g->exit_y = e->y; g->exit_z = e->z; g->exit_dir = e->dir;
            break;
        case ENT_LEVER:
            if (g->n_lever < MH_MAX_LEVER) {
                mh_lever_t *l = &g->lever[g->n_lever++];
                l->x = e->x; l->y = e->y; l->z = e->z; l->dir = e->dir; l->group = e->a;
            }
            break;
        case ENT_CRATE:
            if (g->n_crate < MH_MAX_CRATE) {
                mh_crate_t *c = &g->crate[g->n_crate++];
                c->x = e->x; c->y = e->y; c->z = e->z;
                c->mx = (float)e->x; c->my = (float)e->y;
                c->t = 1;
                c->art = e->a;
            }
            break;
        case ENT_PLATFORM:
            if (g->n_plat < MH_MAX_PLAT && e->a < lv->n_paths) {
                mh_plat_t *p = &g->plat[g->n_plat++];
                p->path = e->a;
                p->group = e->b;
                p->z = e->c;
                p->step = e->p0 ? e->p0 / 1000.0f : 0.8f;
            }
            break;
        case ENT_LANE:
            if (g->n_lane < MH_MAX_LANE) {
                mh_lane_t *l = &g->lane[g->n_lane++];
                l->kind = e->a; l->dir = e->dir; l->x = e->x; l->y = e->y; l->z = e->z;
                l->size = e->b ? e->b : 1;
                l->len = e->c ? e->c : 8;
                l->step = e->p0 ? e->p0 / 1000.0f : 0.4f;
                l->gap = (float)(e->p1 ? e->p1 : 3);
                if (l->kind == LANE_LILY) l->gap = (float)e->p1 / 1000.0f;
                float period = l->size + l->gap;
                l->n = (int)((l->len + period) / period) + 1;
            }
            break;
        case ENT_TRAP:
            if (g->n_trap < MH_MAX_TRAP) {
                mh_trap_t *t = &g->trap[g->n_trap++];
                t->kind = e->a; t->dir = e->dir; t->x = e->x; t->y = e->y; t->z = e->z;
                t->period = e->p0 ? e->p0 / 1000.0f : 2.0f;
                t->phase = e->p1 / 1000.0f;
                t->fired = -1;
            }
            break;
        case ENT_MONSTER:
            if (g->n_mon < MH_MAX_MON && e->a < MON_N) {
                mh_mon_t *m = &g->mon[g->n_mon++];
                m->kind = e->a;
                m->flags = e->c;
                m->dir = e->dir;
                m->path = e->b < lv->n_paths ? e->b : -1;
                m->step = e->p0 ? e->p0 / 1000.0f : 0.6f;
                m->param = e->p1 / 1000.0f;
                m->size = (m->kind == MON_BRUTE || m->kind == MON_PHARAOH || m->kind == MON_ALPHA) ? 2 : 1;
                float off = m->size == 2 ? 1.0f : 0.5f;
                m->x = e->x + off;
                m->y = e->y + off;
                m->z = e->z * FLOOR_M;
                m->ox = m->x;
                m->oy = m->y;
                m->home_x = e->x;
                m->home_y = e->y;
                m->home_dir = e->dir;
                m->gx = e->x;
                m->gy = e->y;
                m->sdir = 1;
                m->state = (m->kind == MON_WEREWOLF || m->kind == MON_ALPHA) ? M_IDLE :
                           m->kind == MON_CROW ? M_PERCH : M_WALK;
                m->pal = mh_rand(&g->seed);
                m->anim = mh_randf(&g->seed) * 4.0f;
            }
            break;
        default:
            break;
        }
    }
    apply_groups(g);
    g->n_dirty = 0;
    place_hero(g, lv->start_x, lv->start_y, lv->start_dir);
}

/* ---- Tommy ---- */

static void begin_hop(mh_game_t *g, int tx, int ty, int tf, bool super, float arc)
{
    mh_hero_t *h = &g->h;
    h->state = super ? H_SUPER : H_HOP;
    h->t = 0;
    h->dur = super ? SUPER_T : HOP_T;
    h->fx = h->x;
    h->fy = h->y;
    h->fz = h->z;
    h->tcx = tx;
    h->tcy = ty;
    h->tx = tx + 0.5f;
    h->ty = ty + 0.5f;
    h->tz = tf > -9 ? tf * FLOOR_M : h->z - 0.2f;
    h->arc = arc;
    h->ride = -1;
    g->events |= super ? EV_SUPER : EV_HOP;
    g->hops++;
}

static void bump(mh_game_t *g)
{
    mh_hero_t *h = &g->h;
    h->state = H_BUMP;
    h->t = 0;
    h->dur = BUMP_T;
    g->events |= EV_BUMP;
}

static bool busy(const mh_hero_t *h)
{
    return h->state != H_IDLE;
}

void mh_game_hop(mh_game_t *g, int dir)
{
    mh_hero_t *h = &g->h;
    if (g->state != GS_PLAY || dir < 0 || dir > 3) return;
    if (busy(h)) {
        if (h->state == H_HOP || h->state == H_SUPER || h->state == H_PUSH || h->state == H_USE || h->state == H_BUMP)
            h->queued = 1 + dir;
        return;
    }
    h->dir = dir;
    /* from a moving carrier the grid cell is where he is now */
    int cx = mh_iround(h->x - 0.5f), cy = mh_iround(h->y - 0.5f);
    int tx = cx + DXv[dir], ty = cy + DYv[dir];
    bool solid;
    int tf = mh_stand_floor(g, tx, ty, &solid);
    if (solid) { bump(g); return; }
    if (tf > -9 && tf - h->floor > 1) { bump(g); return; }
    if (tx == g->exit_x && ty == g->exit_y && !g->exit_open) { bump(g); return; }
    float rise = tf > -9 ? (float)(tf - h->floor) * FLOOR_M : 0;
    begin_hop(g, tx, ty, tf, false, HOP_ARC + (rise > 0 ? rise : 0));
}

void mh_game_action(mh_game_t *g)
{
    mh_hero_t *h = &g->h;
    if (g->state != GS_PLAY) return;
    if (busy(h)) {
        if (h->state == H_HOP || h->state == H_SUPER || h->state == H_PUSH || h->state == H_USE)
            h->queued = 5;
        return;
    }
    int d = h->dir;
    int cx = mh_iround(h->x - 0.5f), cy = mh_iround(h->y - 0.5f);
    int fx = cx + DXv[d], fy = cy + DYv[d];
    /* something to use in front? */
    int k = lever_at(g, fx, fy);
    if (k >= 0) {
        mh_lever_t *l = &g->lever[k];
        l->on = !l->on;
        l->t = 0;
        if (l->on) g->groups |= 1u << l->group;
        else g->groups &= ~(1u << l->group);
        apply_groups(g);
        out(g, OUT_LEVER, k);
        h->state = H_USE;
        h->t = 0;
        h->dur = USE_T;
        g->events |= EV_LEVER;
        return;
    }
    k = chest_at(g, fx, fy);
    if (k >= 0 && !g->chest[k].open) {
        mh_chest_t *c = &g->chest[k];
        c->open = true;
        c->t = 0;
        out(g, OUT_CHEST, k);
        g->coins += c->coins;
        if (c->bonus == ENT_HEART) { g->lives++; g->events |= EV_LIFE; }
        if (c->bonus == ENT_HOURGLASS && !g->link) { g->time_left += 30; g->events |= EV_TIME; }
        fx_add(g, FX_SPARKLE, fx + 0.5f, fy + 0.5f, c->z * FLOOR_M + 0.3f);
        h->state = H_USE;
        h->t = 0;
        h->dur = USE_T;
        g->events |= EV_CHEST;
        return;
    }
    k = crate_at(g, fx, fy);
    if (k >= 0 && g->crate[k].z == h->floor) {
        mh_crate_t *c = &g->crate[k];
        int nx = fx + DXv[d], ny = fy + DYv[d];
        bool solid;
        int nf = mh_stand_floor(g, nx, ny, &solid);
        bool hole = false;
        if (!solid && mh_in(g->lv, nx, ny)) {
            const mh_cell_t *nc = mh_cell(g->lv, nx, ny);
            hole = nc->kind == CK_WATER || nc->kind == CK_PIT;
            if (hole || (nf > -9 && nf <= c->z)) {
                c->mx = (float)c->x;
                c->my = (float)c->y;
                c->x = nx;
                c->y = ny;
                c->t = 0;
                if (hole) {
                    /* it falls in and is ground from now on, flush with the banks */
                    mh_cell_t *mc = mh_cell(g->lv, nx, ny);
                    c->z = mc->h - 1;
                    c->sunk = true;
                    g->events |= nc->kind == CK_WATER ? EV_SPLASHC : EV_LAND;
                    if (nc->kind == CK_WATER) fx_add(g, FX_SPLASH, nx + 0.5f, ny + 0.5f, mc->h * FLOOR_M - 0.18f);
                } else {
                    c->z = nf;
                }
                out(g, OUT_CRATE, k);
                h->state = H_PUSH;
                h->t = 0;
                h->dur = PUSH_T;
                g->events |= EV_PUSH;
                return;
            }
        }
        bump(g);
        return;
    }
    /* nothing to use: the super hop, two cells or two floors */
    if (h->cool > 0) return;
    h->cool = SUPER_COOL;
    for (int step = 2; step >= 1; step--) {
        int tx = cx + DXv[d] * step, ty = cy + DYv[d] * step;
        bool solid;
        int tf = mh_stand_floor(g, tx, ty, &solid);
        if (solid) continue;
        if (tf > -9 && tf - h->floor > 2) continue;
        if (tx == g->exit_x && ty == g->exit_y && !g->exit_open) continue;
        if (step == 2) {
            /* it flies over monsters, water, pits and crates; not over a
             * wall, a prop, or ground more than two floors up */
            bool ms;
            int mf = mh_stand_floor(g, cx + DXv[d], cy + DYv[d], &ms);
            if (ms || (mf > -9 && mf > h->floor + 2)) continue;
        }
        float rise = tf > -9 ? (float)(tf - h->floor) * FLOOR_M : 0;
        begin_hop(g, tx, ty, tf, true, SUPER_ARC + (rise > 0 ? rise : 0));
        return;
    }
    /* nowhere to go: a jump on the spot */
    begin_hop(g, cx, cy, h->floor, true, SUPER_ARC * 0.7f);
}

static void die(mh_game_t *g, int why)
{
    mh_hero_t *h = &g->h;
    if (g->state != GS_PLAY) return;
    g->state = GS_DYING;
    g->st = 0;
    h->die_why = why;
    h->t = 0;
    h->ride = -1;
    switch (why) {
    case DIE_WATER: case DIE_QUICK:
        h->state = H_SINK;
        g->events |= EV_SPLASH;
        fx_add(g, why == DIE_WATER ? FX_SPLASH : FX_BUBBLES, h->x, h->y, h->z);
        break;
    case DIE_PIT:
        h->state = H_FALL;
        g->events |= EV_FALL;
        break;
    default:
        h->state = H_HURT;
        g->events |= EV_HURT;
        fx_add(g, FX_POOF, h->x, h->y, h->z + 0.4f);
        break;
    }
    h->dur = DIE_T;
}

static void respawn(mh_game_t *g)
{
    int x = g->start_x, y = g->start_y, dir = g->lv->start_dir;
    if (g->cp >= 0) {
        x = g->cpt[g->cp].x;
        y = g->cpt[g->cp].y;
        dir = DIR_N;
    }
    place_hero(g, x, y, dir);
    g->h.cool = 0;
    g->state = GS_PLAY;
    g->st = 0;
    g->events |= EV_RESPAWN;
}

/* what carries Tommy at (x, y) now: a log, a lily, a platform */
static int carrier_at(mh_game_t *g, float x, float y, float *off)
{
    float pos[16];
    for (int li = 0; li < g->n_lane; li++) {
        const mh_lane_t *l = &g->lane[li];
        if (l->kind != LANE_LOG && l->kind != LANE_LILY) continue;
        /* along the lane's axis and across it */
        float fx = l->x + 0.5f, fy = l->y + 0.5f;
        float along = (x - fx) * DXv[l->dir] + (y - fy) * DYv[l->dir];
        float across = (x - fx) * DYv[l->dir] - (y - fy) * DXv[l->dir];
        if (absf(across) > 0.45f) continue;
        int n = mh_lane_movers(g, li, pos, 16);
        for (int k = 0; k < n; k++) {
            if (l->kind == LANE_LILY) {
                if (absf(along - pos[k]) < 0.45f && lily_up(g, li, k, NULL)) {
                    *off = along - pos[k];
                    return li * 64 + k;
                }
                continue;
            }
            /* a log covers [pos - 0.5, pos + size - 0.5] around cell centres */
            if (along >= pos[k] - 0.55f && along <= pos[k] + l->size - 0.45f) {
                *off = along - pos[k];
                return li * 64 + k;
            }
        }
    }
    for (int pi = 0; pi < g->n_plat; pi++) {
        float px, py;
        mh_plat_pos(g, pi, g->t, &px, &py);
        if (absf(px - x) < 0.5f && absf(py - y) < 0.5f) {
            *off = 0;
            return 1000 + pi;
        }
    }
    return -1;
}

int mh_game_carrier(mh_game_t *g, float x, float y)
{
    float off;
    return carrier_at(g, x, y, &off);
}

static void land(mh_game_t *g)
{
    mh_hero_t *h = &g->h;
    h->x = h->tx;
    h->y = h->ty;
    h->cx = h->tcx;
    h->cy = h->tcy;
    h->state = H_IDLE;
    h->t = 0;
    h->high = false;
    const mh_level_t *lv = g->lv;
    if (!mh_in(lv, h->cx, h->cy)) { die(g, DIE_PIT); return; }
    const mh_cell_t *c = mh_cell(lv, h->cx, h->cy);
    bool solid;
    int f = mh_stand_floor(g, h->cx, h->cy, &solid);
    if (f <= -9) {
        float off;
        int car = carrier_at(g, h->x, h->y, &off);
        if (car >= 0) {
            h->ride = car;
            h->ride_off = off;
            h->floor = c->h;
            /* on a log (its top 2 cm under the banks) or a lily pad (17 cm) */
            h->z = car >= 1000 ? g->plat[car - 1000].z * FLOOR_M
                 : c->h * FLOOR_M - (g->lane[car / 64].kind == LANE_LILY ? 0.17f : 0.02f);
            g->events |= EV_LAND;
            return;
        }
        if (c->kind == CK_WATER) die(g, DIE_WATER);
        else die(g, DIE_PIT);
        return;
    }
    h->floor = f;
    h->z = f * FLOOR_M;
    if (c->kind == CK_QUICK && crate_at(g, h->cx, h->cy) < 0) h->z -= 0.05f;
    g->events |= EV_LAND;
    fx_add(g, FX_DUST, h->x, h->y, h->z);
    h->quick_t = 0;
    /* pick-ups */
    for (int i = 0; i < g->n_pick; i++) {
        mh_pick_t *p = &g->pick[i];
        if (p->taken || p->x != h->cx || p->y != h->cy) continue;
        p->taken = true;
        p->t = 0;
        p->who = 0;
        p->at = g->t;
        switch (p->type) {
        case ENT_KEY:
            g->keys++;
            g->my_keys++;
            out(g, OUT_KEY, i);
            g->events |= EV_KEY;
            fx_add(g, FX_SPARKLE, h->x, h->y, h->z + 0.5f);
            if (g->keys >= MH_KEYS && !g->exit_open) {
                g->exit_open = true;
                g->exit_t = 0;
                g->events |= EV_OPEN;
            }
            break;
        case ENT_COIN: g->coins++; g->events |= EV_COIN; break;
        case ENT_HEART: g->lives++; g->events |= EV_LIFE; break;
        case ENT_HOURGLASS:
            if (!g->link) g->time_left += 30;
            g->events |= EV_TIME;
            break;
        case ENT_STICKER:
            g->sticker = true;
            g->events |= EV_STICKER;
            fx_add(g, FX_SPARKLE, h->x, h->y, h->z + 0.5f);
            break;
        default: break;
        }
    }
    for (int i = 0; i < g->n_cp; i++) {
        mh_cp_t *cp = &g->cpt[i];
        if (cp->x == h->cx && cp->y == h->cy && !cp->lit) {
            cp->lit = true;
            g->cp = i;
            g->events |= EV_CHECK;
        }
    }
    if (h->cx == g->exit_x && h->cy == g->exit_y && g->exit_open) {
        h->state = H_WIN;
        h->t = 0;
        h->dur = 1.2f;
        h->dir = DIR_S;
        g->state = GS_WON;
        g->st = 0;
        g->events |= EV_WIN;
    }
}

static void step_hero(mh_game_t *g, float dt)
{
    mh_hero_t *h = &g->h;
    if (h->cool > 0) h->cool -= dt;
    h->t += dt;
    switch (h->state) {
    case H_HOP:
    case H_SUPER: {
        float f = h->t / h->dur;
        if (f >= 1.0f) {
            land(g);
            break;
        }
        h->x = h->fx + (h->tx - h->fx) * f;
        h->y = h->fy + (h->ty - h->fy) * f;
        float base = h->fz + (h->tz - h->fz) * f;
        float up = 4.0f * h->arc * f * (1.0f - f);
        h->z = base + up;
        h->high = h->state == H_SUPER && up > 0.45f;
        /* he counts as being where he lands once past halfway */
        if (f > 0.5f) {
            h->cx = h->tcx;
            h->cy = h->tcy;
        }
        break;
    }
    case H_PUSH:
    case H_USE:
    case H_BUMP:
        if (h->t >= h->dur) {
            h->state = H_IDLE;
            h->t = 0;
        }
        break;
    case H_IDLE:
        if (h->ride >= 0) {
            float x, y;
            if (h->ride >= 1000) {
                int pi = h->ride - 1000;
                mh_plat_pos(g, pi, g->t, &x, &y);
                h->x = x;
                h->y = y;
            } else {
                int li = h->ride / 64, k = h->ride % 64;
                float pos[16];
                int n = mh_lane_movers(g, li, pos, 16);
                const mh_lane_t *l = &g->lane[li];
                if (k >= n) { die(g, DIE_WATER); break; }
                float along = pos[k] + h->ride_off;
                if (l->kind == LANE_LILY) {
                    float sink;
                    if (!lily_up(g, li, k, &sink)) { die(g, DIE_WATER); break; }
                    h->z = mh_cell(g->lv, h->cx, h->cy)->h * FLOOR_M - 0.17f - sink * 0.15f;
                }
                if (along < -0.5f || along > l->len - 0.5f) { die(g, DIE_WATER); break; }
                lane_point(l, along, &x, &y);
                h->x = x;
                h->y = y;
            }
            h->cx = mh_iround(h->x - 0.5f);
            h->cy = mh_iround(h->y - 0.5f);
            if (!mh_in(g->lv, h->cx, h->cy)) { die(g, DIE_WATER); break; }
        } else if (mh_in(g->lv, h->cx, h->cy)) {
            const mh_cell_t *c = mh_cell(g->lv, h->cx, h->cy);
            if (c->kind == CK_QUICK && crate_at(g, h->cx, h->cy) < 0) {
                h->quick_t += dt;
                h->z = c->h * FLOOR_M - 0.05f - h->quick_t * 0.25f;
                if (h->quick_t > QUICK_T) die(g, DIE_QUICK);
            }
        }
        break;
    default:
        break;
    }
    if (h->state == H_IDLE && h->queued && g->state == GS_PLAY) {
        int q = h->queued;
        h->queued = 0;
        if (q == 5) mh_game_action(g);
        else mh_game_hop(g, q - 1);
    }
}

/* ---- the monsters ---- */

static bool hero_vulnerable(const mh_game_t *g)
{
    const mh_hero_t *h = &g->h;
    if (g->state != GS_PLAY || h->high) return false;
    if (g->st < INVULN_T && g->lost > 0 && h->state != H_WIN) {
        /* just respawned */
        return false;
    }
    return true;
}

static bool clear_cell(const mh_game_t *g, int x, int y, int floor)
{
    bool solid;
    int f = mh_stand_floor(g, x, y, &solid);
    return !solid && f == floor;
}

/* the hero in line of sight along dir within range: its distance, or 0 */
static int sees(const mh_game_t *g, int x, int y, int dir, int range, int floor)
{
    const mh_hero_t *h = &g->h;
    int hx = h->cx, hy = h->cy;
    for (int k = 1; k <= range; k++) {
        int cx = x + DXv[dir] * k, cy = y + DYv[dir] * k;
        if (cx == hx && cy == hy) return k;
        if (!clear_cell(g, cx, cy, floor)) return 0;
    }
    return 0;
}

static void mon_walk(mh_game_t *g, mh_mon_t *m, float dt, float speed)
{
    if (m->path < 0) return;
    /* progress along the path is measured in cells */
    m->s += dt * speed / m->step;
    float px, py;
    const mh_path_t *p = &g->lv->path[m->path];
    path_at(g->lv, m->path, m->s, &px, &py);
    float off = m->size == 2 ? 0.5f : 0.0f;
    float ddx = px + off - m->x, ddy = py + off - m->y;
    if (absf(ddx) > absf(ddy)) {
        if (ddx > 0.001f) m->dir = DIR_E;
        else if (ddx < -0.001f) m->dir = DIR_W;
    } else {
        if (ddy > 0.001f) m->dir = DIR_N;
        else if (ddy < -0.001f) m->dir = DIR_S;
    }
    m->x = px + off;
    m->y = py + off;
    (void)p;
}

static void spawn_projectile(mh_game_t *g, float x, float y, int z, int dir, bool boulder)
{
    if (boulder) {
        for (int i = 0; i < MH_MAX_BOULDER; i++) {
            mh_boulder_t *b = &g->boulder[i];
            if (b->live) continue;
            b->live = true;
            b->x = x;
            b->y = y;
            b->z = z;
            b->dir = dir;
            b->roll = 0;
            return;
        }
        return;
    }
    for (int i = 0; i < MH_MAX_DART; i++) {
        mh_dart_t *d = &g->dart[i];
        if (d->live) continue;
        d->live = true;
        d->x = x;
        d->y = y;
        d->z = z;
        d->dir = dir;
        return;
    }
}

static float mon_height(int kind)
{
    switch (kind) {
    case MON_ZOMBIEDOG: return 0.5f;
    case MON_CROW: return 0.4f;
    case MON_BRUTE: case MON_ALPHA: case MON_PHARAOH: return 1.9f;
    default: return 1.1f;
    }
}

static void step_mon(mh_game_t *g, mh_mon_t *m, float dt)
{
    const mh_hero_t *h = &g->h;
    m->anim += dt;
    int cx = mh_ifloor(m->x), cy = mh_ifloor(m->y);
    int floor = mh_iround(m->z / FLOOR_M);
    switch (m->kind) {
    case MON_ZOMBIE:
    case MON_ZOMBIEDOG:
    case MON_ARMOR:
    case MON_MUMMY:
        if (m->state == M_WALK) {
            mon_walk(g, m, dt, 1.0f);
            if ((m->flags & MF_NOTICE) && hero_vulnerable(g) && sees(g, cx, cy, m->dir, 3, floor)) {
                m->state = M_NOTICE;
                m->dur = 0.35f;
                m->timer = 0;
            }
            if (m->param > 0 && (m->kind == MON_ARMOR || m->kind == MON_MUMMY || m->kind == MON_ZOMBIEDOG)) {
                /* pauses at every cell for armour, a push for mummies */
                m->timer += dt;
                float every = m->kind == MON_MUMMY ? m->param : m->param + 2.0f;
                if (m->timer > every) {
                    m->timer = 0;
                    m->state = m->kind == MON_MUMMY ? M_PUSHB : M_IDLE;
                    m->dur = m->kind == MON_MUMMY ? 0.5f : m->param;
                    if (m->kind == MON_MUMMY) {
                        spawn_projectile(g, cx + DXv[m->dir] + 0.5f, cy + DYv[m->dir] + 0.5f, floor, m->dir, true);
                    }
                }
            }
        } else if (m->state == M_NOTICE) {
            m->timer += dt;
            if (m->timer > m->dur) { m->state = M_LUNGE; m->timer = 0; }
        } else if (m->state == M_LUNGE) {
            mon_walk(g, m, dt, 3.5f);
            m->timer += dt;
            if (m->timer > 2.0f * m->step / 3.5f) { m->state = M_WALK; m->timer = 0; }
        } else {
            m->timer += dt;
            if (m->timer > m->dur) { m->state = M_WALK; m->timer = 0; }
        }
        break;
    case MON_VAMPIRE:
    case MON_COUNT:
        if (m->state == M_WALK) {
            mon_walk(g, m, dt, 1.0f);
            m->timer += dt;
            if (m->param > 0 && m->timer > m->param) {
                m->state = m->kind == MON_COUNT ? M_CAST : M_TRANSFORM;
                m->timer = 0;
                m->cx0 = cx;
                m->cy0 = cy;
                m->home_dir = m->dir;
            }
        } else if (m->state == M_TRANSFORM) {
            m->timer += dt;
            if (m->timer > 0.35f) { m->state = M_BAT; m->timer = 0; }
        } else if (m->state == M_BAT) {
            /* out four cells and back, flying over everything */
            m->timer += dt;
            float T = 2.4f, f = m->timer / T;
            if (f >= 1) { f = 1; m->state = M_UNTRANSFORM; m->timer = 0; }
            float s = 4.0f * (f < 0.5f ? f * 2 : (1 - f) * 2);
            m->x = m->cx0 + 0.5f + DXv[m->home_dir] * s;
            m->y = m->cy0 + 0.5f + DYv[m->home_dir] * s;
            m->dir = f < 0.5f ? m->home_dir : (m->home_dir + 2) & 3;
        } else if (m->state == M_UNTRANSFORM) {
            m->timer += dt;
            if (m->timer > 0.35f) { m->state = M_WALK; m->timer = 0; m->dir = m->home_dir; }
        } else if (m->state == M_CAST) {
            m->timer += dt;
            if (m->timer > 0.3f && m->dur == 0) {
                m->dur = 1;
                for (int d = 0; d < 4; d++) spawn_projectile(g, m->x, m->y, floor, d, false);
                g->events |= EV_CAST;
            }
            if (m->timer > 0.7f) { m->state = M_WALK; m->timer = 0; m->dur = 0; }
        }
        break;
    case MON_WEREWOLF:
    case MON_ALPHA: {
        int range = m->kind == MON_ALPHA ? 8 : 6;
        float run = m->kind == MON_ALPHA ? 0.10f : 0.12f;
        int size = m->size;
        int mx = mh_ifloor(m->x - (size == 2 ? 0.5f : 0)), my = mh_ifloor(m->y - (size == 2 ? 0.5f : 0));
        if (m->state == M_IDLE) {
            m->timer += dt;
            if (m->timer > 2.4f) {
                m->timer = 0;
                m->dir = (m->dir + 1) & 3;
            }
            if (hero_vulnerable(g) && sees(g, mx, my, m->dir, range, floor)) {
                m->state = M_HOWL;
                m->t = 0;
                g->events |= EV_HOWL;
            }
        } else if (m->state == M_HOWL) {
            m->t += dt;
            if (m->t > 0.7f) {
                m->state = M_RUN;
                m->t = 0;
                m->ox = m->x;
                m->oy = m->y;
            }
        } else if (m->state == M_RUN) {
            m->t += dt / run;
            while (m->t >= 1.0f) {
                m->t -= 1.0f;
                int nx = mh_ifloor(m->ox) + DXv[m->dir], ny = mh_ifloor(m->oy) + DYv[m->dir];
                if (!clear_cell(g, nx, ny, floor)) {
                    m->x = m->ox;
                    m->y = m->oy;
                    m->state = M_STUN;
                    m->t = 0;
                    g->events |= EV_BUMP;
                    break;
                }
                m->ox += DXv[m->dir];
                m->oy += DYv[m->dir];
            }
            if (m->state == M_RUN) {
                m->x = m->ox + DXv[m->dir] * m->t;
                m->y = m->oy + DYv[m->dir] * m->t;
            }
        } else if (m->state == M_STUN) {
            m->t += dt;
            if (m->t > (m->kind == MON_ALPHA ? 1.0f : 1.5f)) {
                m->state = M_HOME;
                m->t = 0;
                m->dir = (m->dir + 2) & 3;
            }
        } else if (m->state == M_HOME) {
            float off = size == 2 ? 1.0f : 0.5f;
            float hx = m->home_x + off, hy = m->home_y + off;
            float ddx = hx - m->x, ddy = hy - m->y;
            float dist = absf(ddx) + absf(ddy);
            float sp = dt / 0.35f;
            if (dist <= sp) {
                m->x = hx;
                m->y = hy;
                m->state = M_IDLE;
                m->dir = m->home_dir;
                m->timer = 0;
            } else {
                m->x += ddx / dist * sp;
                m->y += ddy / dist * sp;
            }
        }
        break;
    }
    case MON_CROW: {
        float hx = m->home_x + 0.5f, hy = m->home_y + 0.5f;
        float hz = mh_in(g->lv, m->home_x, m->home_y) ? mh_cell(g->lv, m->home_x, m->home_y)->h * FLOOR_M : 0;
        if (m->state == M_PERCH) {
            m->timer -= dt;
            float dx = h->x - hx, dy = h->y - hy;
            if (m->timer <= 0 && hero_vulnerable(g) && dx * dx + dy * dy < 2.6f * 2.6f) {
                m->state = M_RISE;
                m->t = 0;
            }
        } else if (m->state == M_RISE) {
            m->t += dt;
            m->z = hz + m->t / 0.3f * 1.0f;
            if (m->t > 0.3f) {
                m->state = M_DIVE;
                m->t = 0;
                m->ox = h->x;
                m->oy = h->y;
            }
        } else if (m->state == M_DIVE) {
            m->t += dt;
            float f = m->t / 0.6f;
            if (f > 1) f = 1;
            m->x = hx + (m->ox - hx) * f;
            m->y = hy + (m->oy - hy) * f;
            m->z = hz + 1.0f - 0.7f * f;
            float ddx = m->ox - hx, ddy = m->oy - hy;
            m->dir = absf(ddx) > absf(ddy) ? (ddx > 0 ? DIR_E : DIR_W) : (ddy > 0 ? DIR_N : DIR_S);
            if (f >= 1) { m->state = M_RETURN; m->t = 0; }
        } else if (m->state == M_RETURN) {
            m->t += dt;
            float f = m->t / 1.0f;
            if (f > 1) f = 1;
            m->x = m->ox + (hx - m->ox) * f;
            m->y = m->oy + (hy - m->oy) * f;
            m->z = hz + 0.3f + 0.7f * (1 - f) * f * 4;
            if (f >= 1) {
                m->state = M_PERCH;
                m->z = hz;
                m->timer = 2.0f;
                m->dir = m->home_dir;
            }
        }
        break;
    }
    case MON_BRUTE:
    case MON_PHARAOH:
        if (m->state == M_WALK) {
            mon_walk(g, m, dt, 1.0f);
            m->timer += dt;
            if (m->param > 0 && m->timer > m->param) {
                m->state = m->kind == MON_BRUTE ? M_STOMP : M_WHIP;
                m->timer = 0;
                m->t = 0;
            }
        } else {
            m->t += dt;
            if (m->kind == MON_BRUTE && m->t > 0.35f && m->dur == 0) {
                m->dur = 1;
                fx_add(g, FX_SHOCK, m->x, m->y, m->z);
                g->events |= EV_STOMP;
            }
            if (m->t > (m->kind == MON_BRUTE ? 0.9f : 0.8f)) {
                m->state = M_WALK;
                m->t = 0;
                m->dur = 0;
            }
        }
        break;
    default:
        break;
    }
    /* deadly contact */
    if (!hero_vulnerable(g)) return;
    float reach = m->size == 2 ? 1.25f : 0.55f;
    if (m->kind == MON_CROW && m->state != M_DIVE) return;
    if ((m->kind == MON_WEREWOLF || m->kind == MON_ALPHA) && m->state == M_STUN) return;
    float dx = absf(h->x - m->x), dy = absf(h->y - m->y);
    float mz = m->z + (m->state == M_BAT ? 0.6f : 0);
    float top = mz + (m->state == M_BAT ? 0.4f : mon_height(m->kind));
    if (dx < reach && dy < reach && h->z < top && h->z + 0.9f > mz) {
        die(g, DIE_MONSTER);
        return;
    }
    /* the pharaoh's whip: a strip two cells long in front of him */
    if (m->kind == MON_PHARAOH && m->state == M_WHIP && m->t > 0.3f && m->t < 0.6f) {
        float fx = h->x - m->x, fy = h->y - m->y;
        float along = fx * DXv[m->dir] + fy * DYv[m->dir];
        float across = fx * DYv[m->dir] - fy * DXv[m->dir];
        if (along > 0.8f && along < 3.2f && absf(across) < 1.1f) die(g, DIE_MONSTER);
    }
}

/* ---- lanes, traps, projectiles ---- */

static void step_hazards(mh_game_t *g, float dt)
{
    mh_hero_t *h = &g->h;
    bool vul = hero_vulnerable(g);
    float pos[16];
    for (int li = 0; li < g->n_lane; li++) {
        const mh_lane_t *l = &g->lane[li];
        if (l->kind == LANE_LOG || l->kind == LANE_LILY) continue;
        if (!vul) continue;
        int n = mh_lane_movers(g, li, pos, 16);
        float fx = l->x + 0.5f, fy = l->y + 0.5f;
        float along = (h->x - fx) * DXv[l->dir] + (h->y - fy) * DYv[l->dir];
        float across = (h->x - fx) * DYv[l->dir] - (h->y - fy) * DXv[l->dir];
        float lz = l->z * FLOOR_M + (l->kind == LANE_BAT ? 0.6f : 0);
        if (absf(across) > 0.42f || h->z > lz + 0.9f || h->z + 0.9f < lz) continue;
        for (int k = 0; k < n; k++) {
            /* a mover spans [pos - 0.4, pos + size - 0.6] around cell centres */
            if (along > pos[k] - 0.42f && along < pos[k] + l->size - 0.58f) {
                die(g, DIE_MONSTER);
                return;
            }
        }
    }
    for (int ti = 0; ti < g->n_trap; ti++) {
        mh_trap_t *tr = &g->trap[ti];
        float ph;
        bool act = mh_trap_active(g, ti, &ph);
        if (tr->kind == TRAP_DARTS) {
            /* one dart a period, at the start of it */
            float cycle = mh_ifloor((g->t + tr->phase) / tr->period);
            if (cycle != tr->fired) {
                tr->fired = cycle;
                spawn_projectile(g, tr->x + DXv[tr->dir] + 0.5f, tr->y + DYv[tr->dir] + 0.5f, tr->z, tr->dir, false);
            }
            continue;
        }
        bool on_it = h->cx == tr->x && h->cy == tr->y && h->state != H_HOP && h->state != H_SUPER;
        if (tr->kind == TRAP_BEAR) {
            if (on_it && !tr->sprung && vul) {
                tr->sprung = true;
                die(g, DIE_TRAP);
                return;
            }
            continue;
        }
        if (act && on_it && vul) {
            die(g, DIE_TRAP);
            return;
        }
    }
    /* darts and bats in flight */
    for (int i = 0; i < MH_MAX_DART; i++) {
        mh_dart_t *d = &g->dart[i];
        if (!d->live) continue;
        float sp = 7.0f * dt;
        d->x += DXv[d->dir] * sp;
        d->y += DYv[d->dir] * sp;
        int cx = mh_ifloor(d->x), cy = mh_ifloor(d->y);
        if (!mh_in(g->lv, cx, cy)) { d->live = false; continue; }
        bool solid;
        int f = mh_stand_floor(g, cx, cy, &solid);
        if (solid || (f > -9 && f > d->z)) { d->live = false; continue; }
        if (vul && absf(h->x - d->x) < 0.4f && absf(h->y - d->y) < 0.4f &&
            h->z < d->z * FLOOR_M + 0.9f && h->z + 0.9f > d->z * FLOOR_M + 0.3f) {
            d->live = false;
            die(g, DIE_MONSTER);
            return;
        }
    }
    for (int i = 0; i < MH_MAX_BOULDER; i++) {
        mh_boulder_t *b = &g->boulder[i];
        if (!b->live) continue;
        float sp = dt / 0.25f;
        b->x += DXv[b->dir] * sp;
        b->y += DYv[b->dir] * sp;
        b->roll += sp;
        int cx = mh_ifloor(b->x), cy = mh_ifloor(b->y);
        bool solid;
        int f = mh_stand_floor(g, cx, cy, &solid);
        if (!mh_in(g->lv, cx, cy) || solid || f != b->z) { b->live = false; fx_add(g, FX_DUST, b->x, b->y, b->z * FLOOR_M); continue; }
        if (vul && absf(h->x - b->x) < 0.55f && absf(h->y - b->y) < 0.55f && h->z < b->z * FLOOR_M + 0.9f) {
            die(g, DIE_MONSTER);
            return;
        }
    }
    /* the brute's shockwave: a ring on the ground */
    for (int i = 0; i < MH_MAX_FX; i++) {
        mh_fx_t *f = &g->fx[i];
        if (f->kind != FX_SHOCK || f->t > 0.8f || !vul) continue;
        float r = 1.0f + f->t / 0.8f * 3.0f;
        float dx = h->x - f->x, dy = h->y - f->y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (absf(dist - r) < 0.45f && h->z - h->floor * FLOOR_M < 0.15f) {
            die(g, DIE_MONSTER);
            return;
        }
    }
}

void mh_game_step(mh_game_t *g, float dt)
{
    if (dt > 0.05f) dt = 0.05f;
    g->st += dt;
    if (g->state == GS_PLAY || g->state == GS_DYING) g->t += dt;
    for (int i = 0; i < MH_MAX_FX; i++) {
        if (g->fx[i].kind) {
            g->fx[i].t += dt;
            if (g->fx[i].t > 1.2f) g->fx[i].kind = 0;
        }
    }
    for (int i = 0; i < g->n_pick; i++) if (g->pick[i].taken) g->pick[i].t += dt;
    for (int i = 0; i < g->n_lever; i++) g->lever[i].t += dt;
    for (int i = 0; i < g->n_chest; i++) g->chest[i].t += dt;
    for (int i = 0; i < g->n_crate; i++) {
        mh_crate_t *c = &g->crate[i];
        if (c->t < 1) {
            c->t += dt / CRATE_T;
            if (c->t >= 1) {
                c->t = 1;
                if (c->sunk) {
                    /* it is ground now: the cache draws it */
                    mh_cell_t *mc = mh_cell(g->lv, c->x, c->y);
                    mc->kind = CK_GROUND;
                    mc->top = 0;
                    mc->prop = (uint8_t)c->art;
                    mc->flags |= CF_ORIGIN | CF_SUNK;
                    mark_dirty(g, c->x, c->y);
                }
            }
        }
    }
    if (g->exit_open) g->exit_t += dt;
    if (g->link && (g->state == GS_PLAY || g->state == GS_DYING)) {
        /* a race runs on the level's clock alone, the same on both watches:
         * dying does not stop it and hourglasses do not add to it */
        g->time_left = (float)g->lv->time_s - g->t;
        if (g->time_left <= 0) {
            g->time_left = 0;
            g->state = GS_OVER;
            g->st = 0;
            g->events |= EV_TIMEUP | EV_OVER;
        }
    }
    switch (g->state) {
    case GS_PLAY:
        if (g->timer && !g->link) {
            float before = g->time_left;
            g->time_left -= dt;
            if (before > 10 && g->time_left <= 10) g->events |= EV_LOW_TIME;
            if (g->time_left <= 0) {
                g->time_left = 0;
                g->events |= EV_TIMEUP;
                if (g->link) {
                    /* the race ends with the clock: the keys decide */
                    g->state = GS_OVER;
                    g->st = 0;
                    g->events |= EV_OVER;
                    break;
                }
                die(g, DIE_TIME);
                break;
            }
        }
        step_hero(g, dt);
        if (g->state != GS_PLAY) break;
        for (int i = 0; i < g->n_mon && g->state == GS_PLAY; i++) step_mon(g, &g->mon[i], dt);
        if (g->state == GS_PLAY) step_hazards(g, dt);
        break;
    case GS_DYING:
        g->h.t += dt;
        for (int i = 0; i < g->n_mon; i++) step_mon(g, &g->mon[i], dt);
        if (g->st > DIE_T) {
            if (!g->link) g->lives--;
            g->lost++;
            if (g->h.die_why == DIE_TIME) g->time_left += 60;
            if (g->lives <= 0) {
                g->lives = 0;
                g->state = GS_OVER;
                g->st = 0;
                g->events |= EV_OVER;
            } else {
                respawn(g);
            }
        }
        break;
    case GS_WON:
        g->h.t += dt;
        break;
    default:
        break;
    }
}

int mh_game_stars(const mh_game_t *g)
{
    if (g->state != GS_WON) return 0;
    int s = 1;
    float used = g->t;
    if (used <= (float)g->lv->par_s) {
        s = 2;
        if (g->lost == 0) s = 3;
    }
    return s;
}

bool mh_game_target(const mh_game_t *g, float *x, float *y)
{
    if (g->exit_open) {
        *x = g->exit_x + 0.5f;
        *y = g->exit_y + 0.5f;
        return g->exit_x >= 0;
    }
    float best = 1e9f;
    bool any = false;
    for (int i = 0; i < g->n_pick; i++) {
        const mh_pick_t *p = &g->pick[i];
        if (p->type != ENT_KEY || p->taken) continue;
        float dx = p->x + 0.5f - g->h.x, dy = p->y + 0.5f - g->h.y;
        float d = dx * dx + dy * dy;
        if (d < best) {
            best = d;
            *x = p->x + 0.5f;
            *y = p->y + 0.5f;
            any = true;
        }
    }
    return any;
}

/* ---- the link ---- */

static void open_exit(mh_game_t *g)
{
    if (g->keys >= MH_KEYS && !g->exit_open) {
        g->exit_open = true;
        g->exit_t = 0;
        g->events |= EV_OPEN;
    }
}

void mh_game_rival_key(mh_game_t *g, int i, float at)
{
    if (i < 0 || i >= g->n_pick || g->pick[i].type != ENT_KEY) return;
    mh_pick_t *p = &g->pick[i];
    if (!p->taken) {
        p->taken = true;
        p->t = 0;
        p->who = 1;
        p->at = at;
        g->keys++;
        g->events |= EV_RIVAL_KEY;
        fx_add(g, FX_POOF, p->x + 0.5f, p->y + 0.5f, p->z * FLOOR_M + 0.3f);
        open_exit(g);
        return;
    }
    /* both took it: the earlier keeps it, the host on a tie (the other
     * watch decides the same way with the same two times) */
    if (p->who == 0 && (at < p->at || (at == p->at && !g->host))) {
        p->who = 1;
        p->at = at;
        g->my_keys--;
        g->events |= EV_KEYLOST;
    }
}

void mh_game_rival_lever(mh_game_t *g, int k, bool on)
{
    if (k < 0 || k >= g->n_lever) return;
    mh_lever_t *l = &g->lever[k];
    if (l->on == on) return;
    l->on = on;
    l->t = 0;
    if (on) g->groups |= 1u << l->group;
    else g->groups &= ~(1u << l->group);
    apply_groups(g);
    g->events |= EV_LEVER;
}

void mh_game_rival_crate(mh_game_t *g, int k, int x, int y, int z, bool sunk)
{
    if (k < 0 || k >= g->n_crate) return;
    mh_crate_t *c = &g->crate[k];
    if (c->sunk || (c->x == x && c->y == y)) return;
    c->mx = (float)c->x;
    c->my = (float)c->y;
    c->x = x;
    c->y = y;
    c->z = z;
    c->t = 0;
    c->sunk = sunk;
}

void mh_game_rival_chest(mh_game_t *g, int k)
{
    if (k < 0 || k >= g->n_chest || g->chest[k].open) return;
    g->chest[k].open = true;
    g->chest[k].t = 0;
}

void mh_game_rival_exit(mh_game_t *g, float at)
{
    g->rival_out = true;
    g->rival_out_t = at;
}

static bool first_out(const mh_game_t *g, bool rival)
{
    bool me = g->state == GS_WON;
    if (rival) {
        if (!g->rival_out) return false;
        if (!me) return true;
        return g->rival_out_t < g->t || (g->rival_out_t == g->t && !g->host);
    }
    if (!me) return false;
    if (!g->rival_out) return true;
    return g->t < g->rival_out_t || (g->t == g->rival_out_t && g->host);
}

int mh_game_points(const mh_game_t *g, bool rival)
{
    int k = 0;
    for (int i = 0; i < g->n_pick; i++)
        if (g->pick[i].type == ENT_KEY && g->pick[i].taken && g->pick[i].who == (rival ? 1 : 0)) k++;
    return k + (first_out(g, rival) ? 2 : 0);
}

bool mh_game_race_over(const mh_game_t *g)
{
    return g->state == GS_WON || g->state == GS_OVER || g->rival_out;
}
