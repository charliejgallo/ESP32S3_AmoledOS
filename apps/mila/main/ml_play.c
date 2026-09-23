/*
 * MILA - a level being played (see ml_play.h)
 */
#include "ml_play.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define WALK_S      0.20f       /* one step                                 */
#define PUSH_S      0.26f       /* one step pushing                         */
#define SLIDE_S     0.09f       /* each further cell of a slide              */
#define ROLL_S      0.075f      /* each further cell of a roll               */
#define FALL_S      0.30f
#define BUMP_S      0.14f
#define YAWN_AFTER  7.0f

static void load_art(ml_things_art_t *a, const char *kit)
{
    char nm[48];
#define L(field, what) do { snprintf(nm, sizeof nm, "%s_%s", kit, what); if (ml_art_has(nm)) ml_art_load(nm, &a->field); } while (0)
    L(obj, "obj");
    L(obj_sh, "obj_sh");
    L(obj_on, "obj_on");
    L(ball, "ball");
    L(ball_sh, "ball_sh");
    L(ball_roll[0], "ball_roll_v0");
    L(ball_roll[1], "ball_roll_v1");
    L(gate_h, "gate_h");
    L(gate_v, "gate_v");
    L(gate_h_sh, "gate_h_sh");
    L(gate_v_sh, "gate_v_sh");
    L(flap_h, "flap_h");
    L(flap_v, "flap_v");
#undef L
}

static void free_art(ml_things_art_t *a)
{
    ml_anim_t *all[] = { &a->obj, &a->obj_sh, &a->obj_on, &a->ball, &a->ball_sh, &a->ball_roll[0],
                         &a->ball_roll[1], &a->gate_h, &a->gate_v, &a->flap_h, &a->flap_v,
                         &a->gate_h_sh, &a->gate_v_sh };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) ml_anim_free(all[i]);
}

static bool wall_at(const ml_map_t *m, int c, int d)
{
    int q = ml_next(m, c, d);
    if (q < 0) return true;
    int t = C_TERR(m->cell[q]);
    return t == T_WALL || t == T_VOID;
}

bool ml_play_init(ml_play_t *p, const ml_level_t *lv, ml_world_t *w, const ml_mila_t *mila,
                  const char *kit)
{
    memset(p, 0, sizeof(*p));
    p->lv = lv;
    p->w = w;
    p->mila = mila;
    p->st = lv->start;
    p->face = MD_S;
    p->anim = MA_IDLE;
    load_art(&p->art, kit);
    const ml_map_t *m = &lv->map;
    for (int c = 0; c < ML_CELLS; c++) {
        int t = C_TERR(m->cell[c]);
        if (t == T_GATE && p->ngates < ML_MAXGATES) {
            p->gate_cell[p->ngates] = (uint8_t)c;
            p->gate_h[p->ngates] = wall_at(m, c, D_LEFT) && wall_at(m, c, D_RIGHT);
            p->gate_amt[p->ngates] = ml_gate_open(m, &p->st, c) ? 1.0f : 0.0f;
            p->ngates++;
        }
        if ((t == T_FLAP_V || t == T_FLAP_H) && p->nflaps < ML_MAXGATES) {
            p->flap_cell[p->nflaps] = (uint8_t)c;
            p->flap_t[p->nflaps] = -1;
            p->nflaps++;
        }
    }
    return true;
}

void ml_play_free(ml_play_t *p)
{
    free_art(&p->art);
}

/* ---- input ---- */

void ml_play_push_dir(ml_play_t *p, int d)
{
    if (p->qw - p->qr >= ML_QUEUE) return;
    p->q[p->qw % ML_QUEUE] = (uint8_t)d;
    p->qw++;
}

