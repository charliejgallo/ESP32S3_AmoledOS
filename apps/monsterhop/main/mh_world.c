/*
 * MONSTER HOP - the world and its background cache (see mh_world.h)
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "mh_world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the zones' look ---- */

static const mh_zone_look_t s_looks[ZONE_N] = {
    /* city: a grey dusk, sodium lamps */
    { 0x2A2F3A, 0x0C0E14, 0xD8DCE6, 0x8A93A8, 0xB8D8E0 },
    /* castle: violet night */
    { 0x2A1740, 0x0A0612, 0xC8B8F0, 0x6A4A98, 0xC8B0FF },
    /* desert: a warm late afternoon */
    { 0x6A4630, 0x24140C, 0xFFE8C8, 0xC89A6A, 0xFFFFFF },
    /* forest: a green moonlit night */
    { 0x10281E, 0x040C08, 0xC8F0DC, 0x3A6A58, 0xD0FFF0 },
    /* test */
    { 0x1C2230, 0x080A10, 0xFFFFFF, 0x606878, 0xFFFFFF },
};

const mh_zone_look_t *mh_zone_look(int zone)
{
    return &s_looks[zone >= 0 && zone < ZONE_N ? zone : ZONE_TEST];
}

/* ---- life ---- */

bool mh_world_init(mh_world_t *w, const mh_level_t *lv)
{
    memset(w, 0, sizeof(*w));
    w->lv = lv;
    w->ox = 160;
    w->oy = 42 * lv->h + 260;
    w->lw = 60 * lv->w + 20 * lv->h + 320;
    w->lh = w->oy + 14 * lv->w + 46 + 120;
    w->dofs = (int)(7.23f * lv->w) + 180;
    w->cc = (uint16_t *)mh_malloc((size_t)MH_CW * MH_CH * 2);
    w->cd = (uint16_t *)mh_malloc((size_t)MH_CW * MH_CH * 2);
    if (!w->cc || !w->cd) {
        mh_world_free(w);
        return false;
    }
    char nm[40];
    for (int i = 1; i < lv->n_assets; i++) {
        if (!lv->asset[i][0]) continue;
        mh_art_load(lv->asset[i], &w->art[i]);
        snprintf(nm, sizeof nm, "%s_sh", lv->asset[i]);
        if (mh_art_has(nm)) mh_art_load(nm, &w->sh[i]);
        snprintf(nm, sizeof nm, "%s_gl", lv->asset[i]);
        if (mh_art_has(nm)) mh_art_load(nm, &w->gl[i]);
        mh_yield();
    }
    mh_world_invalidate_all(w);
    return true;
}

void mh_world_free(mh_world_t *w)
{
    for (int i = 0; i < MH_LV_MAXASSET; i++) {
        mh_anim_free(&w->art[i]);
        mh_anim_free(&w->sh[i]);
        mh_anim_free(&w->gl[i]);
    }
    free(w->cc);
    free(w->cd);
    w->cc = w->cd = NULL;
}

void mh_world_invalidate_all(mh_world_t *w)
{
    for (int j = 0; j < MH_CNY; j++)
        for (int i = 0; i < MH_CNX; i++) w->tag[j][i] = -1;
}

static inline int32_t tag_of(int bx, int by)
{
    return (int32_t)(((uint32_t)(by & 0x7FFF) << 16) | (uint32_t)(bx & 0xFFFF));
}

void mh_world_invalidate_cell(mh_world_t *w, int x, int y)
{
    /* the art of a cell reaches ~150 px around its anchor, and 240 above */
    float ax = mh_lpx(w, x + 0.5f, y + 0.5f), ay = mh_lpy(w, x + 0.5f, y + 0.5f, 0);
    int bx0 = ((int)ax - 150) / MH_CB, bx1 = ((int)ax + 150) / MH_CB;
    int by0 = ((int)ay - 260) / MH_CB, by1 = ((int)ay + 90) / MH_CB;
    for (int by = by0; by <= by1; by++) {
        for (int bx = bx0; bx <= bx1; bx++) {
            if (bx < 0 || by < 0) continue;
            int32_t *t = &w->tag[by % MH_CNY][bx % MH_CNX];
            if (*t == tag_of(bx, by)) *t = -1;
        }
    }
}

/* ---- drawing a block ---- */

typedef struct {
    int x0, y0;                 /* LP of the block's top-left             */
    uint16_t *c, *d;            /* the cache at that pixel                */
} blk_t;

enum { IT_TERRAIN = 0, IT_SHADOW, IT_PROP, IT_GLOW };

