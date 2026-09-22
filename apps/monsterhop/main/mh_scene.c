/*
 * MONSTER HOP - the draw list (see mh_scene.h)
 */
#include "mh_scene.h"
#include "mh_shop.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FLOOR_M 0.50923f

static const int DXv[4] = { 0, 1, 0, -1 };
static const int DYv[4] = { 1, 0, -1, 0 };

static float absf(float v) { return v < 0 ? -v : v; }

/* ---- palettes ---- */

/* what a monster looks like when the pack has no palette for it */
static void mon_default(int kind, int var, mh_pal_t *p)
{
    memset(p, 0, sizeof(*p));
    static const uint32_t shirts[3] = { 0x5A6EA8, 0xA85050, 0x5E9A58 };
    p->c[3] = 0x1E1616;
    p->c[4] = 0xF0EAD8;
    p->c[5] = 0xFFE040;
    switch (kind) {
    case MON_ZOMBIE: case MON_BRUTE:
        p->c[1] = 0x92AE7C; p->c[2] = 0x6E8C5E; p->c[6] = 0x3A3024;
        p->c[7] = shirts[var % 3]; p->c[8] = 0xD8D0B8; p->c[9] = 0x444C5C; p->c[10] = 0x3A2A20;
        p->c[11] = 0xE8B020; p->c[12] = 0x805020;
        break;
    case MON_VAMPIRE: case MON_COUNT:
        p->c[1] = 0xD8CCEC; p->c[2] = 0xB8A8D8; p->c[5] = 0xFF3040; p->c[6] = 0x181420;
        p->c[7] = 0x241830; p->c[8] = 0xC01830; p->c[9] = 0x5A2A6A; p->c[10] = 0x201820;
        p->c[11] = 0xE8C040; p->c[12] = 0x901020;
        break;
    case MON_MUMMY: case MON_PHARAOH:
        p->c[1] = 0xE8DCC0; p->c[2] = 0xC0B090; p->c[5] = 0x40FFD0; p->c[6] = 0x302418;
        p->c[7] = 0xB89868; p->c[8] = 0x8A6A40; p->c[11] = 0xF0C030; p->c[12] = 0x2850C0;
        break;
    case MON_WEREWOLF: case MON_ALPHA:
        p->c[1] = kind == MON_ALPHA ? 0xA8ACB4 : 0x7A6450; p->c[2] = 0xC8B8A0; p->c[5] = 0xFFD020;
        p->c[6] = 0x4A3A2C; p->c[7] = 0x3A5A8A; p->c[8] = 0x2A3A5A; p->c[11] = 0x806040;
        break;
    case MON_ZOMBIEDOG:
        p->c[1] = 0x9AA888; p->c[2] = 0x788866; p->c[11] = 0xC02828; p->c[12] = 0xE8C040;
        break;
    case MON_ARMOR:
        p->c[1] = 0xB8BCC8; p->c[2] = 0x80848F; p->c[5] = 0x60C0FF; p->c[11] = 0xC02040; p->c[12] = 0x5A2A8A;
        break;
    case MON_CROW:
        p->c[1] = 0x2A2A38; p->c[2] = 0x4A4A60; p->c[3] = 0xE0A020; p->c[5] = 0xFF5030;
        break;
    default:
        p->c[1] = 0x90A080;
        break;
    }
}

static const char *mon_key(int kind)
{
    static const char *const n[MON_N] = { "zombie", "vampire", "mummy", "werewolf", "zombiedog", "armor",
                                          "crow", "brute", "count", "pharaoh", "alpha" };
    return kind >= 0 && kind < MON_N ? n[kind] : "";
}

/* the pack's palette for a monster variant, trying the names the art
 * scripts use (pal_zombie, pal_zombie_0, pal_zombie_default...) */
static bool mon_pal(int kind, int var, mh_pal_t *p)
{
    char nm[48];
    const char *k = mon_key(kind);
    /* the base palette, then a variant's regions over it (variants only
     * carry what they change: the clothes) */
    snprintf(nm, sizeof nm, "pal_%s", k);
    bool base = mh_pal_load(nm, p);
    if (!base) {
        snprintf(nm, sizeof nm, "pal_%s_default", k);
        base = mh_pal_load(nm, p);
    }
    if (!base) mon_default(kind, var, p);
    mh_pal_t v;
    snprintf(nm, sizeof nm, "pal_%s_%d", k, var);
    if (mh_pal_load(nm, &v)) {
        for (int i = 1; i < 16; i++) if (v.c[i]) p->c[i] = v.c[i];
        return true;
    }
    return var == 0 && base;
}

/* the odd skins' effects (mh_shop.h: 1 ghost, 2 lava, 3 rainbow) */
#define FX_GHOST 1
#define FX_LAVA 2
#define FX_RAINBOW 3

void mh_scene_outfit(mh_scene_t *s, const mh_outfit_t *o, uint32_t tint, int skin_fx)
{
    s->tint = tint;
    s->skin_fx = skin_fx;
    s->body_pal = o->body;
    mh_lut_build(&s->body, &o->body, tint, skin_fx == FX_LAVA ? (1u << 1) : 0);
    mh_lut_build(&s->cap, &o->cap, tint, 0);
    mh_lut_build(&s->back, &o->back, tint, 1u << 4);
    mh_lut_build(&s->hand, &o->hand, tint, 1u << 4);
    mh_lut_build(&s->pet, &o->pet, tint, 0);
    mh_scene_rival(s, &o->body);
}

void mh_scene_trail(mh_scene_t *s, int trail)
{
    s->trail_style = trail;
    s->nbit = 0;
    if (!s->rnd) s->rnd = 0x9E3779B9u;
}