bool ml_play_walk_to(ml_play_t *p, int cell)
{
    const ml_map_t *m = &p->lv->map;
    ml_state_t s = p->st;
    int from = s.mila;
    if (cell == from) return false;
    int16_t prev[ML_CELLS];
    uint8_t pdir[ML_CELLS];
    for (int i = 0; i < ML_CELLS; i++) prev[i] = -1;
    int qa[ML_CELLS], h = 0, t = 0;
    qa[t++] = from;
    prev[from] = (int16_t)from;
    while (h < t) {
        int c = qa[h++];
        if (c == cell) break;
        for (int d = 0; d < 4; d++) {
            int q = ml_next(m, c, d);
            if (q < 0 || prev[q] >= 0) continue;
            s.mila = (uint8_t)c;
            if (!ml_mila_can(m, &s, c, q, d) || ml_thing_at(m, &s, q) >= 0) continue;
            prev[q] = (int16_t)c;
            pdir[q] = (uint8_t)d;
            qa[t++] = q;
        }
    }
    if (prev[cell] < 0) return false;
    uint8_t path[ML_CELLS];
    int n = 0;
    for (int c = cell; c != from && n < ML_CELLS; c = prev[c]) path[n++] = pdir[c];
    /* the queue is replaced: a new tap changes the plan */
    p->qr = p->qw;
    for (int i = n - 1; i >= 0; i--) ml_play_push_dir(p, path[i]);
    return true;
}

void ml_play_undo(ml_play_t *p)
{
    p->want_undo = true;
}

void ml_play_restart(ml_play_t *p)
{
    p->want_restart = true;
}

/* ---- stepping ---- */

bool ml_play_busy(const ml_play_t *p)
{
    if (p->mt.on || p->bump_t > 0) return true;
    for (int i = 0; i < ML_MAXT; i++)
        if (p->th[i].on) return true;
    return false;
}

static void settle(ml_play_t *p)
{
    memset(&p->mt, 0, sizeof p->mt);
    memset(p->th, 0, sizeof p->th);
    p->bump_t = 0;
    ml_world_sync(p->w);
}

static void do_undo(ml_play_t *p)
{
    if (!p->nhist || p->won) return;
    const ml_hist_t *h = &p->hist[--p->nhist];
    p->st = h->st;
    p->pushes = h->pushes;
    p->face = h->face;
    if (p->moves > 0) p->moves--;
    p->anim = MA_IDLE;
    p->anim_t = 0;
    p->qr = p->qw;
    settle(p);
    p->events |= PE_UNDO;
}

static void do_restart(ml_play_t *p)
{
    if (p->won) return;
    p->st = p->lv->start;
    p->nhist = 0;
    p->moves = p->pushes = 0;
    p->face = MD_S;
    p->anim = MA_IDLE;
    p->qr = p->qw;
    settle(p);
    p->events |= PE_RESTART;
}

static void start_step(ml_play_t *p, int d)
{
    const ml_map_t *m = &p->lv->map;
    ml_state_t before = p->st;
    ml_step_t o;
    p->face = d;
    if (!ml_step(m, &p->st, d, &o)) {
        p->bump_t = BUMP_S;
        p->events |= PE_BUMP;
        p->qr = p->qw;          /* a walk that meets something stops        */
        return;
    }
    if (p->nhist >= ML_HIST) {
        memmove(p->hist, p->hist + 1, sizeof(ml_hist_t) * (ML_HIST - 1));
        p->nhist--;
    }
    ml_hist_t *h = &p->hist[p->nhist++];
    h->st = before;
    h->pushes = (uint16_t)p->pushes;
    h->face = (uint8_t)p->face;
    p->moves++;
    p->idle_t = 0;
    bool push = o.thing >= 0;
    p->mt.on = true;
    p->mt.from = before.mila;
    p->mt.to = p->st.mila;
    p->mt.u = 0;
    p->mt.dur = push ? PUSH_S : WALK_S;
    p->anim = push ? MA_PUSH : MA_WALK;
    p->events |= push ? PE_PUSH : PE_STEP;
    int tf = C_TERR(m->cell[p->st.mila]), tb = C_TERR(m->cell[before.mila]);
    if (tf == T_FLAP_V || tf == T_FLAP_H || tb == T_FLAP_V || tb == T_FLAP_H) p->events |= PE_FLAP;
    for (int i = 0; i < p->nflaps; i++)
        if (p->flap_cell[i] == p->st.mila || p->flap_cell[i] == before.mila) p->flap_t[i] = 0;
    if (push) {
        p->pushes++;
        ml_tween_t *t = &p->th[o.thing];
        t->on = true;
        t->from = o.from;
        t->to = o.to;
        t->u = 0;
        t->dist = 0;
        t->fell = o.fell;
        bool ball = m->kind[o.thing] == K_BALL;
        t->dur = PUSH_S + (o.cells - 1) * (ball ? ROLL_S : SLIDE_S) + (o.fell ? FALL_S : 0);
        if (o.cells > 1) p->events |= ball ? PE_ROLL : PE_SLIDE;
    }
}