typedef struct {
    const mh_spr_t *s;
    int16_t x, y;               /* LP of the anchor                       */
    int16_t d;                  /* depth of the anchor                    */
    uint8_t kind;
    uint8_t fmt;
} item_t;

#define MAX_ITEMS 320

/* a COL sprite into the block: depth-tested, depth-writing */
static void blk_col(const blk_t *b, const mh_spr_t *s, int ax, int ay, int d0)
{
    int sx = ax - s->ax - b->x0, sy = ay - s->ay - b->y0;     /* block coords of the sprite's (0,0) */
    int r0 = sy < 0 ? -sy : 0, r1 = s->h;
    if (sy + r1 > MH_CB) r1 = MH_CB - sy;
    for (int r = r0; r < r1; r++) {
        int x0, x1;
        const uint8_t *p = mh_spr_row(s, r, &x0, &x1);
        int c0 = sx + x0, c1 = sx + x1;
        if (c0 < 0) { p += (size_t)(-c0) * 4; c0 = 0; }
        if (c1 > MH_CB) c1 = MH_CB;
        if (c0 >= c1) continue;
        uint16_t *dc = b->c + (size_t)(sy + r) * MH_CW;
        uint16_t *dd = b->d + (size_t)(sy + r) * MH_CW;
        for (int x = c0; x < c1; x++, p += 4) {
            int z = p[3];
            if (z == MH_Z_EMPTY) continue;
            int sd = d0 + z - 128;
            if (sd >= dd[x]) continue;
            int a = p[2];
            uint16_t col = (uint16_t)(p[0] | (p[1] << 8));
            dc[x] = mh_blend(dc[x], col, a);
            if (a >= 128) dd[x] = (uint16_t)sd;
        }
    }
}

/* a shadow plane: darkens what lies on the ground plane through the anchor */
static void blk_shadow(const blk_t *b, const mh_spr_t *s, int ax, int ay, int d0, int strength)
{
    int sx = ax - s->ax - b->x0, sy = ay - s->ay - b->y0;
    int r0 = sy < 0 ? -sy : 0, r1 = s->h;
    if (sy + r1 > MH_CB) r1 = MH_CB - sy;
    for (int r = r0; r < r1; r++) {
        int x0, x1;
        const uint8_t *p = mh_spr_row(s, r, &x0, &x1);
        int c0 = sx + x0, c1 = sx + x1;
        if (c0 < 0) { p += -c0; c0 = 0; }
        if (c1 > MH_CB) c1 = MH_CB;
        if (c0 >= c1) continue;
        /* the plane's depth on this row */
        int dp = d0 + mh_iround((float)(sy + r + b->y0 - ay) * MH_DPLANE_PX);
        uint16_t *dc = b->c + (size_t)(sy + r) * MH_CW;
        uint16_t *dd = b->d + (size_t)(sy + r) * MH_CW;
        for (int x = c0; x < c1; x++, p++) {
            int v = *p;
            if (!v) continue;
            int dz = (int)dd[x] - dp;
            if (dz < -3 || dz > 3) continue;
            dc[x] = mh_darken(dc[x], 256 - (v * strength >> 8));
        }
    }
}

static void blk_glow(const blk_t *b, const mh_spr_t *s, int ax, int ay, int d0)
{
    int sx = ax - s->ax - b->x0, sy = ay - s->ay - b->y0;
    int r0 = sy < 0 ? -sy : 0, r1 = s->h;
    if (sy + r1 > MH_CB) r1 = MH_CB - sy;
    for (int r = r0; r < r1; r++) {
        int x0, x1;
        const uint8_t *p = mh_spr_row(s, r, &x0, &x1);
        int c0 = sx + x0, c1 = sx + x1;
        if (c0 < 0) { p += (size_t)(-c0) * 2; c0 = 0; }
        if (c1 > MH_CB) c1 = MH_CB;
        if (c0 >= c1) continue;
        int dp = d0 + mh_iround((float)(sy + r + b->y0 - ay) * MH_DPLANE_PX);
        uint16_t *dc = b->c + (size_t)(sy + r) * MH_CW;
        uint16_t *dd = b->d + (size_t)(sy + r) * MH_CW;
        for (int x = c0; x < c1; x++, p += 2) {
            uint16_t g = (uint16_t)(p[0] | (p[1] << 8));
            if (!g) continue;
            int dz = (int)dd[x] - dp;
            /* the ground and what stands a little above it catch the light */
            if (dz < -40 || dz > 6) continue;
            dc[x] = mh_add(dc[x], g);
        }
    }
}