static void bit_add(mh_scene_t *s, int kind, float x, float y, float z, float vx, float vy, float vz, float life,
                    uint32_t col)
{
    if (s->nbit >= MH_SCENE_BITS) {
        /* the oldest makes room */
        memmove(&s->bit[0], &s->bit[1], sizeof(s->bit[0]) * (MH_SCENE_BITS - 1));
        s->nbit--;
    }
    s->bit[s->nbit].x = x;
    s->bit[s->nbit].y = y;
    s->bit[s->nbit].z = z;
    s->bit[s->nbit].vx = vx;
    s->bit[s->nbit].vy = vy;
    s->bit[s->nbit].vz = vz;
    s->bit[s->nbit].t = 0;
    s->bit[s->nbit].life = life;
    s->bit[s->nbit].col = col;
    s->bit[s->nbit].kind = (uint8_t)kind;
    s->nbit++;
}

static void bits_step(mh_scene_t *s, const mh_game_t *g, float dt)
{
    int k = 0;
    for (int i = 0; i < s->nbit; i++) {
        s->bit[i].t += dt;
        if (s->bit[i].t >= s->bit[i].life) continue;
        s->bit[i].x += s->bit[i].vx * dt;
        s->bit[i].y += s->bit[i].vy * dt;
        s->bit[i].z += s->bit[i].vz * dt;
        if (s->bit[i].kind == TRAIL_CONFETTI) s->bit[i].vz -= 3.0f * dt;
        if (s->bit[i].kind == TRAIL_SPARKLES) s->bit[i].vz -= 0.6f * dt;
        s->bit[k++] = s->bit[i];
    }
    s->nbit = k;
    if (s->trail_style == TRAIL_NONE || g->state != GS_PLAY) return;
    const mh_hero_t *h = &g->h;
    bool air = h->state == H_HOP || h->state == H_SUPER;
    if (s->trail_style == TRAIL_STEPS) {
        /* a footprint where he took off */
        if (air && s->last_state != H_HOP && s->last_state != H_SUPER && h->ride < 0) {
            s->step_side ^= 1;
            float side = s->step_side ? 0.12f : -0.12f;
            bit_add(s, TRAIL_STEPS, h->fx + (h->dir & 1 ? 0 : side), h->fy + (h->dir & 1 ? side : 0), h->fz, 0, 0, 0,
                    1.6f, 0x3A3028);
        }
        s->last_state = h->state;
        return;
    }
    s->last_state = h->state;
    if (!air) return;
    s->emit_t += dt;
    float every = s->trail_style == TRAIL_HEARTS ? 0.09f : 0.035f;
    static const uint32_t conf[4] = { 0xFF5A5A, 0x5AD0FF, 0xFFD040, 0x8AF08A };
    while (s->emit_t > every) {
        s->emit_t -= every;
        float rx = mh_randf(&s->rnd) - 0.5f, ry = mh_randf(&s->rnd) - 0.5f;
        switch (s->trail_style) {
        case TRAIL_SPARKLES:
            bit_add(s, TRAIL_SPARKLES, h->x + rx * 0.3f, h->y + ry * 0.3f, h->z + 0.35f + mh_randf(&s->rnd) * 0.3f,
                    rx * 0.4f, ry * 0.4f, 0.2f, 0.55f, mh_randf(&s->rnd) < 0.5f ? 0xFFE070 : 0xFFFFFF);
            break;
        case TRAIL_CONFETTI:
            bit_add(s, TRAIL_CONFETTI, h->x + rx * 0.2f, h->y + ry * 0.2f, h->z + 0.6f, rx * 1.6f, ry * 1.6f,
                    0.9f + mh_randf(&s->rnd) * 0.6f, 0.7f, conf[mh_rand(&s->rnd) & 3]);
            break;
        case TRAIL_HEARTS:
            bit_add(s, TRAIL_HEARTS, h->x + rx * 0.3f, h->y + ry * 0.3f, h->z + 0.5f, 0, 0, 0.7f, 0.8f,
                    mh_randf(&s->rnd) < 0.5f ? 0xFF5A8A : 0xFFB0C8);
            break;
        default:
            break;
        }
    }
}

void mh_scene_bits_draw(const mh_scene_t *s, const mh_world_t *w, mh_img_t *im, int cam_x, int cam_y)
{
    for (int i = 0; i < s->nbit; i++) {
        const float f = s->bit[i].t / s->bit[i].life;
        int x = (int)(mh_lpx(w, s->bit[i].x, s->bit[i].y) - (float)cam_x);
        int y = (int)(mh_lpy(w, s->bit[i].x, s->bit[i].y, s->bit[i].z) - (float)cam_y);
        if (x < -8 || x > MH_W + 8 || y < im->cy0 - 8 || y > im->cy1 + 8) continue;
        uint16_t c = mh_hex(s->bit[i].col);
        int a = (int)(255 * (1 - f));
        switch (s->bit[i].kind) {
        case TRAIL_SPARKLES: {
            /* a four-pointed glint that shrinks */
            int r = f < 0.4f ? 6 : f < 0.7f ? 4 : 3;
            mh_rect_blend(im, x - r, y, 2 * r + 1, 1, c, a);
            mh_rect_blend(im, x, y - r, 1, 2 * r + 1, c, a);
            mh_rect_blend(im, x - r / 2, y - 1, r + 1, 3, c, a / 2);
            mh_rect_blend(im, x - 1, y - r / 2, 3, r + 1, c, a / 2);
            mh_disc(im, x * 16 + 8, y * 16 + 8, 40, mh_hex(0xFFFFFF), a);
            break;
        }
        case TRAIL_CONFETTI: {
            /* a paper that flips as it falls */
            bool wide = ((int)(s->bit[i].t * 16) + i) & 1;
            mh_rect_blend(im, x - 3, y - 2, wide ? 7 : 3, wide ? 3 : 5, c, a);
            break;
        }
        case TRAIL_HEARTS: {
            int k = f < 0.2f ? 12 + (int)(f * 20) : 16;     /* grows a little, then floats */
            mh_disc(im, x * 16 - k * 4, y * 16, k * 4, c, a);
            mh_disc(im, x * 16 + k * 4, y * 16, k * 4, c, a);
            for (int r = 0; r < k / 3; r++) mh_rect_blend(im, x - k / 2 + r, y + 1 + r, k - 2 * r + 1, 1, c, a);
            mh_disc(im, x * 16 - k * 5, y * 16 - k * 3, k * 3 / 2, mh_hex(0xFFFFFF), a / 2);
            break;
        }
        case TRAIL_STEPS:
            /* a shoe print: the sole and the heel */
            mh_disc(im, x * 16, y * 16, 64, c, a * 5 / 8);
            mh_disc(im, (x + 3) * 16, (y - 4) * 16, 44, c, a * 5 / 8);
            break;
        default:
            break;
        }
    }
}

