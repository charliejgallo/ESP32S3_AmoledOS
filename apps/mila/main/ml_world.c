/*
 * MILA - the level on screen (see ml_world.h)
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "ml_world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the kit ---- */

/* props_<kit>: "prop_a prop_b ...\nprop2_x ..." written by the packer */
static int kit_names(const char *kit, char names[][32], int max1, char names2[][32], int max2, int *n2)
{
    char nm[40];
    snprintf(nm, sizeof nm, "props_%s", kit);
    uint32_t len = 0;
    char *b = (char *)ml_art_blob(nm, &len);
    int n1 = 0;
    *n2 = 0;
    if (!b) return 0;
    char *p = b, *end = b + len;
    while (p < end && *p) {
        while (p < end && (*p == ' ' || *p == '\n')) p++;
        char *q = p;
        while (q < end && *q && *q != ' ' && *q != '\n') q++;
        int l = (int)(q - p);
        if (l > 0 && l < 32) {
            bool two = !strncmp(p, "prop2_", 6);
            if (two && *n2 < max2) {
                snprintf(names2[*n2], 32, "%s_%.*s", kit, l, p);
                (*n2)++;
            } else if (!two && n1 < max1) {
                snprintf(names[n1], 32, "%s_%.*s", kit, l, p);
                n1++;
            }
        }
        if (q >= end || !*q) break;
        p = q + 1;
    }
    free(b);
    return n1;
}

void ml_kit_counts(const char *kit, int *n1, int *n2)
{
    char a[ML_MAX_PROPS][32], b[ML_MAX_PROPS2][32];
    *n1 = kit_names(kit, a, ML_MAX_PROPS, b, ML_MAX_PROPS2, n2);
}

static void load2(const char *kit, const char *what, ml_anim_t *out)
{
    char nm[48];
    snprintf(nm, sizeof nm, "%s_%s", kit, what);
    ml_art_load(nm, out);
}

bool ml_world_init(ml_world_t *w, const ml_level_t *lv, const ml_state_t *st, const char *kit)
{
    memset(w, 0, sizeof(*w));
    w->lv = lv;
    w->st = st;
    w->ox = 220;
    w->oy = 280;
    w->lw = 2 * w->ox + ML_CELL_W * lv->map.w;
    w->lh = w->oy + ML_CELL_H * lv->map.h + 120;
    w->dofs = 600;
    w->dplane = ML_DPLANE_PX;
    w->cc = (uint16_t *)ml_malloc((size_t)ML_CW * ML_CH * 2);
    w->cd = (uint16_t *)ml_malloc((size_t)ML_CW * ML_CH * 2);
    if (!w->cc || !w->cd) {
        ml_world_free(w);
        return false;
    }
    load2(kit, "floor", &w->floor);
    load2(kit, "wall", &w->wall);
    load2(kit, "wall_sh", &w->wall_sh);
    load2(kit, "target", &w->target);
    {
        char gn[48];
        snprintf(gn, sizeof gn, "%s_target_gl", kit);
        if (ml_art_has(gn)) ml_art_load(gn, &w->target_gl);
    }
    const ml_map_t *m = &lv->map;
    bool wet = false, plate = false, hole = false;
    for (int c = 0; c < ML_CELLS; c++) {
        int t = C_TERR(m->cell[c]);
        wet |= t == T_WET;
        plate |= t == T_PLATE;
        hole |= t == T_HOLE;
    }
    if (wet) load2(kit, "wet", &w->wet);
    if (plate) {
        load2(kit, "plate_up", &w->plate_up);
        load2(kit, "plate_down", &w->plate_down);
    }
    if (hole) {
        load2(kit, "hole", &w->hole);
        load2(kit, "hole_crate", &w->hole_fill);
    }
    ml_yield();
    char n1[ML_MAX_PROPS][32], n2[ML_MAX_PROPS2][32];
    w->nprop = kit_names(kit, n1, ML_MAX_PROPS, n2, ML_MAX_PROPS2, &w->nprop2);
    /* only the pieces this level uses */
    bool use1[ML_MAX_PROPS] = { false }, use2[ML_MAX_PROPS2] = { false };
    for (int c = 0; c < ML_CELLS; c++) {
        int k = lv->deco[c];
        if (k >= 1 && k <= ML_MAX_PROPS && k <= w->nprop) use1[k - 1] = true;
        if (k >= 64 && k < 64 + ML_MAX_PROPS2 && k - 64 < w->nprop2) use2[k - 64] = true;
    }
    char nm[48];
    for (int i = 0; i < w->nprop; i++) {
        if (!use1[i]) continue;
        ml_art_load(n1[i], &w->prop[i]);
        snprintf(nm, sizeof nm, "%.40s_sh", n1[i]);
        if (ml_art_has(nm)) ml_art_load(nm, &w->prop_sh[i]);
        snprintf(nm, sizeof nm, "%.40s_gl", n1[i]);
        if (ml_art_has(nm)) ml_art_load(nm, &w->prop_gl[i]);
        ml_yield();
    }
    for (int i = 0; i < w->nprop2; i++) {
        if (!use2[i]) continue;
        ml_art_load(n2[i], &w->prop2[i]);
        snprintf(nm, sizeof nm, "%.40s_sh", n2[i]);
        if (ml_art_has(nm)) ml_art_load(nm, &w->prop2_sh[i]);
    }
    ml_world_sync(w);
    ml_world_invalidate_all(w);
    return true;
}