static void end_step(ml_play_t *p)
{
    const ml_map_t *m = &p->lv->map;
    ml_world_sync(p->w);
    /* what came to rest */
    for (int i = 0; i < m->nthings; i++) {
        ml_tween_t *t = &p->th[i];
        if (!t->on) continue;
        t->on = false;
        if (t->fell) p->events |= PE_FALL;
        else if (m->cell[p->st.pos[i]] & C_TARGET) p->events |= PE_TARGET;
    }
    if (ml_won(m, &p->st) && !p->won) {
        p->won = true;
        p->won_t = 0;
        p->anim = MA_WIN;
        p->anim_t = 0;
        p->face = MD_S;
        p->qr = p->qw;
        p->events |= PE_WIN;
    }
}

void ml_play_step(ml_play_t *p, float dt)
{
    const ml_map_t *m = &p->lv->map;
    if (p->want_undo) {
        p->want_undo = false;
        do_undo(p);
    }
    if (p->want_restart) {
        p->want_restart = false;
        do_restart(p);
    }
    p->anim_t += dt;
    if (p->won) p->won_t += dt;
    /* tweens */
    bool was_busy = ml_play_busy(p);
    if (p->mt.on) {
        p->mt.u += dt / p->mt.dur;
        if (p->mt.u >= 1) p->mt.on = false;
    }
    if (p->bump_t > 0) {
        p->bump_t -= dt;
        if (p->bump_t < 0) p->bump_t = 0;
    }
    for (int i = 0; i < m->nthings; i++) {
        ml_tween_t *t = &p->th[i];
        if (!t->on) continue;
        t->u += dt / t->dur;
        if (t->u >= 1) t->u = 1;
    }
    bool things_done = true;
    for (int i = 0; i < m->nthings; i++)
        if (p->th[i].on && p->th[i].u < 1) things_done = false;
    if (was_busy && !p->mt.on && p->bump_t <= 0 && things_done) end_step(p);
    /* the next step */
    if (!ml_play_busy(p) && !p->won) {
        if (p->qr != p->qw) {
            int d = p->q[p->qr % ML_QUEUE];
            p->qr++;
            start_step(p, d);
        } else if (p->anim == MA_WALK || p->anim == MA_PUSH) {
            p->anim = MA_IDLE;
            p->anim_t = 0;
        }
    }
    /* idle: a yawn now and then */
    if (!ml_play_busy(p) && !p->won) {
        p->idle_t += dt;
        if (p->anim == MA_IDLE && p->idle_t > YAWN_AFTER) {
            p->anim = MA_YAWN;
            p->anim_t = 0;
            p->idle_t = 0;
        } else if (p->anim == MA_YAWN && p->anim_t > 1.0f) {
            const ml_frames_t *f = ml_mila_frames(p->mila, MA_YAWN, MD_S);
            if (!f || p->anim_t * 1000.0f > (float)(f->body.n * (f->body.ms ? f->body.ms : 120))) {
                p->anim = MA_IDLE;
                p->anim_t = 0;
            }
        }
    }
    /* gates and flaps */
    for (int i = 0; i < p->ngates; i++) {
        float want = ml_gate_open(m, &p->st, p->gate_cell[i]) ? 1.0f : 0.0f;
        float a = p->gate_amt[i];
        if (want > a && a == 0) p->events |= PE_GATE;
        if (want < a && a == 1) p->events |= PE_GATE_SHUT;
        a += (want > a ? 1 : -1) * dt * 4.0f;
        p->gate_amt[i] = want > p->gate_amt[i] ? (a > want ? want : a) : (a < want ? want : a);
    }
    for (int i = 0; i < p->nflaps; i++) {
        if (p->flap_t[i] >= 0) {
            p->flap_t[i] += dt;
            if (p->flap_t[i] > 0.6f) p->flap_t[i] = -1;
        }
    }
}