void mh_scene_rival(mh_scene_t *s, const mh_pal_t *body)
{
    /* the other watch's Tommy: its colours, paled towards a cool blue */
    mh_pal_t gp = *body;
    for (int i = 1; i < 16; i++) gp.c[i] = mh_mix(gp.c[i], 0x9AD8FF, 90);
    mh_lut_build(&s->rival, &gp, s->tint, 0);
}

void mh_scene_init(mh_scene_t *s, const mh_world_t *w, const mh_game_t *g, const mh_outfit_t *o, int skin_fx)
{
    memset(s, 0, sizeof(*s));
    uint32_t tint = mh_zone_look(g->lv->zone)->tint;
    mh_scene_outfit(s, o, tint, skin_fx);
    for (int k = 0; k < MON_N; k++) {
        int nv = 0;
        for (int v = 0; v < 3; v++) {
            mh_pal_t p;
            if (!mon_pal(k, v, &p)) {
                if (v > 0) break;
                mon_default(k, v, &p);
                /* the zombies' clothes come in three looks even without art */
                if (k == MON_ZOMBIE) {
                    for (int vv = 0; vv < 3; vv++) {
                        mon_default(k, vv, &p);
                        mh_lut_build(&s->mon[k][vv], &p, tint, 1u << 5);
                    }
                    nv = 3;
                    break;
                }
            }
            mh_lut_build(&s->mon[k][v], &p, tint, 1u << 5);
            nv = v + 1;
        }
        s->mon_var[k] = nv ? nv : 1;
    }
    mh_pal_t p;
    if (!mh_pal_load("pal_bat", &p)) mon_default(MON_VAMPIRE, 0, &p);
    mh_lut_build(&s->bat, &p, tint, 1u << 5);
    if (!mh_pal_load("pal_scarab", &p)) {
        memset(&p, 0, sizeof p);
        p.c[1] = 0x2A9A8A; p.c[2] = 0xE0B040; p.c[3] = 0x1A1A1A; p.c[5] = 0xFF4020;
    }
    mh_lut_build(&s->scarab, &p, tint, 1u << 5);
    static const uint32_t paints[4][2] = { { 0x8A3A2A, 0xD8C8A0 }, { 0x3A5A7A, 0xE0E0E0 },
                                           { 0x6A7A3A, 0xC8B870 }, { 0x7A5A8A, 0xD8D0E0 } };
    for (int i = 0; i < 4; i++) {
        memset(&p, 0, sizeof p);
        p.c[1] = paints[i][0]; p.c[2] = paints[i][1]; p.c[3] = 0x6A8AA0; p.c[4] = 0xC8C8D0;
        p.c[5] = 0x202024; p.c[6] = 0x1A1A1A; p.c[7] = 0xFFE8A0; p.c[8] = 0x8A4A28;
        mh_lut_build(&s->car[i], &p, tint, 1u << 7);
    }
    s->px = g->h.x;
    s->py = g->h.y - 1;
    s->pz = g->h.z;
    s->pt = 1;
    (void)w;
}

/* ---- helpers ---- */

static const mh_spr_t *pick(const mh_anim_t *a, int frame)
{
    if (!a || !a->n) return NULL;
    if (frame < 0) frame = 0;
    return &a->f[frame % a->n];
}

static int loop_frame(const mh_anim_t *a, float t)
{
    if (!a || !a->n) return 0;
    int ms = a->ms ? a->ms : 120;
    return (int)(t * 1000.0f / (float)ms) % a->n;
}

static int prog_frame(const mh_anim_t *a, float f)
{
    if (!a || !a->n) return 0;
    int k = (int)(f * (float)a->n);
    return k < 0 ? 0 : k >= a->n ? a->n - 1 : k;
}

/* an animation of a rig, for a facing (falls back to s, then to any) */
static const mh_anim_t *rig_anim(const mh_rig_t *r, int slot, int dir)
{
    if (!r || !r->any) return NULL;
    if (r->a[slot][dir].n) return &r->a[slot][dir];
    if (r->a[slot][DIR_S].n) return &r->a[slot][DIR_S];
    return NULL;
}
static const mh_anim_t *rig_shadow(const mh_rig_t *r, int slot, int dir)
{
    if (!r || !r->any) return NULL;
    if (r->sh[slot][dir].n) return &r->sh[slot][dir];
    if (r->sh[slot][DIR_S].n) return &r->sh[slot][DIR_S];
    return NULL;
}

static mh_draw_t *put(mh_dlist_t *l, const mh_world_t *w, const mh_spr_t *s, int fmt, float x, float y, float z)
{
    if (!s) return NULL;
    mh_draw_t *d = mh_dlist_add(l);
    if (!d) return NULL;
    d->s = s;
    d->fmt = (uint8_t)fmt;
    d->x = (int16_t)mh_iround(mh_lpx(w, x, y));
    d->y = (int16_t)mh_iround(mh_lpy(w, x, y, z));
    d->d = (int16_t)mh_depth(w, x, y, z);
    return d;
}