void ml_world_free(ml_world_t *w)
{
    ml_anim_t *all[] = { &w->floor, &w->wall, &w->wall_sh, &w->target, &w->target_gl, &w->wet, &w->plate_up,
                         &w->plate_down, &w->hole, &w->hole_fill };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) ml_anim_free(all[i]);
    for (int i = 0; i < ML_MAX_PROPS; i++) {
        ml_anim_free(&w->prop[i]);
        ml_anim_free(&w->prop_sh[i]);
        ml_anim_free(&w->prop_gl[i]);
    }
    for (int i = 0; i < ML_MAX_PROPS2; i++) {
        ml_anim_free(&w->prop2[i]);
        ml_anim_free(&w->prop2_sh[i]);
    }
    free(w->cc);
    free(w->cd);
    w->cc = w->cd = NULL;
}

void ml_world_invalidate_all(ml_world_t *w)
{
    for (int j = 0; j < ML_CNY; j++)
        for (int i = 0; i < ML_CNX; i++) w->tag[j][i] = -1;
}

static inline int32_t tag_of(int bx, int by)
{
    return (int32_t)(((uint32_t)(by & 0x7FFF) << 16) | (uint32_t)(bx & 0xFFFF));
}

void ml_world_invalidate_cell(ml_world_t *w, int cell)
{
    float ax = ml_lpx(w, ml_cx(cell) + 0.5f), ay = ml_lpy(w, ml_cy(cell) + 0.5f, 0);
    int bx0 = ((int)ax - 110) / ML_CB, bx1 = ((int)ax + 110) / ML_CB;
    int by0 = ((int)ay - 160) / ML_CB, by1 = ((int)ay + 80) / ML_CB;
    for (int by = by0; by <= by1; by++) {
        for (int bx = bx0; bx <= bx1; bx++) {
            if (bx < 0 || by < 0) continue;
            int32_t *t = &w->tag[by % ML_CNY][bx % ML_CNX];
            if (*t == tag_of(bx, by)) *t = -1;
        }
    }
}

void ml_world_sync(ml_world_t *w)
{
    const ml_map_t *m = &w->lv->map;
    const ml_state_t *s = w->st;
    for (int c = 0; c < ML_CELLS; c++) {
        if (C_TERR(m->cell[c]) != T_PLATE) continue;
        uint8_t p = (uint8_t)(s->mila == c || ml_thing_at(m, s, c) >= 0);
        if (p != w->pressed[c]) {
            w->pressed[c] = p;
            ml_world_invalidate_cell(w, c);
        }
    }
    if (s->filled != w->filled) {
        uint16_t ch = s->filled ^ w->filled;
        w->filled = s->filled;
        for (int c = 0; c < ML_CELLS; c++)
            if (C_TERR(m->cell[c]) == T_HOLE && (ch >> m->hole[c] & 1)) ml_world_invalidate_cell(w, c);
    }
}