static const mh_spr_t *frame_of(const mh_anim_t *a, int x, int y)
{
    if (!a->n) return NULL;
    int k = a->n > 1 ? (int)(mh_hash2(x, y) % a->n) : 0;
    return &a->f[k];
}

static int cmp_depth(const void *pa, const void *pb)
{
    const item_t *a = (const item_t *)pa, *b = (const item_t *)pb;
    if (a->kind != b->kind) return (int)a->kind - (int)b->kind;
    return (int)b->d - (int)a->d;      /* far first */
}

static void add_item(item_t *it, int *n, const mh_spr_t *s, int x, int y, int d, int kind,
                     const blk_t *b)
{
    if (!s || *n >= MAX_ITEMS) return;
    /* the sprite's box against the block */
    int l = x - s->ax, t = y - s->ay;
    if (l >= b->x0 + MH_CB || t >= b->y0 + MH_CB || l + s->w <= b->x0 || t + s->h <= b->y0) return;
    item_t *e = &it[(*n)++];
    e->s = s;
    e->x = (int16_t)x;
    e->y = (int16_t)y;
    e->d = (int16_t)d;
    e->kind = (uint8_t)kind;
}

static int cell_h(const mh_level_t *lv, int x, int y)
{
    if (!mh_in(lv, x, y)) return -3;
    const mh_cell_t *c = mh_cell(lv, x, y);
    if (c->kind == CK_PIT) return -3;
    if (c->kind == CK_WATER || c->kind == CK_BRIDGE || c->kind == CK_QUICK) return c->h - 1;
    return c->h;
}