/* is an LP point anywhere near the screen? */
static bool visible(const mh_scene_t *s, const mh_world_t *w, float x, float y, float z)
{
    float lx = mh_lpx(w, x, y), ly = mh_lpy(w, x, y, z);
    return lx > s->icam_x - 160 && lx < s->icam_x + MH_W + 160 && ly > s->icam_y - 60 && ly < s->icam_y + MH_H + 260;
}

/* ---- the camera and the pet ---- */

void mh_scene_look(mh_scene_t *s, const mh_world_t *w, float x, float y, float z, float dt, bool instant)
{
    float tx = mh_lpx(w, x, y) - MH_W / 2.0f;
    float ty = mh_lpy(w, x, y, z) - MH_H * 0.58f;
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx > w->lw - MH_W) tx = (float)(w->lw - MH_W);
    if (ty > w->lh - MH_H) ty = (float)(w->lh - MH_H);
    if (instant || !s->cam_set) {
        s->cam_x = tx;
        s->cam_y = ty;
        s->cam_set = true;
    } else {
        float k = dt * 7.0f;
        if (k > 1) k = 1;
        s->cam_x += (tx - s->cam_x) * k;
        s->cam_y += (ty - s->cam_y) * k;
    }
    s->icam_x = mh_iround(s->cam_x);
    s->icam_y = mh_iround(s->cam_y);
}

static void pet_step(mh_scene_t *s, const mh_game_t *g, float dt)
{
    const mh_hero_t *h = &g->h;
    /* remember Tommy's cells */
    int cx = mh_ifloor(h->x), cy = mh_ifloor(h->y);
    if (s->ntrail == 0 || s->trail[0][0] != cx || s->trail[0][1] != cy) {
        memmove(&s->trail[1], &s->trail[0], sizeof(s->trail) - sizeof(s->trail[0]));
        s->trail[0][0] = cx;
        s->trail[0][1] = cy;
        if (s->ntrail < 8) s->ntrail++;
    }
    if (g->state == GS_PLAY && g->st < 0.05f) {
        /* a respawn: it comes along */
        s->px = h->x;
        s->py = h->y;
        s->pz = h->z;
        s->pt = 1;
        s->ntrail = 0;
        return;
    }
    if (s->pt < 1) {
        s->pt += dt / 0.2f;
        if (s->pt > 1) s->pt = 1;
        float f = s->pt;
        s->px = s->pfx + (s->ptx - s->pfx) * f;
        s->py = s->pfy + (s->pty - s->pfy) * f;
        s->pz = s->pfz + (s->ptz - s->pfz) * f + 4.0f * 0.22f * f * (1 - f);
        return;
    }
    if (s->ntrail >= 2) {
        float tx = s->trail[1][0] + 0.5f, ty = s->trail[1][1] + 0.5f;
        float dx = tx - s->px, dy = ty - s->py;
        if (dx * dx + dy * dy > 0.04f) {
            s->pfx = s->px; s->pfy = s->py; s->pfz = s->pz;
            s->ptx = tx; s->pty = ty;
            const mh_level_t *lv = g->lv;
            int ix = s->trail[1][0], iy = s->trail[1][1];
            s->ptz = mh_in(lv, ix, iy) ? mh_cell(lv, ix, iy)->h * FLOOR_M : s->pz;
            if (h->ride >= 0) s->ptz = h->z;
            s->pt = 0;
            float adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
            s->pdir = adx > ady ? (dx > 0 ? DIR_E : DIR_W) : (dy > 0 ? DIR_N : DIR_S);
        }
    }
}

/* ---- Tommy ---- */

static void hero_draw(mh_scene_t *s, const mh_world_t *w, const mh_game_t *g, const mh_cast_t *c, mh_dlist_t *l)
{
    const mh_hero_t *h = &g->h;
    int slot = HA_IDLE, frame = 0;
    float t = h->t, x = h->x, y = h->y, z = h->z;
    float f = h->dur > 0 ? t / h->dur : 0;
    int alpha = 255;
    switch (h->state) {
    case H_HOP: slot = HA_HOP; break;
    case H_SUPER: slot = HA_SUPER; break;
    case H_PUSH: slot = HA_PUSH; break;
    case H_USE: slot = HA_USE; break;
    case H_BUMP: {
        float k = f < 0.5f ? f : 1 - f;
        x += DXv[h->dir] * k * 0.25f;
        y += DYv[h->dir] * k * 0.25f;
        break;
    }
    case H_HURT: slot = HA_HURT; break;
    case H_SINK: slot = HA_SINK; z -= t * 0.9f; if (t > 0.9f) return; break;
    case H_FALL:
        slot = HA_FALL;
        z -= t * t * 3.0f;
        alpha = t < 0.8f ? 255 - (int)(t * 300) : 0;
        if (alpha <= 0) return;
        break;
    case H_WIN: slot = HA_WIN; break;
    default: break;
    }
    int dir = h->dir;
    if (slot >= HA_WIN) dir = DIR_S;
    const mh_anim_t *ba = rig_anim(&c->body, slot, dir);
    if (!ba && slot != HA_IDLE) ba = rig_anim(&c->body, HA_IDLE, dir);
    bool looping = h->state == H_IDLE || h->state == H_BUMP || h->state == H_SINK || h->state == H_FALL;
    if (ba) frame = looping ? loop_frame(ba, s->anim_t) : prog_frame(ba, f);
    /* blinking while invulnerable after a respawn; the ghost skin is see-through */
    if (s->skin_fx == FX_GHOST && alpha > 150) alpha = 150;
    if (g->state == GS_PLAY && g->lost > 0 && g->st < 1.6f && ((int)(g->st * 10) & 1)) alpha = 110;
    float gz = h->floor * FLOOR_M;
    if (h->ride >= 0) gz = h->z;
    bool air = h->state == H_HOP || h->state == H_SUPER;
    /* the shadow, on the ground under him */
    const mh_anim_t *sa = rig_shadow(&c->body, air ? HA_IDLE : slot, dir);
    if (!ba) sa = c->stand_in_sh.n ? &c->stand_in_sh : NULL;
    if (sa && h->state != H_SINK && h->state != H_FALL) {
        float up = z - (air ? (h->fz + (h->tz - h->fz) * f) : gz);
        mh_draw_t *d = put(l, w, pick(sa, air ? 0 : frame), MH_PX_PLANE, x, y, air ? h->fz + (h->tz - h->fz) * f : gz);
        if (d) d->alpha = (uint8_t)(150 - (up > 0 ? (int)(up * 120) : 0) < 40 ? 40 : 150 - (up > 0 ? (int)(up * 120) : 0));
    }
    if (!ba) {
        mh_draw_t *d = put(l, w, pick(&c->stand_in, 0), MH_PX_LID, x, y, z);
        if (d) {
            d->lut = &s->body;
            d->flags = DR_XRAY;
            d->xray = mh_hex(0x70D0FF);
            d->prio = 1;
            d->alpha = (uint8_t)alpha;
        }
        return;
    }
    const mh_rig_t *layers[4] = { &c->body, &c->back, &c->hand, &c->cap };
    const mh_lut_t *luts[4] = { &s->body, &s->back, &s->hand, &s->cap };
    for (int k = 0; k < 4; k++) {
        const mh_anim_t *a = k == 0 ? ba : rig_anim(layers[k], slot, dir);
        if (!a && k > 0) a = rig_anim(layers[k], HA_IDLE, dir);
        if (!a) continue;
        mh_draw_t *d = put(l, w, pick(a, frame), MH_PX_LID, x, y, z);
        if (!d) continue;
        d->lut = luts[k];
        d->flags = DR_XRAY;
        d->xray = mh_hex(0x70D0FF);
        d->prio = (int8_t)(1 + k);
        d->alpha = (uint8_t)alpha;
    }
}