/* ---- what a cell shows ---- */

enum { IT_GROUND = 0, IT_DECAL, IT_SHADOW, IT_STAND, IT_GLOW };

typedef void (*item_fn)(void *ctx, const ml_spr_t *s, int cell, float z, int kind, int fmt);

static const ml_spr_t *pick(const ml_anim_t *a, int cell, uint32_t salt)
{
    if (!a->n) return NULL;
    uint32_t h = (uint32_t)cell * 2654435761u ^ salt;
    h ^= h >> 15;
    return &a->f[a->n > 1 ? h % a->n : 0];
}

static void cell_items(const ml_world_t *w, int c, item_fn fn, void *ctx)
{
    const ml_map_t *m = &w->lv->map;
    int t = C_TERR(m->cell[c]);
    int deco = w->lv->deco[c];
    const ml_spr_t *s;
    switch (t) {
    case T_VOID:
        return;
    case T_WALL:
        if (deco == DECO_HIDDEN) return;
        if (deco == DECO_WALL) {
            if ((s = pick(&w->wall, c, 7))) fn(ctx, s, c, 0, IT_STAND, ML_PX_COL);
            if ((s = pick(&w->wall_sh, c, 7))) fn(ctx, s, c, 0, IT_SHADOW, ML_PX_PLANE);
            return;
        }
        if ((s = pick(&w->floor, c, 1))) fn(ctx, s, c, 0, IT_GROUND, ML_PX_COL);
        if (deco >= 1 && deco <= w->nprop) {
            if ((s = pick(&w->prop[deco - 1], c, 3))) fn(ctx, s, c, 0, IT_STAND, ML_PX_COL);
            if ((s = pick(&w->prop_sh[deco - 1], c, 3))) fn(ctx, s, c, 0, IT_SHADOW, ML_PX_PLANE);
            if ((s = pick(&w->prop_gl[deco - 1], c, 3))) fn(ctx, s, c, 0, IT_GLOW, ML_PX_GLOW);
        } else if (deco >= 64 && deco - 64 < w->nprop2) {
            if ((s = pick(&w->prop2[deco - 64], c, 3))) fn(ctx, s, c, 0, IT_STAND, ML_PX_COL);
            if ((s = pick(&w->prop2_sh[deco - 64], c, 3))) fn(ctx, s, c, 0, IT_SHADOW, ML_PX_PLANE);
        }
        return;
    case T_WET:
        s = pick(&w->wet, c, 1);
        if (!s) s = pick(&w->floor, c, 1);
        if (s) fn(ctx, s, c, 0, IT_GROUND, ML_PX_COL);
        break;
    case T_HOLE:
        if (w->st->filled >> m->hole[c] & 1) {
            if ((s = pick(&w->hole_fill, c, 1))) fn(ctx, s, c, 0, IT_GROUND, ML_PX_COL);
        } else if ((s = pick(&w->hole, c, 1))) {
            fn(ctx, s, c, 0, IT_GROUND, ML_PX_COL);
        }
        return;
    default:
        if ((s = pick(&w->floor, c, 1))) fn(ctx, s, c, 0, IT_GROUND, ML_PX_COL);
        break;
    }
    if (t == T_PLATE) {
        const ml_anim_t *pa = w->pressed[c] ? &w->plate_down : &w->plate_up;
        if ((s = pick(pa, c, 1))) fn(ctx, s, c, 0, IT_DECAL, ML_PX_COL);
    }
    if ((m->cell[c] & C_TARGET) && (s = pick(&w->target, c, 1))) {
        fn(ctx, s, c, 0, IT_DECAL, ML_PX_COL);
        if ((s = pick(&w->target_gl, c, 1))) fn(ctx, s, c, 0, IT_GLOW, ML_PX_GLOW);
    }
}

/* ---- drawing a block ---- */

typedef struct {
    int x0, y0;                 /* LP of the block's top-left             */
    uint16_t *c, *d;
} blk_t;