static void draw_block(mh_world_t *w, int bx, int by)
{
    const mh_level_t *lv = w->lv;
    blk_t b;
    b.x0 = bx * MH_CB;
    b.y0 = by * MH_CB;
    size_t base = (size_t)((b.y0 % MH_CH) * MH_CW + (b.x0 & (MH_CW - 1)));
    b.c = w->cc + base;
    b.d = w->cd + base;
    const mh_zone_look_t *look = mh_zone_look(lv->zone);
    for (int r = 0; r < MH_CB; r++) {
        int t = (b.y0 + r) * 256 / (w->lh > 0 ? w->lh : 1);
        if (t > 256) t = 256;
        uint16_t vc = mh_hex(mh_mix(look->void_top, look->void_bot, t));
        uint16_t *dc = b.c + (size_t)r * MH_CW, *dd = b.d + (size_t)r * MH_CW;
        for (int x = 0; x < MH_CB; x++) {
            dc[x] = vc;
            dd[x] = MH_DFAR;
        }
    }
    /* the cells whose art can reach the block: anchors within this LP box */
    float qx0 = (float)(b.x0 - 150 - w->ox), qx1 = (float)(b.x0 + MH_CB + 150 - w->ox);
    float qy0 = (float)(b.y0 - 110 - w->oy), qy1 = (float)(b.y0 + MH_CB + 260 - w->oy);
    float cx[4] = { qx0, qx1, qx0, qx1 }, cy[4] = { qy0, qy0, qy1, qy1 };
    float wx0 = 1e9f, wx1 = -1e9f, wy0 = 1e9f, wy1 = -1e9f;
    for (int k = 0; k < 4; k++) {
        float X = (42.0f * cx[k] + 20.0f * cy[k]) / 2800.0f;
        float Y = (14.0f * cx[k] - 60.0f * cy[k]) / 2800.0f;
        if (X < wx0) wx0 = X;
        if (X > wx1) wx1 = X;
        if (Y < wy0) wy0 = Y;
        if (Y > wy1) wy1 = Y;
    }
    int ix0 = mh_ifloor(wx0) - 1, ix1 = mh_ifloor(wx1) + 1, iy0 = mh_ifloor(wy0) - 1, iy1 = mh_ifloor(wy1) + 1;
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 >= lv->w) ix1 = lv->w - 1;
    if (iy1 >= lv->h) iy1 = lv->h - 1;

    static item_t it[MAX_ITEMS];
    int n = 0;
    for (int y = iy0; y <= iy1; y++) {
        for (int x = ix0; x <= ix1; x++) {
            const mh_cell_t *c = mh_cell(lv, x, y);
            float fx = x + 0.5f, fy = y + 0.5f;
            int lx = mh_iround(mh_lpx(w, fx, fy));
            if (c->kind == CK_GROUND && c->top) {
                /* the blocks under the top whose sides show */
                int hf = cell_h(lv, x, y - 1), hr = cell_h(lv, x + 1, y);
                int lo = hf < hr ? hf : hr;
                const mh_anim_t *fa = c->fill ? &w->art[c->fill] : &w->art[c->top];
                for (int k = c->h - 1; k >= -1 && k >= lo; k--) {
                    float z = mh_floor_z(k);
                    add_item(it, &n, frame_of(fa, x, y), lx, mh_iround(mh_lpy(w, fx, fy, z)),
                             mh_depth(w, fx, fy, z), IT_TERRAIN, &b);
                }
                float z = mh_floor_z(c->h);
                add_item(it, &n, frame_of(&w->art[c->top], x, y), lx, mh_iround(mh_lpy(w, fx, fy, z)),
                         mh_depth(w, fx, fy, z), IT_TERRAIN, &b);
            }
            if ((c->kind == CK_WATER || c->kind == CK_BRIDGE || c->kind == CK_QUICK) && c->surf) {
                float z = mh_floor_z(c->h) - 0.18f;
                add_item(it, &n, frame_of(&w->art[c->surf], x, y), lx, mh_iround(mh_lpy(w, fx, fy, z)),
                         mh_depth(w, fx, fy, z), IT_TERRAIN, &b);
            }
            if (c->kind == CK_BRIDGE && c->deck) {
                float z = mh_floor_z(c->h);
                add_item(it, &n, frame_of(&w->art[c->deck], x, y), lx, mh_iround(mh_lpy(w, fx, fy, z)),
                         mh_depth(w, fx, fy, z), IT_PROP, &b);
            }
            if ((c->flags & CF_ORIGIN) && c->prop) {
                float z = mh_floor_z((c->flags & CF_SUNK) ? c->h - 1 : c->h);
                int ly = mh_iround(mh_lpy(w, fx, fy, z)), d = mh_depth(w, fx, fy, z);
                add_item(it, &n, frame_of(&w->art[c->prop], x, y), lx, ly, d, IT_PROP, &b);
                if (w->sh[c->prop].n) add_item(it, &n, frame_of(&w->sh[c->prop], x, y), lx, ly, d, IT_SHADOW, &b);
                if (w->gl[c->prop].n) add_item(it, &n, frame_of(&w->gl[c->prop], x, y), lx, ly, d, IT_GLOW, &b);
            }
        }
    }
    qsort(it, (size_t)n, sizeof(item_t), cmp_depth);
    for (int i = 0; i < n; i++) {
        const item_t *e = &it[i];
        switch (e->kind) {
        case IT_TERRAIN:
        case IT_PROP:
            blk_col(&b, e->s, e->x, e->y, e->d);
            break;
        case IT_SHADOW:
            blk_shadow(&b, e->s, e->x, e->y, e->d, 150);
            break;
        case IT_GLOW:
            blk_glow(&b, e->s, e->x, e->y, e->d);
            break;
        }
    }
    w->tag[by % MH_CNY][bx % MH_CNX] = tag_of(bx, by);
    w->blocks_drawn++;
}

/* shadows are applied after the terrain and before the props: sort order
 * already does that (IT_TERRAIN < IT_SHADOW < IT_PROP < IT_GLOW) */

static bool valid(const mh_world_t *w, int bx, int by)
{
    return w->tag[by % MH_CNY][bx % MH_CNX] == tag_of(bx, by);
}

int mh_world_prepare(mh_world_t *w, int cam_x, int cam_y, int dirx, int diry, int extra)
{
    int drawn = 0;
    if (cam_x < 0) cam_x = 0;
    if (cam_y < 0) cam_y = 0;
    int bx0 = cam_x / MH_CB, bx1 = (cam_x + MH_W - 1) / MH_CB;
    int by0 = cam_y / MH_CB, by1 = (cam_y + MH_H - 1) / MH_CB;
    for (int by = by0; by <= by1; by++) {
        for (int bx = bx0; bx <= bx1; bx++) {
            if (!valid(w, bx, by)) {
                draw_block(w, bx, by);
                drawn++;
            }
        }
    }
    /* one column / row ahead, where the camera is heading (the ring has one
     * spare of each) */
    if (extra > 0 && dirx) {
        int bx = dirx > 0 ? bx1 + 1 : bx0 - 1;
        if (bx >= 0 && bx1 - bx0 + 1 < MH_CNX) {
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
        if (by >= 0 && by1 - by0 + 1 < MH_CNY) {
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