/* the ghost: body only (its cap and things are the other watch's art),
 * drawn from the state and the hop phase it sent */
static void rival_draw(mh_scene_t *s, const mh_world_t *w, const mh_game_t *g, const mh_cast_t *c, mh_dlist_t *l,
                       float dt)
{
    if (!s->g_set || absf(s->gx - g->rx) > 3 || absf(s->gy - g->ry) > 3) {
        s->gx = g->rx;
        s->gy = g->ry;
        s->gz = g->rz;
        s->g_set = true;
    }
    float k = dt * 18.0f;
    if (k > 1) k = 1;
    s->gx += (g->rx - s->gx) * k;
    s->gy += (g->ry - s->gy) * k;
    s->gz += (g->rz - s->gz) * k;
    int slot = HA_IDLE;
    switch (g->rstate) {
    case H_HOP: slot = HA_HOP; break;
    case H_SUPER: slot = HA_SUPER; break;
    case H_PUSH: slot = HA_PUSH; break;
    case H_USE: slot = HA_USE; break;
    case H_HURT: slot = HA_HURT; break;
    case H_SINK: slot = HA_SINK; break;
    case H_FALL: slot = HA_FALL; break;
    case H_WIN: slot = HA_WIN; break;
    case H_GONE: return;
    default: break;
    }
    int dir = slot >= HA_WIN ? DIR_S : g->rdir;
    const mh_anim_t *a = rig_anim(&c->body, slot, dir);
    if (!a) a = rig_anim(&c->body, HA_IDLE, dir);
    const mh_spr_t *sp;
    if (a) {
        bool looping = slot == HA_IDLE || slot == HA_SINK || slot == HA_FALL;
        float f = g->rf < 0 ? 0 : g->rf > 1 ? 1 : g->rf;
        sp = pick(a, looping ? loop_frame(a, s->anim_t) : prog_frame(a, f));
    } else if (c->stand_in.n) {
        sp = pick(&c->stand_in, 0);
    } else {
        return;
    }
    mh_draw_t *d = put(l, w, sp, MH_PX_LID, s->gx, s->gy, s->gz);
    if (d) {
        d->lut = &s->rival;
        d->alpha = 150;
        d->flags = DR_XRAY;
        d->xray = mh_hex(0x9AD8FF);
        d->prio = 1;
    }
}

static void pet_draw(mh_scene_t *s, const mh_world_t *w, const mh_game_t *g, const mh_cast_t *c, mh_dlist_t *l)
{
    if (!c->pet.any || g->h.state == H_SINK || g->h.state == H_FALL) return;
    bool hop = s->pt < 1;
    const mh_anim_t *a = rig_anim(&c->pet, hop ? HA_HOP : HA_IDLE, s->pdir);
    if (!a) return;
    int fr = hop ? prog_frame(a, s->pt) : loop_frame(a, s->anim_t);
    const mh_anim_t *sa = rig_shadow(&c->pet, HA_IDLE, s->pdir);
    float gz = hop ? s->pfz + (s->ptz - s->pfz) * s->pt : s->pz;
    if (sa) {
        mh_draw_t *d = put(l, w, pick(sa, 0), MH_PX_PLANE, s->px, s->py, gz);
        if (d) d->alpha = 120;
    }
    mh_draw_t *d = put(l, w, pick(a, fr), MH_PX_LID, s->px, s->py, s->pz);
    if (d) {
        d->lut = &s->pet;
        d->flags = DR_XRAY;
        d->xray = mh_hex(0x70D0FF);
    }
}

/* ---- monsters ---- */

