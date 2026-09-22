/*
 * GOLF - the art rendered in Blender (see gf_art.h)
 */
/* The .so is compiled with -Os (components/elf_loader/elf_loader.cmake) and
 * per-file CMake options do not reach that compile: this is the only way to
 * give the pixel and physics loops -O2. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "gf_art.h"
#include "aos_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRY_SIZE  50
#define NAME_LEN    24
#define HATS        6

enum { T_SPRITE = 1, T_PLANE = 2, T_GOLFER = 3 };

typedef struct {
    char     name[NAME_LEN];
    uint8_t  type, flags;
    uint16_t w, h;
    int16_t  ax, ay;
    uint32_t off, clen, rawlen;
    uint16_t ppm8, extra;
} entry_t;

struct gf_tree_art {
    gf_mip_t side[GF_TREE_KINDS], top[GF_TREE_KINDS], shadow[GF_TREE_KINDS];
    float    height[GF_TREE_KINDS], diam[GF_TREE_KINDS];
    bool     ok[GF_TREE_KINDS];
    gf_mip_t flag[4];
    int      nflag;
};

#define MAX_FRAMES  32

typedef struct {
    int16_t     body, shadow, hat[HATS];    /* entry indices, -1 = none      */
    gf_sprite_t spr;                        /* coloured, NULL px until used  */
    int16_t     x0, y0;
    gf_sprite_t shd;
    int16_t     sx0, sy0;
} frame_t;

/* The pack stays on the card: only its table is in memory, and an entry is
 * read when it is needed (the trees at load, the golfer's frames when an
 * outfit is coloured, always from the worker task). Keeping the 1.1 MB file
 * in PSRAM left the board with under 1 MB free in the middle of a round. */
static FILE         *s_fp;
static size_t        s_pak_len;
static entry_t      *s_ent;
static int           s_nent;
static struct gf_tree_art s_trees;
static bool          s_trees_ok;
static frame_t       s_frames[SEQ_N][MAX_FRAMES];
static int           s_nframes[SEQ_N];
static uint32_t      s_pal[RG_N];
static int           s_hat = -1;
static volatile unsigned s_ready_mask;   /* sequences coloured since the last outfit */

static const char *const SEQ_NAME[SEQ_N] = { "swing", "idle", "cheer", "sad", "turn" };
static const char *const TREE_NAME[GF_TREE_KINDS] = { "tree_pine", "tree_oak", "tree_poplar", "tree_palm", "bush" };

/* --------------------------------------------------------------------------
 * The pack
 * -------------------------------------------------------------------------- */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* The standard LZ4 block format. */
static bool lz4_decode(const uint8_t *src, size_t slen, uint8_t *dst, size_t dlen)
{
    size_t i = 0, o = 0;
    while (i < slen) {
        uint8_t tok = src[i++];
        size_t lit = tok >> 4;
        if (lit == 15) {
            uint8_t b;
            do {
                if (i >= slen) return false;
                b = src[i++];
                lit += b;
            } while (b == 255);
        }
        if (i + lit > slen || o + lit > dlen) return false;
        memcpy(dst + o, src + i, lit);
        i += lit;
        o += lit;
        if (i >= slen) break;
        if (i + 2 > slen) return false;
        size_t off = (size_t)src[i] | ((size_t)src[i + 1] << 8);
        i += 2;
        size_t m = (size_t)(tok & 15) + 4;
        if ((tok & 15) == 15) {
            uint8_t b;
            do {
                if (i >= slen) return false;
                b = src[i++];
                m += b;
            } while (b == 255);
        }
        if (off == 0 || off > o || o + m > dlen) return false;
        for (size_t k = 0; k < m; k++, o++) {
            dst[o] = dst[o - off];
        }
    }
    return o == dlen;
}

static int find(const char *name)
{
    for (int i = 0; i < s_nent; i++) {
        if (!strncmp(s_ent[i].name, name, NAME_LEN)) return i;
    }
    return -1;
}

static uint8_t *unpack(int idx)
{
    const entry_t *e = &s_ent[idx];
    if (!s_fp || e->off + e->clen > s_pak_len) return NULL;
    uint8_t *raw = (uint8_t *)gf_malloc(e->rawlen ? e->rawlen : 1);
    uint8_t *src = (uint8_t *)gf_malloc(e->clen ? e->clen : 1);
    bool ok = raw && src && fseek(s_fp, (long)e->off, SEEK_SET) == 0 &&
              fread(src, 1, e->clen, s_fp) == e->clen && lz4_decode(src, e->clen, raw, e->rawlen);
    free(src);
    if (!ok) {
        free(raw);
        return NULL;
    }
    return raw;
}