typedef struct {
    const ml_spr_t *s;
    int16_t x, y, d;
    uint8_t kind;
} item_t;

#define MAX_ITEMS 256

static void blk_col(const blk_t *b, const ml_spr_t *s, int ax, int ay, int d0, int slack)
{
    int sx = ax - s->ax - b->x0, sy = ay - s->ay - b->y0;
    int r0 = sy < 0 ? -sy : 0, r1 = s->h;
    if (sy + r1 > ML_CB) r1 = ML_CB - sy;
    for (int r = r0; r < r1; r++) {
        int x0, x1;
        const uint8_t *p = ml_spr_row(s, r, &x0, &x1);
        int c0 = sx + x0, c1 = sx + x1;
        if (c0 < 0) { p += (size_t)(-c0) * 4; c0 = 0; }
        if (c1 > ML_CB) c1 = ML_CB;
        if (c0 >= c1) continue;
        uint16_t *dc = b->c + (size_t)(sy + r) * ML_CW;
        uint16_t *dd = b->d + (size_t)(sy + r) * ML_CW;
        for (int x = c0; x < c1; x++, p += 4) {
            int z = p[3];
            if (z == ML_Z_EMPTY) continue;
            int sd = d0 + z - 128;
            if (sd >= (int)dd[x] + slack) continue;
            int a = p[2];
            dc[x] = ml_blend(dc[x], (uint16_t)(p[0] | (p[1] << 8)), a);
            if (a >= 128) dd[x] = (uint16_t)(sd < 0 ? 0 : sd);
        }
    }
}

/* a shadow into the block's shadow buffer: the darkest wins */
static void blk_shadow(const blk_t *b, const ml_spr_t *s, int ax, int ay, uint8_t *shb)
{
    int sx = ax - s->ax - b->x0, sy = ay - s->ay - b->y0;
    int r0 = sy < 0 ? -sy : 0, r1 = s->h;
    if (sy + r1 > ML_CB) r1 = ML_CB - sy;
    for (int r = r0; r < r1; r++) {
        int x0, x1;
        const uint8_t *p = ml_spr_row(s, r, &x0, &x1);
        int c0 = sx + x0, c1 = sx + x1;
        if (c0 < 0) { p += -c0; c0 = 0; }
        if (c1 > ML_CB) c1 = ML_CB;
        uint8_t *dst = shb + (size_t)(sy + r) * ML_CB;
        for (int x = c0; x < c1; x++, p++)
            if (*p > dst[x]) dst[x] = *p;
    }
}

/* the floor plane's depth on an LP row */
static inline int plane_depth(const ml_world_t *w, int lpy)
{
    return ml_iround(ML_DGY * (float)(lpy - w->oy) / ML_CELL_H) + w->dofs;
}

/* a light pool: added to the floor and what stands a little above it */
static void blk_glow(const ml_world_t *w, const blk_t *b, const ml_spr_t *s, int ax, int ay)
{
    int sx = ax - s->ax - b->x0, sy = ay - s->ay - b->y0;
    int r0 = sy < 0 ? -sy : 0, r1 = s->h;
    if (sy + r1 > ML_CB) r1 = ML_CB - sy;
    for (int r = r0; r < r1; r++) {
        int x0, x1;
        const uint8_t *p = ml_spr_row(s, r, &x0, &x1);
        int c0 = sx + x0, c1 = sx + x1;
        if (c0 < 0) { p += (size_t)(-c0) * 2; c0 = 0; }
        if (c1 > ML_CB) c1 = ML_CB;
        int dp = plane_depth(w, b->y0 + sy + r);
        uint16_t *dc = b->c + (size_t)(sy + r) * ML_CW;
        const uint16_t *dd = b->d + (size_t)(sy + r) * ML_CW;
        for (int x = c0; x < c1; x++, p += 2) {
            uint16_t g = (uint16_t)(p[0] | (p[1] << 8));
            if (!g) continue;
            int dz = (int)dd[x] - dp;
            if (dz < -30 || dz > 4) continue;
            dc[x] = ml_add(dc[x], g);
        }
    }
}