static void mon_draw(mh_scene_t *s, const mh_world_t *w, const mh_game_t *g, const mh_cast_t *c, mh_dlist_t *l,
                     const mh_mon_t *m)
{
    if (!visible(s, w, m->x, m->y, m->z)) return;
    const mh_rig_t *r = &c->mon[m->kind];
    const mh_lut_t *lut = &s->mon[m->kind][m->pal % (uint32_t)s->mon_var[m->kind]];
    int slot = MA_IDLE;
    bool bycell = false;       /* frames follow the steps along the path */
    float z = m->z;
    switch (m->kind) {
    case MON_ZOMBIE:
        slot = m->state == M_NOTICE ? MA_NOTICE : m->state == M_LUNGE ? MA_LUNGE : m->state == M_IDLE ? MA_IDLE : MA_WALK;
        bycell = slot == MA_WALK;
        break;
    case MON_VAMPIRE:
        if (m->state == M_BAT) {
            r = &c->bat;
            lut = &s->bat;
            slot = MA_FLY;
        } else {
            slot = (m->state == M_TRANSFORM || m->state == M_UNTRANSFORM) ? MA_TRANSFORM : MA_GLIDE;
        }
        break;
    case MON_MUMMY:
        slot = m->state == M_PUSHB ? MA_PUSH : m->state == M_IDLE ? MA_IDLE : MA_WALK;
        bycell = slot == MA_WALK;
        break;
    case MON_WEREWOLF:
    case MON_ALPHA:
        slot = m->state == M_HOWL ? MA_HOWL : (m->state == M_RUN || m->state == M_HOME) ? MA_RUN :
               m->state == M_STUN ? MA_STUN : (m->kind == MON_ALPHA ? MA_HOWL : MA_IDLE);
        break;
    case MON_ZOMBIEDOG:
        slot = m->state == M_IDLE ? MA_IDLE : MA_RUN;
        break;
    case MON_ARMOR:
        slot = m->state == M_IDLE ? MA_IDLE : MA_WALK;
        bycell = slot == MA_WALK;
        break;
    case MON_CROW:
        slot = m->state == M_PERCH ? MA_PERCH : m->state == M_DIVE ? MA_DIVE : MA_FLY;
        /* the fly frames hold the bird 1 m above their anchor */
        if (slot != MA_PERCH) z -= 1.0f;
        break;
    case MON_BRUTE:
        slot = m->state == M_STOMP ? MA_STOMP : MA_WALK;
        bycell = slot == MA_WALK;
        break;
    case MON_COUNT:
        slot = m->state == M_CAST ? MA_CAST : MA_GLIDE;
        break;
    case MON_PHARAOH:
        slot = m->state == M_WHIP ? MA_WHIP : MA_WALK;
        bycell = slot == MA_WALK;
        break;
    default:
        break;
    }
    const mh_anim_t *a = rig_anim(r, slot, m->dir);
    if (!a) a = rig_anim(r, MA_WALK, m->dir);
    if (!a) a = rig_anim(r, MA_IDLE, m->dir);
    if (!a) {
        /* no art yet: the stand-in, in the monster's colours */
        mh_draw_t *d = put(l, w, pick(&c->stand_in, 0), MH_PX_LID, m->x, m->y, z);
        if (d) d->lut = lut;
        return;
    }
    int fr;
    if (bycell) fr = (int)(m->s * (float)a->n) % a->n;
    else if (m->state == M_STOMP || m->state == M_WHIP || m->state == M_CAST || m->state == M_TRANSFORM ||
             m->state == M_UNTRANSFORM || m->state == M_HOWL)
        fr = prog_frame(a, m->kind == MON_BRUTE ? m->t / 0.9f : m->state == M_HOWL ? m->t / 0.7f :
                               m->state == M_CAST ? m->timer / 0.7f : m->timer / 0.35f);
    else fr = loop_frame(a, m->anim);
    const mh_anim_t *sa = rig_shadow(r, slot, m->dir);
    if (sa) {
        float gz = m->z;
        if (m->kind == MON_CROW && m->state != M_PERCH) {
            const mh_level_t *lv = g->lv;
            int cx = mh_ifloor(m->x), cy = mh_ifloor(m->y);
            gz = mh_in(lv, cx, cy) ? mh_cell(lv, cx, cy)->h * FLOOR_M : 0;
            z = gz;     /* the dive frames were rendered with their shadow below */
        }
        mh_draw_t *d = put(l, w, pick(sa, fr), MH_PX_PLANE, m->x, m->y, gz);
        if (d) d->alpha = 140;
    }
    mh_draw_t *d = put(l, w, pick(a, fr), MH_PX_LID, m->x, m->y, z);
    if (d) d->lut = lut;
}

/* ---- the level's things ---- */

static void ob(mh_dlist_t *l, const mh_world_t *w, const mh_cast_t *c, int i, int frame, float x, float y, float z,
               int prio)
{
    if (!c->ob[i].n) return;
    if (c->ob_sh[i].n) {
        mh_draw_t *d = put(l, w, pick(&c->ob_sh[i], frame), MH_PX_PLANE, x, y, z);
        if (d) d->alpha = 130;
    }
    int fmt = c->ob[i].fmt;
    mh_draw_t *d = put(l, w, pick(&c->ob[i], frame), fmt, x, y, z);
    if (d) d->prio = (int8_t)prio;
}