/* a colour sprite or an alpha plane, into a mip chain */
static bool load_mip(const char *name, gf_mip_t *m)
{
    int i = find(name);
    if (i < 0) return false;
    const entry_t *e = &s_ent[i];
    uint8_t *raw = unpack(i);
    if (!raw) return false;
    gf_sprite_t s = { 0 };
    s.w = (int16_t)e->w;
    s.h = (int16_t)e->h;
    s.ox = e->ax;
    s.oy = e->ay;
    size_t n = (size_t)e->w * e->h;
    if (e->type == T_SPRITE) {
        s.px = (uint16_t *)gf_malloc(n * 2);
        s.a = (uint8_t *)gf_malloc(n);
        if (!s.px || !s.a) {
            free(s.px);
            free(s.a);
            free(raw);
            return false;
        }
        for (size_t k = 0; k < n; k++) {
            s.px[k] = rd16(raw + 2 * k);
        }
        memcpy(s.a, raw + 2 * n, n);
        free(raw);
    } else {
        s.px = NULL;
        s.a = raw;          /* the plane is the alpha */
    }
    gf_mip_build(m, &s);
    m->ppm = (float)e->ppm8 / 8.0f;
    return true;
}

static void load_trees(void)
{
    memset(&s_trees, 0, sizeof(s_trees));
    char nm[NAME_LEN];
    int any = 0;
    for (int k = 0; k < GF_TREE_KINDS; k++) {
        snprintf(nm, sizeof nm, "%.12s_s", TREE_NAME[k]);
        bool a = load_mip(nm, &s_trees.side[k]);
        int si = find(nm);
        snprintf(nm, sizeof nm, "%.12s_t", TREE_NAME[k]);
        bool b = load_mip(nm, &s_trees.top[k]);
        int ti = find(nm);
        snprintf(nm, sizeof nm, "%.12s_h", TREE_NAME[k]);
        bool c = load_mip(nm, &s_trees.shadow[k]);
        s_trees.ok[k] = a && b && c;
        if (si >= 0) s_trees.height[k] = (float)s_ent[si].extra / 100.0f;
        if (ti >= 0) s_trees.diam[k] = (float)s_ent[ti].extra / 100.0f;
        any += s_trees.ok[k];
    }
    for (int f = 0; f < 4; f++) {
        snprintf(nm, sizeof nm, "flag_%d", f);
        if (load_mip(nm, &s_trees.flag[f])) s_trees.nflag = f + 1;
    }
    s_trees_ok = any > 0;
}

static void index_golfer(void)
{
    char nm[NAME_LEN];
    for (int q = 0; q < SEQ_N; q++) {
        s_nframes[q] = 0;
        for (int f = 0; f < MAX_FRAMES; f++) {
            frame_t *fr = &s_frames[q][f];
            memset(fr, 0, sizeof(*fr));
            snprintf(nm, sizeof nm, "%s_%02d", SEQ_NAME[q], f);
            fr->body = (int16_t)find(nm);
            if (fr->body < 0) break;
            snprintf(nm, sizeof nm, "%s_%02ds", SEQ_NAME[q], f);
            fr->shadow = (int16_t)find(nm);
            for (int h = 0; h < HATS; h++) {
                snprintf(nm, sizeof nm, "h%d%.2s%02d", h, SEQ_NAME[q], f);
                fr->hat[h] = (int16_t)find(nm);
            }
            s_nframes[q] = f + 1;
        }
    }
}