/* darkens the floor under the shadow buffer (only the floor: a prop never
 * darkens itself, a wall top in front stays lit) */
static void blk_apply_shadow(const ml_world_t *w, const blk_t *b, const uint8_t *shb, int strength)
{
    for (int r = 0; r < ML_CB; r++) {
        const uint8_t *sp = shb + (size_t)r * ML_CB;
        int dp = plane_depth(w, b->y0 + r);
        uint16_t *dc = b->c + (size_t)r * ML_CW;
        const uint16_t *dd = b->d + (size_t)r * ML_CW;
        for (int x = 0; x < ML_CB; x++) {
            int v = sp[x];
            if (!v) continue;
            int dz = (int)dd[x] - dp;
            if (dz < -3 || dz > 3) continue;
            dc[x] = ml_darken(dc[x], 256 - (v * strength >> 8));
        }
    }
}

typedef struct {
    const ml_world_t *w;
    const blk_t *b;
    item_t *it;
    int n;
} gather_t;

static void gather(void *ctx, const ml_spr_t *s, int cell, float z, int kind, int fmt)
{
    (void)fmt;
    gather_t *g = (gather_t *)ctx;
    if (g->n >= MAX_ITEMS) return;
    float gx = ml_cx(cell) + 0.5f, gy = ml_cy(cell) + 0.5f;
    int x = ml_iround(ml_lpx(g->w, gx)), y = ml_iround(ml_lpy(g->w, gy, z));
    int l = x - s->ax, t = y - s->ay;
    const blk_t *b = g->b;
    if (l >= b->x0 + ML_CB || t >= b->y0 + ML_CB || l + s->w <= b->x0 || t + s->h <= b->y0) return;
    item_t *e = &g->it[g->n++];
    e->s = s;
    e->x = (int16_t)x;
    e->y = (int16_t)y;
    e->d = (int16_t)ml_depth(g->w, gy, z);
    e->kind = (uint8_t)kind;
}

static int cmp_item(const void *pa, const void *pb)
{
    const item_t *a = (const item_t *)pa, *b = (const item_t *)pb;
    if (a->kind != b->kind) return (int)a->kind - (int)b->kind;
    return (int)b->d - (int)a->d;      /* far first */
}

static void draw_block(ml_world_t *w, int bx, int by)
{
    const ml_map_t *m = &w->lv->map;
    blk_t b;
    b.x0 = bx * ML_CB;
    b.y0 = by * ML_CB;
    size_t base = (size_t)((b.y0 % ML_CH) * ML_CW + (b.x0 & (ML_CW - 1)));
    b.c = w->cc + base;
    b.d = w->cd + base;
    for (int r = 0; r < ML_CB; r++) {
        uint16_t *dc = b.c + (size_t)r * ML_CW, *dd = b.d + (size_t)r * ML_CW;
        for (int x = 0; x < ML_CB; x++) {
            dc[x] = 0;
            dd[x] = ML_DFAR;
        }
    }
    /* the cells whose art can reach the block */
    int ix0 = (b.x0 - 110 - w->ox) / ML_CELL_W - 1, ix1 = (b.x0 + ML_CB + 110 - w->ox) / ML_CELL_W + 1;
    int iy0 = (b.y0 - 80 - w->oy) / ML_CELL_H - 1, iy1 = (b.y0 + ML_CB + 170 - w->oy) / ML_CELL_H + 1;
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 >= m->w) ix1 = m->w - 1;
    if (iy1 >= m->h) iy1 = m->h - 1;
    static item_t it[MAX_ITEMS];
    gather_t g = { w, &b, it, 0 };
    for (int y = iy0; y <= iy1; y++)
        for (int x = ix0; x <= ix1; x++) cell_items(w, y * ML_MAXW + x, gather, &g);
    qsort(it, (size_t)g.n, sizeof(item_t), cmp_item);
    /* shadows: the darkest per pixel, applied once to the floor (two walls'
     * shadows overlapping would otherwise darken twice: a saw-tooth) */
    static uint8_t shb[ML_CB * ML_CB];
    bool any_sh = false, applied = false;
    for (int i = 0; i < g.n; i++) {
        const item_t *e = &it[i];
        if (e->kind == IT_STAND && any_sh && !applied) {
            blk_apply_shadow(w, &b, shb, 140);
            applied = true;
        }
        switch (e->kind) {
        case IT_GROUND: blk_col(&b, e->s, e->x, e->y, e->d, 0); break;
        case IT_DECAL:  blk_col(&b, e->s, e->x, e->y, e->d, 3); break;
        case IT_SHADOW:
            if (!any_sh) memset(shb, 0, sizeof shb);
            any_sh = true;
            blk_shadow(&b, e->s, e->x, e->y, shb);
            break;
        case IT_STAND:  blk_col(&b, e->s, e->x, e->y, e->d, 0); break;
        case IT_GLOW:
            if (any_sh && !applied) {
                blk_apply_shadow(w, &b, shb, 140);
                applied = true;
            }
            blk_glow(w, &b, e->s, e->x, e->y);
            break;
        }
    }
    if (any_sh && !applied) blk_apply_shadow(w, &b, shb, 140);
    w->tag[by % ML_CNY][bx % ML_CNX] = tag_of(bx, by);
    w->blocks_drawn++;
}