static void things_draw(mh_scene_t *s, const mh_world_t *w, const mh_game_t *g, const mh_cast_t *c, mh_dlist_t *l)
{
    const mh_level_t *lv = g->lv;
    float t = s->anim_t;
    for (int i = 0; i < g->n_pick; i++) {
        const mh_pick_t *p = &g->pick[i];
        if (p->taken) continue;
        float x = p->x + 0.5f, y = p->y + 0.5f, z = p->z * FLOOR_M;
        if (!visible(s, w, x, y, z)) continue;
        int k = p->type == ENT_KEY ? OB_KEY : p->type == ENT_COIN ? OB_COIN : p->type == ENT_HEART ? OB_HEART :
                p->type == ENT_STICKER ? OB_STICKER : OB_HOURGLASS;
        ob(l, w, c, k, loop_frame(&c->ob[k], t + i * 0.13f), x, y, z, 0);
    }
    for (int i = 0; i < g->n_chest; i++) {
        const mh_chest_t *ch = &g->chest[i];
        int fr = ch->open ? prog_frame(&c->ob[OB_CHEST], ch->t / 0.35f) : 0;
        ob(l, w, c, OB_CHEST, fr, ch->x + 0.5f, ch->y + 0.5f, ch->z * FLOOR_M, 0);
    }
    for (int i = 0; i < g->n_lever; i++) {
        const mh_lever_t *lvr = &g->lever[i];
        int n = c->ob[OB_LEVER].n;
        int fr = 0;
        if (n >= 3) fr = lvr->t < 0.2f ? 1 : (lvr->on ? 2 : 0);
        else if (n) fr = lvr->on ? n - 1 : 0;
        ob(l, w, c, OB_LEVER, fr, lvr->x + 0.5f, lvr->y + 0.5f, lvr->z * FLOOR_M, 0);
    }
    for (int i = 0; i < g->n_cp; i++) {
        const mh_cp_t *cp = &g->cpt[i];
        float x = cp->x + 0.5f, y = cp->y + 0.5f, z = cp->z * FLOOR_M;
        if (cp->lit) ob(l, w, c, OB_LANTERN_ON, loop_frame(&c->ob[OB_LANTERN_ON], t), x, y, z, 0);
        else ob(l, w, c, OB_LANTERN_OFF, 0, x, y, z, 0);
    }
    if (g->exit_x >= 0) {
        int n = c->ob[OB_GATE].n;
        int fr = 0;
        if (g->exit_open && n) fr = prog_frame(&c->ob[OB_GATE], g->exit_t / 0.8f);
        ob(l, w, c, OB_GATE, fr, g->exit_x + 0.5f, g->exit_y + 0.5f, g->exit_z * FLOOR_M, 0);
    }
    for (int i = 0; i < g->n_crate; i++) {
        const mh_crate_t *cr = &g->crate[i];
        if (cr->sunk && cr->t >= 1) continue;
        float f = cr->t;
        float x = cr->mx + (cr->x - cr->mx) * f + 0.5f, y = cr->my + (cr->y - cr->my) * f + 0.5f;
        float z = cr->z * FLOOR_M;
        if (cr->sunk) z = (cr->z + 1) * FLOOR_M - f * FLOOR_M;
        ob(l, w, c, OB_CRATE, 0, x, y, z, 0);
    }
    for (int i = 0; i < g->n_plat; i++) {
        float x, y;
        mh_plat_pos(g, i, g->t, &x, &y);
        ob(l, w, c, OB_PLATFORM, loop_frame(&c->ob[OB_PLATFORM], t), x, y, g->plat[i].z * FLOOR_M, -1);
    }
    /* lanes */
    float pos[16];
    for (int li = 0; li < g->n_lane; li++) {
        const mh_lane_t *ln = &g->lane[li];
        int n = mh_lane_movers(g, li, pos, 16);
        float lz = ln->z * FLOOR_M;
        for (int k = 0; k < n; k++) {
            float along = pos[k];
            if (along < -(float)ln->size || along > ln->len + 0.5f) continue;
            float fx = ln->x + 0.5f + DXv[ln->dir] * along, fy = ln->y + 0.5f + DYv[ln->dir] * along;
            switch (ln->kind) {
            case LANE_CAR: {
                /* the art's anchor is its west cell */
                float ax = ln->dir == DIR_E ? fx : fx - (ln->size - 1);
                if (!visible(s, w, ax, fy, lz)) break;
                int i = ln->dir == DIR_E ? OB_RUNCAR_E : OB_RUNCAR_W;
                const mh_anim_t *a = &c->ob[i];
                if (!a->n) break;
                int fr = (int)(along * 4) % a->n;
                if (c->ob_sh[i].n) {
                    mh_draw_t *d = put(l, w, pick(&c->ob_sh[i], fr), MH_PX_PLANE, ax, fy, lz);
                    if (d) d->alpha = 140;
                }
                mh_draw_t *d = put(l, w, pick(a, fr), a->fmt, ax, fy, lz);
                if (d) d->lut = &s->car[(li + k) & 3];
                break;
            }
            case LANE_LOG:
                for (int p = 0; p < ln->size; p++) {
                    /* pieces from west to east */
                    float px = ln->dir == DIR_E ? fx + p : fx - p;
                    int west = ln->dir == DIR_E ? 0 : ln->size - 1;
                    int idx = ln->dir == DIR_E ? p : ln->size - 1 - p;
                    int oi = idx == 0 ? OB_LOG_W : idx == ln->size - 1 ? OB_LOG_E : OB_LOG_M;
                    (void)west;
                    if (!visible(s, w, px, fy, lz)) continue;
                    ob(l, w, c, oi, 0, px, fy, lz - 0.02f, -1);
                }
                break;
            case LANE_BOULDER: {
                int i = (ln->dir == DIR_E || ln->dir == DIR_W) ? OB_BOULDER_X : OB_BOULDER_Y;
                if (!visible(s, w, fx, fy, lz)) break;
                int fr = c->ob[i].n ? (int)(along * 2 * c->ob[i].n / 2) % c->ob[i].n : 0;
                if (ln->dir == DIR_W || ln->dir == DIR_S) fr = c->ob[i].n ? (c->ob[i].n - 1 - fr) : 0;
                ob(l, w, c, i, fr, fx, fy, lz, 0);
                break;
            }
            case LANE_SCARAB:
            case LANE_BAT: {
                const mh_rig_t *r = ln->kind == LANE_BAT ? &c->bat : &c->scarab;
                const mh_anim_t *a = rig_anim(r, ln->kind == LANE_BAT ? MA_FLY : MA_CRAWL, ln->dir);
                for (int p = 0; p < ln->size; p++) {
                    float px = fx - DXv[ln->dir] * p, py = fy - DYv[ln->dir] * p;
                    if (!visible(s, w, px, py, lz)) continue;
                    const mh_spr_t *sp = a ? pick(a, loop_frame(a, t + p * 0.07f)) : pick(&c->stand_in, 0);
                    mh_draw_t *d = put(l, w, sp, MH_PX_LID, px, py, lz);
                    if (d) d->lut = ln->kind == LANE_BAT ? &s->bat : &s->scarab;
                }
                break;
            }
            case LANE_LILY: {
                if (!visible(s, w, fx, fy, lz)) break;
                int nfr = c->ob[OB_LILY].n;
                float per = ln->step * 4.0f;
                float ph = fmodf(g->t + (float)k * per * 0.37f + ln->gap, per) / per;
                int fr = ph < 0.6f ? 0 : nfr > 1 ? 1 + (int)((ph - 0.6f) / 0.4f * (nfr - 1)) : 0;
                if (fr >= nfr) fr = nfr - 1;
                if (ph >= 0.97f) break;
                ob(l, w, c, OB_LILY, fr, fx, fy, lz - 0.17f, -1);
                break;
            }
            default:
                break;
            }
        }
    }
    for (int ti = 0; ti < g->n_trap; ti++) {
        const mh_trap_t *tr = &g->trap[ti];
        float ph;
        mh_trap_active(g, ti, &ph);
        float x = tr->x + 0.5f, y = tr->y + 0.5f, z = tr->z * FLOOR_M;
        if (!visible(s, w, x, y, z)) continue;
        switch (tr->kind) {
        case TRAP_SPIKES: {
            int fr = ph < 0.4f ? 0 : ph < 0.5f ? 1 : ph < 0.95f ? 2 : 1;
            ob(l, w, c, OB_SPIKES, fr, x, y, z, -1);
            break;
        }
        case TRAP_VENT: {
            int n = c->ob[OB_VENT].n;
            int fr = 0;
            if (n > 1 && ph >= 0.5f && ph < 0.95f) fr = 1 + (int)((ph - 0.5f) / 0.45f * (n - 1));
            if (fr >= n) fr = n - 1;
            ob(l, w, c, OB_VENT, fr, x, y, z, 0);
            break;
        }
        case TRAP_BEAR:
            ob(l, w, c, OB_BEAR, tr->sprung ? 1 : 0, x, y, z, -1);
            break;
        default:
            break;
        }
    }
    for (int i = 0; i < MH_MAX_DART; i++) {
        const mh_dart_t *d = &g->dart[i];
        if (!d->live) continue;
        bool xa = d->dir == DIR_E || d->dir == DIR_W;
        ob(l, w, c, xa ? OB_DART_X : OB_DART_Y, 0, d->x, d->y, d->z * FLOOR_M, 0);
    }
    for (int i = 0; i < MH_MAX_BOULDER; i++) {
        const mh_boulder_t *b = &g->boulder[i];
        if (!b->live) continue;
        bool xa = b->dir == DIR_E || b->dir == DIR_W;
        int oi = xa ? OB_BOULDER_X : OB_BOULDER_Y;
        int n = c->ob[oi].n;
        ob(l, w, c, oi, n ? (int)(b->roll * 2) % n : 0, b->x, b->y, b->z * FLOOR_M, 0);
    }
    for (int i = 0; i < MH_MAX_FX; i++) {
        const mh_fx_t *f = &g->fx[i];
        int oi = f->kind == FX_DUST ? OB_FX_DUST : f->kind == FX_SPLASH ? OB_FX_SPLASH : f->kind == FX_POOF ? OB_FX_POOF :
                 f->kind == FX_SPARKLE ? OB_FX_SPARKLE : f->kind == FX_BUBBLES ? OB_FX_BUBBLES : -1;
        if (oi < 0 || !c->ob[oi].n) continue;
        const mh_anim_t *a = &c->ob[oi];
        int ms = a->ms ? a->ms : 50;
        int fr = (int)(f->t * 1000.0f / (float)ms);
        if (fr >= a->n) continue;
        mh_draw_t *d = put(l, w, pick(a, fr), a->fmt, f->x, f->y, f->z);
        if (d) d->prio = 8;
    }
    (void)lv;
}