bool gf_art_load(const char *path)
{
    gf_art_free();
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t hdr[8];
    if (len < 8 || fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "GFPK", 4) != 0) {
        fclose(f);
        return false;
    }
    s_fp = f;
    s_pak_len = (size_t)len;
    s_nent = rd16(hdr + 6);
    uint8_t *table = (uint8_t *)gf_malloc((size_t)s_nent * ENTRY_SIZE);
    s_ent = (entry_t *)gf_calloc((size_t)s_nent, sizeof(entry_t));
    if (!table || !s_ent || fread(table, ENTRY_SIZE, (size_t)s_nent, f) != (size_t)s_nent) {
        free(table);
        gf_art_free();
        return false;
    }
    /* <24s B B H H h h I I I H H>: 50 bytes, see tools/pack_assets.py */
    for (int i = 0; i < s_nent; i++) {
        const uint8_t *p = table + (size_t)i * ENTRY_SIZE;
        entry_t *e = &s_ent[i];
        memcpy(e->name, p, NAME_LEN);
        e->name[NAME_LEN - 1] = 0;
        e->type = p[24];
        e->flags = p[25];
        e->w = rd16(p + 26);
        e->h = rd16(p + 28);
        e->ax = (int16_t)rd16(p + 30);
        e->ay = (int16_t)rd16(p + 32);
        e->off = rd32(p + 34);
        e->clen = rd32(p + 38);
        e->rawlen = rd32(p + 42);
        e->ppm8 = rd16(p + 46);
        e->extra = rd16(p + 48);
    }
    free(table);
    uint64_t t0 = aos_hal_uptime_ms();
    load_trees();
    index_golfer();
    aos_hal_log("golf", "pak: %d entries, %u KB, trees and index %u ms", s_nent, (unsigned)(s_pak_len / 1024),
                (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0));
    return true;
}

void gf_art_free(void)
{
    for (int q = 0; q < SEQ_N; q++) {
        gf_art_release(q);
    }
    for (int k = 0; k < GF_TREE_KINDS; k++) {
        gf_mip_free(&s_trees.side[k]);
        gf_mip_free(&s_trees.top[k]);
        gf_mip_free(&s_trees.shadow[k]);
    }
    for (int f = 0; f < 4; f++) {
        gf_mip_free(&s_trees.flag[f]);
    }
    memset(&s_trees, 0, sizeof(s_trees));
    s_trees_ok = false;
    free(s_ent);
    s_ent = NULL;
    s_nent = 0;
    if (s_fp) fclose(s_fp);
    s_fp = NULL;
    s_pak_len = 0;
    memset(s_nframes, 0, sizeof(s_nframes));
}

/* --------------------------------------------------------------------------
 * Trees
 * -------------------------------------------------------------------------- */

const gf_tree_art_t *gf_art_trees(void)
{
    return s_trees_ok ? &s_trees : NULL;
}

bool gf_art_tree_top(const gf_tree_art_t *a, int kind)
{
    return a && kind >= 0 && kind < GF_TREE_KINDS && a->ok[kind];
}

void gf_art_tree_draw_top(const gf_tree_art_t *a, gf_img_t *im, int kind, float cx, float cy, float ppm, float size, int tint)
{
    const gf_mip_t *m = &a->top[kind];
    gf_mip_draw(im, m, cx, cy, ppm * size / m->ppm, 255, tint, NULL, 0, 0, 0);
}

void gf_art_tree_shadow(const gf_tree_art_t *a, gf_img_t *im, int kind, float cx, float cy, float ppm, float size)
{
    const gf_mip_t *m = &a->shadow[kind];
    gf_mip_shadow(im, m, cx, cy, ppm * size / m->ppm, 150);
}

const gf_mip_t *gf_art_tree_side(int kind)
{
    return s_trees_ok && s_trees.ok[kind] ? &s_trees.side[kind] : NULL;
}

const gf_mip_t *gf_art_flag(int frame)
{
    return s_trees.nflag ? &s_trees.flag[frame % s_trees.nflag] : NULL;
}

float gf_art_tree_height(int kind)
{
    return s_trees.height[kind];
}

float gf_art_tree_radius(int kind)
{
    return s_trees.diam[kind] * 0.5f;
}

/* --------------------------------------------------------------------------
 * The golfer
 * -------------------------------------------------------------------------- */

bool gf_art_have_golfer(void)
{
    return s_nframes[SEQ_SWING] > 0;
}

int gf_art_frames(int seq)
{
    return seq >= 0 && seq < SEQ_N ? s_nframes[seq] : 0;
}

void gf_art_release(int seq)
{
    for (int f = 0; f < MAX_FRAMES; f++) {
        frame_t *fr = &s_frames[seq][f];
        free(fr->spr.px);
        free(fr->spr.a);
        fr->spr.px = NULL;
        fr->spr.a = NULL;
        free(fr->shd.a);
        fr->shd.a = NULL;
    }
}

unsigned gf_art_ready(void)
{
    return s_ready_mask;
}