static bool valid(const ml_world_t *w, int bx, int by)
{
    return w->tag[by % ML_CNY][bx % ML_CNX] == tag_of(bx, by);
}

int ml_world_prepare(ml_world_t *w, int cam_x, int cam_y, int dirx, int diry, int extra)
{
    int drawn = 0;
    if (cam_x < 0) cam_x = 0;
    if (cam_y < 0) cam_y = 0;
    int bx0 = cam_x / ML_CB, bx1 = (cam_x + ML_W - 1) / ML_CB;
    int by0 = cam_y / ML_CB, by1 = (cam_y + ML_H - 1) / ML_CB;
    for (int by = by0; by <= by1; by++) {
        for (int bx = bx0; bx <= bx1; bx++) {
            if (!valid(w, bx, by)) {
                draw_block(w, bx, by);
                drawn++;
            }
        }
    }
    if (extra > 0 && dirx) {
        int bx = dirx > 0 ? bx1 + 1 : bx0 - 1;
        if (bx >= 0 && bx1 - bx0 + 1 < ML_CNX) {
            for (int by = by0; by <= by1 && extra > 0; by++) {
                if (!valid(w, bx, by)) {
                    draw_block(w, bx, by);
                    drawn++;
                    extra--;
                }
            }
        }
    }
    if (extra > 0 && diry) {
        int by = diry > 0 ? by1 + 1 : by0 - 1;
        if (by >= 0 && by1 - by0 + 1 < ML_CNY) {
            for (int bx = bx0; bx <= bx1 && extra > 0; bx++) {
                if (!valid(w, bx, by)) {
                    draw_block(w, bx, by);
                    drawn++;
                    extra--;
                }
            }
        }
    }
    return drawn;
}

/* ---- the overview: a painter, sprites box-filtered to the scale ---- */

void ml_overview_fit(const ml_world_t *w, int sw, int sh, int top, int bottom, ml_ov_view_t *v)
{
    const ml_map_t *m = &w->lv->map;
    /* the level's picture: the grid plus what stands on its top row */
    float x0 = (float)w->ox, x1 = (float)(w->ox + ML_CELL_W * m->w);
    float y0 = (float)w->oy - 60.0f, y1 = (float)(w->oy + ML_CELL_H * m->h + 10);
    float s = (float)(sw - 12) / (x1 - x0);
    float s2 = (float)(sh - top - bottom) / (y1 - y0);
    if (s2 < s) s = s2;
    if (s > 1.0f) s = 1.0f;
    v->scale = s;
    v->ox = (float)sw * 0.5f - (x0 + x1) * 0.5f * s;
    v->oy = (float)top + (float)(sh - top - bottom) * 0.5f - (y0 + y1) * 0.5f * s;
}

typedef struct {
    ml_ov_item_t it[640];
    int n;
    const ml_world_t *w;
} ov_list_t;