void mh_scene_build(mh_scene_t *s, const mh_world_t *w, const mh_game_t *g, const mh_cast_t *c,
                    mh_dlist_t *l, float dt)
{
    s->anim_t += dt;
    if (s->skin_fx == FX_RAINBOW) {
        s->fx_t += dt;
        if (s->fx_t > 0.08f) {
            s->fx_t = 0;
            /* the hue walks around the wheel, two seconds a turn */
            float hh = fmodf(s->anim_t * 0.5f, 1.0f) * 6.0f;
            int i = (int)hh;
            float f = hh - (float)i;
            int q = (int)(255 * (1 - f)), t = (int)(255 * f);
            int r = 255, gg = 255, b = 255;
            switch (i) {
            case 0: gg = t; b = 60; break;
            case 1: r = q; b = 60; break;
            case 2: r = 60; b = t; break;
            case 3: r = 60; gg = q; break;
            case 4: r = t; gg = 60; break;
            default: gg = 60; b = q; break;
            }
            s->body_pal.c[1] = ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)b;
            mh_lut_build(&s->body, &s->body_pal, s->tint, 0);
        }
    }
    pet_step(s, g, dt);
    bits_step(s, g, dt);
    /* the camera leads a little where Tommy faces */
    const mh_hero_t *h = &g->h;
    float lead = 0.8f;
    mh_scene_look(s, w, h->x + DXv[h->dir] * lead * 0.5f, h->y + DYv[h->dir] * lead, h->floor * FLOOR_M, dt, false);
    mh_dlist_clear(l);
    things_draw(s, w, g, c, l);
    for (int i = 0; i < g->n_mon; i++) mon_draw(s, w, g, c, l, &g->mon[i]);
    for (int i = 0; i < MH_MAX_DART; i++) (void)0;
    pet_draw(s, w, g, c, l);
    if (h->state != H_GONE) hero_draw(s, w, g, c, l);
    if (g->rival) rival_draw(s, w, g, c, l, dt);
    /* the count's bats in flight share the darts' slots */
    mh_dlist_sort(l);
}