void gf_art_outfit(const uint8_t eq[CAT_N])
{
    s_ready_mask = 0;
    gf_outfit_palette(eq, s_pal, &s_hat);
    for (int q = 0; q < SEQ_N; q++) {
        for (int f = 0; f < MAX_FRAMES; f++) {
            frame_t *fr = &s_frames[q][f];
            free(fr->spr.px);
            free(fr->spr.a);
            fr->spr.px = NULL;
            fr->spr.a = NULL;
        }
    }
}

/* one pixel of a golfer layer: region colour lit by the grey pass */
static inline void shade_px(uint8_t b0, uint8_t b1, int *r, int *g, int *b, int *a)
{
    int id = b0 >> 4;
    *a = (b0 & 15) * 17;
    uint32_t c = s_pal[id];
    int cr = (int)(c >> 16) & 255, cg = (int)(c >> 8) & 255, cb = (int)c & 255;
    /* the neutral grey renders around 200 in full light: that is "as is" */
    int k = b1 * 256 / 196;
    int rr = cr * k >> 8, gg = cg * k >> 8, bb = cb * k >> 8;
    /* past white, towards white: highlights keep their hue */
    int over = rr > 255 ? rr - 255 : 0;
    if (gg - 255 > over) over = gg - 255;
    if (bb - 255 > over) over = bb - 255;
    if (over > 0) {
        rr += over / 2; gg += over / 2; bb += over / 2;
    }
    *r = rr > 255 ? 255 : rr;
    *g = gg > 255 ? 255 : gg;
    *b = bb > 255 ? 255 : bb;
}

static bool colour_frame(frame_t *fr)
{
    const entry_t *eb = &s_ent[fr->body];
    int bx0 = eb->ax, by0 = eb->ay, bx1 = bx0 + eb->w, by1 = by0 + eb->h;
    int hi = s_hat >= 0 && s_hat < HATS ? fr->hat[s_hat] : -1;
    int ux0 = bx0, uy0 = by0, ux1 = bx1, uy1 = by1;
    if (hi >= 0) {
        const entry_t *eh = &s_ent[hi];
        if (eh->ax < ux0) ux0 = eh->ax;
        if (eh->ay < uy0) uy0 = eh->ay;
        if (eh->ax + eh->w > ux1) ux1 = eh->ax + eh->w;
        if (eh->ay + eh->h > uy1) uy1 = eh->ay + eh->h;
    }
    int W = ux1 - ux0, H = uy1 - uy0;
    uint16_t *px = (uint16_t *)gf_malloc((size_t)W * H * 2);
    uint8_t *al = (uint8_t *)gf_calloc((size_t)W * H, 1);
    uint8_t *raw = unpack(fr->body);
    if (!px || !al || !raw) {
        free(px);
        free(al);
        free(raw);
        return false;
    }
    for (int y = 0; y < eb->h; y++) {
        for (int x = 0; x < eb->w; x++) {
            const uint8_t *p = raw + 2 * ((size_t)y * eb->w + x);
            int r, g, b, a;
            shade_px(p[0], p[1], &r, &g, &b, &a);
            if (!a) continue;
            size_t o = (size_t)(y + by0 - uy0) * W + (x + bx0 - ux0);
            px[o] = gf_dither(r, g, b, x + bx0, y + by0);
            al[o] = (uint8_t)a;
        }
    }
    free(raw);
    if (hi >= 0) {
        const entry_t *eh = &s_ent[hi];
        raw = unpack(hi);
        if (raw) {
            for (int y = 0; y < eh->h; y++) {
                for (int x = 0; x < eh->w; x++) {
                    const uint8_t *p = raw + 2 * ((size_t)y * eh->w + x);
                    int r, g, b, a;
                    shade_px(p[0], p[1], &r, &g, &b, &a);
                    if (!a) continue;
                    size_t o = (size_t)(y + eh->ay - uy0) * W + (x + eh->ax - ux0);
                    uint16_t c = gf_dither(r, g, b, x + eh->ax, y + eh->ay);
                    int da = al[o];
                    if (da == 0) {
                        px[o] = c;
                        al[o] = (uint8_t)a;
                    } else {
                        px[o] = gf_blend(px[o], c, a);
                        int na = a + da * (255 - a) / 255;
                        al[o] = (uint8_t)(na > 255 ? 255 : na);
                    }
                }
            }
            free(raw);
        }
    }
    fr->spr.px = px;
    fr->spr.a = al;
    fr->spr.w = (int16_t)W;
    fr->spr.h = (int16_t)H;
    fr->spr.ox = 0;
    fr->spr.oy = 0;
    fr->x0 = (int16_t)ux0;
    fr->y0 = (int16_t)uy0;
    return true;
}