static void ov_gather(void *ctx, const ml_spr_t *s, int cell, float z, int kind, int fmt)
{
    ov_list_t *l = (ov_list_t *)ctx;
    if (l->n >= (int)(sizeof l->it / sizeof l->it[0])) return;
    ml_ov_item_t *e = &l->it[l->n++];
    e->s = s;
    e->lut = NULL;
    e->gx = ml_cx(cell) + 0.5f;
    e->gy = ml_cy(cell) + 0.5f;
    e->z = z;
    e->d = (int16_t)ml_depth(l->w, e->gy, z);
    e->fmt = (uint8_t)fmt;
    if (kind == IT_GLOW) {
        l->n--;
        return;
    }
    e->layer = (uint8_t)(kind == IT_GROUND ? 0 : kind == IT_DECAL ? 1 : kind == IT_SHADOW ? 2 : 3);
}

static int cmp_ov(const void *pa, const void *pb)
{
    const ml_ov_item_t *a = (const ml_ov_item_t *)pa, *b = (const ml_ov_item_t *)pb;
    if (a->layer != b->layer) return (int)a->layer - (int)b->layer;
    return (int)b->d - (int)a->d;
}

/* one sprite scaled by s with n x n taps per pixel */
static void ov_draw(const ml_world_t *w, const ml_ov_view_t *v, uint16_t *dst, const ml_ov_item_t *e)
{
    const ml_spr_t *s = e->s;
    float sc = v->scale;
    float lx = ml_lpx(w, e->gx) - s->ax, ly = ml_lpy(w, e->gy, e->z) - s->ay;
    float sx0 = v->ox + lx * sc, sy0 = v->oy + ly * sc;
    int dx0 = ml_ifloor(sx0), dy0 = ml_ifloor(sy0);
    int dx1 = ml_ifloor(sx0 + s->w * sc) + 1, dy1 = ml_ifloor(sy0 + s->h * sc) + 1;
    if (dx0 < 0) dx0 = 0;
    if (dy0 < 0) dy0 = 0;
    if (dx1 > ML_W) dx1 = ML_W;
    if (dy1 > ML_H) dy1 = ML_H;
    int n = (int)(1.0f / sc + 0.99f);
    if (n < 1) n = 1;
    if (n > 4) n = 4;
    float step = 1.0f / (sc * (float)n);
    int bpp = e->fmt == ML_PX_COL ? 4 : e->fmt == ML_PX_LID ? 3 : 1;
    for (int y = dy0; y < dy1; y++) {
        uint16_t *row = dst + (size_t)y * ML_W;
        for (int x = dx0; x < dx1; x++) {
            int ar = 0, ag = 0, ab = 0, aa = 0;
            float u0 = ((float)x - sx0) / sc, v0 = ((float)y - sy0) / sc;
            for (int j = 0; j < n; j++) {
                int sy = ml_ifloor(v0 + (j + 0.5f) * step);
                if (sy < 0 || sy >= s->h) continue;
                int x0, x1;
                const uint8_t *p = ml_spr_row(s, sy, &x0, &x1);
                for (int i = 0; i < n; i++) {
                    int sx = ml_ifloor(u0 + (i + 0.5f) * step);
                    if (sx < x0 || sx >= x1) continue;
                    const uint8_t *q = p + (size_t)(sx - x0) * bpp;
                    int a, r, g, b;
                    if (e->fmt == ML_PX_COL) {
                        a = q[2];
                        ml_unpack((uint16_t)(q[0] | (q[1] << 8)), &r, &g, &b);
                    } else if (e->fmt == ML_PX_LID) {
                        a = (q[0] & 15) * 17;
                        if (!e->lut) continue;
                        ml_unpack(e->lut->c[q[0] >> 4][q[1] >> 2], &r, &g, &b);
                    } else {
                        a = q[0];
                        r = g = b = 0;
                    }
                    ar += r * a;
                    ag += g * a;
                    ab += b * a;
                    aa += a;
                }
            }
            if (!aa) continue;
            int cnt = n * n;
            int alpha = aa / cnt;
            if (e->fmt == ML_PX_PLANE) {
                row[x] = ml_darken(row[x], 256 - (alpha * 140 >> 8));
                continue;
            }
            uint16_t c = ml_rgb(ar / aa, ag / aa, ab / aa);
            row[x] = ml_blend(row[x], c, alpha);
        }
    }
}