/* ---- where things are drawn ---- */

static void cell_g(int c, float *gx, float *gy)
{
    *gx = ml_cx(c) + 0.5f;
    *gy = ml_cy(c) + 0.5f;
}

static float ease(float u)
{
    if (u <= 0) return 0;
    if (u >= 1) return 1;
    return u * u * (3 - 2 * u);
}

void ml_play_mila_pos(const ml_play_t *p, float *gx, float *gy)
{
    if (p->mt.on) {
        float ax, ay, bx, by;
        cell_g(p->mt.from, &ax, &ay);
        cell_g(p->mt.to, &bx, &by);
        float u = p->mt.u;
        *gx = ax + (bx - ax) * u;
        *gy = ay + (by - ay) * u;
    } else {
        cell_g(p->st.mila, gx, gy);
    }
    if (p->bump_t > 0) {
        float k = sinf((1 - p->bump_t / BUMP_S) * 3.14159f) * 0.12f;
        static const float dx[4] = { 0, 1, 0, -1 }, dy[4] = { -1, 0, 1, 0 };
        *gx += dx[p->face] * k;
        *gy += dy[p->face] * k;
    }
}

/* a thing's position: the first cell with Mila's push, then the rest of
 * its slide, then the fall */
static void thing_pos(const ml_play_t *p, int i, float *gx, float *gy, float *z, float *alpha)
{
    const ml_tween_t *t = &p->th[i];
    *z = 0;
    *alpha = 1;
    if (!t->on) {
        cell_g(p->st.pos[i], gx, gy);
        return;
    }
    const ml_map_t *m = &p->lv->map;
    float ax, ay, bx, by;
    cell_g(t->from, &ax, &ay);
    cell_g(t->to, &bx, &by);
    float cells = fabsf(bx - ax) + fabsf(by - ay);
    float tt = t->u * t->dur;
    bool ball = m->kind[i] == K_BALL;
    float per = ball ? ROLL_S : SLIDE_S;
    float d;
    if (tt < PUSH_S) d = tt / PUSH_S;
    else d = 1 + (tt - PUSH_S) / per;
    if (d > cells) d = cells;
    float k = cells > 0 ? d / cells : 1;
    *gx = ax + (bx - ax) * k;
    *gy = ay + (by - ay) * k;
    if (t->fell) {
        float moving = PUSH_S + (cells - 1) * per;
        if (tt > moving) {
            float f = (tt - moving) / FALL_S;
            if (f > 1) f = 1;
            *z = -ML_FLOOR_M * ease(f);
            *alpha = 1 - f * 0.6f;
        }
    }
}

static void add(ml_dlist_t *dl, ml_play_t *p, const ml_spr_t *s, int fmt, float gx, float gy, float z,
                int prio, const ml_lut_t *lut, int alpha, int flags)
{
    if (!s) return;
    ml_draw_t *e = ml_dlist_add(dl);
    if (!e) return;
    e->s = s;
    e->lut = lut;
    e->x = (int16_t)ml_iround(ml_lpx(p->w, gx));
    e->y = (int16_t)ml_iround(ml_lpy(p->w, gy, z));
    e->d = (int16_t)ml_depth(p->w, gy, z);
    e->fmt = (uint8_t)fmt;
    e->alpha = (uint8_t)alpha;
    e->flags = (uint8_t)flags;
    e->prio = (int8_t)prio;
    e->xray = ml_rgb(120, 140, 210);
}

static const ml_spr_t *frame(const ml_anim_t *a, int k)
{
    if (!a->n) return NULL;
    return &a->f[((k % a->n) + a->n) % a->n];
}

typedef void (*emit_fn)(void *ctx, const ml_spr_t *s, int fmt, float gx, float gy, float z, int prio,
                        const ml_lut_t *lut, int alpha, int flags);