static bool decode_shadow(frame_t *fr)
{
    {
        const entry_t *e = &s_ent[fr->shadow];
        uint8_t *raw = unpack(fr->shadow);
        if (!raw) return false;
        fr->shd.px = NULL;
        if (e->flags & 1) {
            /* stored at half resolution: back up, bilinearly */
            int W = e->w * 2, H = e->h * 2;
            uint8_t *full = (uint8_t *)gf_malloc((size_t)W * H);
            if (!full) {
                free(raw);
                return false;
            }
            for (int y = 0; y < H; y++) {
                float sy = (y - 0.5f) * 0.5f;
                int y0 = sy < 0 ? 0 : (int)sy, y1 = y0 + 1 < e->h ? y0 + 1 : e->h - 1;
                float ty = sy < 0 ? 0 : sy - (float)y0;
                for (int x = 0; x < W; x++) {
                    float sx = (x - 0.5f) * 0.5f;
                    int x0 = sx < 0 ? 0 : (int)sx, x1 = x0 + 1 < e->w ? x0 + 1 : e->w - 1;
                    float tx = sx < 0 ? 0 : sx - (float)x0;
                    float a = raw[y0 * e->w + x0] + (raw[y0 * e->w + x1] - raw[y0 * e->w + x0]) * tx;
                    float b = raw[y1 * e->w + x0] + (raw[y1 * e->w + x1] - raw[y1 * e->w + x0]) * tx;
                    full[y * W + x] = (uint8_t)(a + (b - a) * ty);
                }
            }
            free(raw);
            fr->shd.a = full;
            fr->shd.w = (int16_t)W;
            fr->shd.h = (int16_t)H;
            fr->sx0 = (int16_t)(e->ax * 2);
            fr->sy0 = (int16_t)(e->ay * 2);
        } else {
            fr->shd.a = raw;
            fr->shd.w = (int16_t)e->w;
            fr->shd.h = (int16_t)e->h;
            fr->sx0 = e->ax;
            fr->sy0 = e->ay;
        }
    }
    return true;
}

/* Colours every frame of the sequences in 'mask' (1 << SEQ_*) that is not
 * coloured yet, and decodes their shadows. From the worker: it reads the
 * card. */
size_t gf_art_prepare(unsigned mask)
{
    /* the wait first: it is what the menu shows, and four frames are
     * ready long before the twenty-four of the swing */
    static const uint8_t ORDER[SEQ_N] = { SEQ_IDLE, SEQ_SWING, SEQ_CHEER, SEQ_SAD, SEQ_TURN };
    size_t bytes = 0;
    for (int oi = 0; oi < SEQ_N; oi++) {
        int q = ORDER[oi];
        if (!(mask & (1u << q))) continue;
        for (int f = 0; f < s_nframes[q]; f++) {
            frame_t *fr = &s_frames[q][f];
            if (!fr->spr.px) colour_frame(fr);
            if (!fr->shd.a && fr->shadow >= 0) decode_shadow(fr);
            bytes += (size_t)fr->spr.w * fr->spr.h * 3 + (size_t)fr->shd.w * fr->shd.h;
        }
        s_ready_mask |= 1u << q;
        gf_yield();
    }
    return bytes;
}

const gf_sprite_t *gf_art_golfer(int seq, int frame, int *x0, int *y0)
{
    if (seq < 0 || seq >= SEQ_N || frame < 0 || frame >= s_nframes[seq]) return NULL;
    frame_t *fr = &s_frames[seq][frame];
    if (!fr->spr.px) return NULL;       /* not coloured yet: gf_art_prepare() */
    *x0 = fr->x0;
    *y0 = fr->y0;
    return &fr->spr;
}

const gf_sprite_t *gf_art_golfer_shadow(int seq, int frame, int *x0, int *y0)
{
    if (seq < 0 || seq >= SEQ_N || frame < 0 || frame >= s_nframes[seq]) return NULL;
    frame_t *fr = &s_frames[seq][frame];
    if (fr->shadow < 0) return NULL;
    if (!fr->shd.a) return NULL;
    *x0 = fr->sx0;
    *y0 = fr->sy0;
    return &fr->shd;
}