void ml_overview_paint(const ml_world_t *w, const ml_ov_view_t *v, uint16_t *dst,
                       const ml_ov_item_t *extra, int nextra)
{
    memset(dst, 0, (size_t)ML_W * ML_H * 2);
    static ov_list_t l;
    l.n = 0;
    l.w = w;
    const ml_map_t *m = &w->lv->map;
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) cell_items(w, y * ML_MAXW + x, ov_gather, &l);
    for (int i = 0; i < nextra && l.n < (int)(sizeof l.it / sizeof l.it[0]); i++) l.it[l.n++] = extra[i];
    qsort(l.it, (size_t)l.n, sizeof l.it[0], cmp_ov);
    for (int i = 0; i < l.n; i++) {
        if (l.it[i].s) ov_draw(w, v, dst, &l.it[i]);
        if ((i & 31) == 31) ml_yield();
    }
}

void ml_zoom_band(const uint16_t *ov, const ml_ov_view_t *v, ml_img_t *im, int y0, int y1, float cx, float cy,
                  float sc, int a)
{
    if (a <= 0) return;
    /* screen (x, y) -> LP -> overview pixel, all linear: u = ku * x + u0 */
    float k = v->scale / sc;
    float u0 = v->ox + (cx - (ML_W / 2.0f) / sc) * v->scale;
    float v0 = v->oy + (cy - (ML_H / 2.0f) / sc) * v->scale;
    for (int y = y0; y < y1; y++) {
        uint16_t *row = im->px + (size_t)y * im->w;
        float fv = v0 + y * k;
        int iy = ml_ifloor(fv);
        int wy = (int)((fv - iy) * 256);
        if (iy < 0 || iy + 1 >= ML_H) {
            if (a >= 255) memset(row, 0, ML_W * 2);
            continue;
        }
        const uint16_t *r0 = ov + (size_t)iy * ML_W, *r1 = r0 + ML_W;
        for (int x = 0; x < ML_W; x++) {
            float fu = u0 + x * k;
            int ix = ml_ifloor(fu);
            uint16_t c;
            if (ix < 0 || ix + 1 >= ML_W) {
                c = 0;
            } else {
                int wx = (int)((fu - ix) * 256);
                uint16_t t = ml_blend(r0[ix], r0[ix + 1], wx);
                uint16_t b = ml_blend(r1[ix], r1[ix + 1], wx);
                c = ml_blend(t, b, wy);
            }
            row[x] = a >= 255 ? c : ml_blend(row[x], c, a);
        }
    }
}

bool ml_world_backdrop(ml_world_t *w, const ml_spr_t *s, int x, int y, int d, float dplane)
{
    if (!w->cc) w->cc = (uint16_t *)ml_malloc((size_t)ML_CW * ML_CH * 2);
    if (!w->cd) w->cd = (uint16_t *)ml_malloc((size_t)ML_CW * ML_CH * 2);
    if (!w->cc || !w->cd) return false;
    w->dplane = dplane;
    w->dofs = d;
    memset(w->cc, 0, (size_t)ML_CW * ML_CH * 2);
    for (size_t i = 0; i < (size_t)ML_CW * ML_CH; i++) w->cd[i] = ML_DFAR;
    if (!s) return true;
    /* the whole picture, block by block (the cache's blocks are just
     * windows onto the same arrays) */
    for (int by = 0; by < ML_CNY; by++) {
        for (int bx = 0; bx < ML_CNX; bx++) {
            blk_t b;
            b.x0 = bx * ML_CB;
            b.y0 = by * ML_CB;
            b.c = w->cc + (size_t)b.y0 * ML_CW + b.x0;
            b.d = w->cd + (size_t)b.y0 * ML_CW + b.x0;
            blk_col(&b, s, x, y, d, 0);
        }
    }
    return true;
}