static void emit_all(ml_play_t *p, emit_fn fn, void *ctx)
{
    const ml_map_t *m = &p->lv->map;
    const ml_things_art_t *a = &p->art;
    /* things */
    int nobj = 0;
    for (int i = 0; i < m->nthings; i++) {
        if (p->st.pos[i] == ML_GONE && !p->th[i].on) continue;
        float gx, gy, z, al;
        thing_pos(p, i, &gx, &gy, &z, &al);
        int alpha = (int)(al * 255);
        if (m->kind[i] == K_BALL) {
            int v = i - nobj;
            const ml_anim_t *roll = &a->ball_roll[v & 1];
            const ml_spr_t *s = NULL;
            if (p->th[i].on && roll->n) {
                float moved = fabsf(gx - (ml_cx(p->th[i].from) + 0.5f)) + fabsf(gy - (ml_cy(p->th[i].from) + 0.5f));
                s = frame(roll, (int)(moved * 6));
            }
            if (!s) s = frame(&a->ball, v);
            fn(ctx, s, ML_PX_COL, gx, gy, z, 1, NULL, alpha, 0);
            if (z == 0) fn(ctx, frame(&a->ball_sh, v), ML_PX_PLANE, gx, gy, 0, 0, NULL, 150, 0);
        } else {
            nobj++;
            bool on = !p->th[i].on && (m->cell[p->st.pos[i]] & C_TARGET);
            const ml_spr_t *s = on && a->obj_on.n ? frame(&a->obj_on, i) : frame(&a->obj, i);
            fn(ctx, s, ML_PX_COL, gx, gy, z, 1, NULL, alpha, 0);
            if (z == 0 && !(on && a->obj_on.n)) fn(ctx, frame(&a->obj_sh, i), ML_PX_PLANE, gx, gy, 0, 0, NULL, 150, 0);
        }
    }
    /* gates and flaps */
    for (int i = 0; i < p->ngates; i++) {
        const ml_anim_t *g = p->gate_h[i] ? &a->gate_h : &a->gate_v;
        const ml_anim_t *gs = p->gate_h[i] ? &a->gate_h_sh : &a->gate_v_sh;
        float gx, gy;
        cell_g(p->gate_cell[i], &gx, &gy);
        int k = g->n > 1 ? (int)(p->gate_amt[i] * (g->n - 1) + 0.5f) : 0;
        fn(ctx, frame(g, k), ML_PX_COL, gx, gy, 0, 0, NULL, 255, 0);
        if (gs->n) fn(ctx, frame(gs, k), ML_PX_PLANE, gx, gy, 0, 0, NULL, 140, 0);
    }
    for (int i = 0; i < p->nflaps; i++) {
        int c = p->flap_cell[i];
        const ml_anim_t *f = C_TERR(m->cell[c]) == T_FLAP_H ? &a->flap_h : &a->flap_v;
        float gx, gy;
        cell_g(c, &gx, &gy);
        int k = 0;
        if (p->flap_t[i] >= 0 && f->n > 1) {
            /* 1, 2, 3, 2, 1: the flap swings and settles */
            static const int seq[] = { 1, 2, 3, 3, 2, 1, 1 };
            int j = (int)(p->flap_t[i] / 0.6f * 7);
            k = seq[j > 6 ? 6 : j];
            if (k >= f->n) k = f->n - 1;
        }
        fn(ctx, frame(f, k), ML_PX_COL, gx, gy, 0, 0, NULL, 255, 0);
    }
    /* Mila */
    float gx, gy;
    ml_play_mila_pos(p, &gx, &gy);
    const ml_frames_t *fr = ml_mila_frames(p->mila, p->anim, p->face);
    if (!fr) fr = ml_mila_frames(p->mila, MA_IDLE, p->face);
    if (fr) {
        bool loop = p->anim == MA_IDLE || p->anim == MA_WALK || p->anim == MA_PUSH;
        int k;
        if (p->anim == MA_WALK || p->anim == MA_PUSH) {
            /* the cycle follows the step, so the paws match the ground */
            float u = p->mt.on ? p->mt.u : 0;
            k = (int)(u * fr->body.n) % (fr->body.n ? fr->body.n : 1);
        } else {
            k = ml_mila_frame(fr, p->anim_t, loop);
        }
        fn(ctx, frame(&fr->body, k), ML_PX_COL, gx, gy, 0, 2, NULL, 255, DR_XRAY);
        if (fr->sh.n) fn(ctx, frame(&fr->sh, k), ML_PX_PLANE, gx, gy, 0, 0, NULL, 150, 0);
        if (fr->neck.n) fn(ctx, frame(&fr->neck, k), ML_PX_LID, gx, gy, 0, 3, &p->mila->neck_lut, 255, 0);
        if (fr->hat.n) fn(ctx, frame(&fr->hat, k), ML_PX_LID, gx, gy, 0, 4, &p->mila->hat_lut, 255, 0);
    }
}

typedef struct {
    ml_dlist_t *dl;
    ml_play_t *p;
} dl_ctx_t;

static void emit_dl(void *ctx, const ml_spr_t *s, int fmt, float gx, float gy, float z, int prio,
                    const ml_lut_t *lut, int alpha, int flags)
{
    dl_ctx_t *c = (dl_ctx_t *)ctx;
    add(c->dl, c->p, s, fmt, gx, gy, z, prio, lut, alpha, flags);
}

void ml_play_draw(ml_play_t *p, ml_dlist_t *dl)
{
    ml_dlist_clear(dl);
    dl_ctx_t c = { dl, p };
    emit_all(p, emit_dl, &c);
    ml_dlist_sort(dl);
}

typedef struct {
    ml_ov_item_t *out;
    int n, max;
    ml_play_t *p;
} ov_ctx_t;

static void emit_ov(void *ctx, const ml_spr_t *s, int fmt, float gx, float gy, float z, int prio,
                    const ml_lut_t *lut, int alpha, int flags)
{
    (void)alpha;
    (void)flags;
    ov_ctx_t *c = (ov_ctx_t *)ctx;
    if (!s || c->n >= c->max) return;
    ml_ov_item_t *e = &c->out[c->n++];
    e->s = s;
    e->lut = lut;
    e->gx = gx;
    e->gy = gy;
    e->z = z;
    e->d = (int16_t)(ml_depth(c->p->w, gy, z) * 4 - prio);
    e->fmt = (uint8_t)fmt;
    e->layer = fmt == ML_PX_PLANE ? 2 : 3;
}

int ml_play_ov_items(ml_play_t *p, ml_ov_item_t *out, int max)
{
    ov_ctx_t c = { out, 0, max, p };
    emit_all(p, emit_ov, &c);
    return c.n;
}

/* ---- camera ---- */

void ml_play_camera(ml_play_t *p, float dt, bool snap)
{
    float gx, gy;
    ml_play_mila_pos(p, &gx, &gy);
    float tx = ml_lpx(p->w, gx) - ML_W / 2.0f;
    float ty = ml_lpy(p->w, gy, 0.3f) - ML_H / 2.0f;
    if (snap) {
        p->cam_x = tx;
        p->cam_y = ty;
    } else {
        float k = 1.0f - expf(-dt * 7.0f);
        p->cam_x += (tx - p->cam_x) * k;
        p->cam_y += (ty - p->cam_y) * k;
    }
    p->icam_x = ml_iround(p->cam_x);
    p->icam_y = ml_iround(p->cam_y);
}

int ml_play_cell_at(const ml_play_t *p, int sx, int sy)
{
    const ml_map_t *m = &p->lv->map;
    float lx = (float)(sx + p->icam_x - p->w->ox), ly = (float)(sy + p->icam_y - p->w->oy);
    int x = ml_ifloor(lx / ML_CELL_W), y = ml_ifloor(ly / ML_CELL_H);
    if (x < 0 || y < 0 || x >= m->w || y >= m->h) return -1;
    return y * ML_MAXW + x;
}

int ml_play_stars(const ml_play_t *p)
{
    if (!p->won) return 0;
    int par = p->lv->par;
    if (p->moves <= par) return 3;
    int two = par + par / 4;
    if (two < par + 4) two = par + 4;
    return p->moves <= two ? 2 : 1;
}

int ml_play_on_target(const ml_play_t *p)
{
    const ml_map_t *m = &p->lv->map;
    int n = 0;
    for (int i = 0; i < m->nthings; i++)
        if (p->st.pos[i] != ML_GONE && (m->cell[p->st.pos[i]] & C_TARGET)) n++;
    return n;
}
