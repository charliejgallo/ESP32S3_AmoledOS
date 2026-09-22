/*
 * TURBO - the art rendered in Blender (see tb_art.h)
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "tb_art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRY_SIZE  50
#define NAME_LEN    24

enum { T_SPRITE = 1, T_PLANE = 2, T_VEHICLE = 4 };

typedef struct {
    char     name[NAME_LEN];
    uint8_t  type, flags;
    uint16_t w, h;
    int16_t  ax, ay;
    uint32_t off, clen, rawlen;
    uint16_t ppm8, extra;
} entry_t;

/* the pack stays on the card: only its table is in memory */
static FILE      *s_fp;
static size_t     s_len;
static entry_t   *s_ent;
static int        s_nent;

static tb_vspr_t  s_near[CAR_N][TB_NEAR_FRAMES];
static tb_sprite_t s_near_sh[CAR_N];           /* one per car, the straight frame's */
static int        s_near_car = -1;              /* whose frames are loaded           */
static tb_vmip_t  s_far[VH_N][TB_FAR_VIEWS];
static tb_mip_t   s_far_sh[VH_N][TB_FAR_VIEWS];
static bool       s_vehicles;
static tb_mip_t   s_prop[PR_N], s_prop_sh[PR_N];
static tb_sprite_t s_bg;
static int        s_stage = -1;

static const char *veh_name(int v)
{
    switch (v) {
    case CAR_WEDGE: return "wedge";
    case CAR_MUSCLE: return "muscle";
    case CAR_RALLY: return "rally";
    case CAR_PICKUP: return "pickup";
    case VH_SEDAN: return "sedan";
    case VH_COMPACT: return "compact";
    case VH_VAN: return "van";
    case VH_HEARSE: return "hearse";
    default: return "truck";
    }
}

static const char *prop_name(int k)
{
    static const char *const n[PR_N] = {
        "checkpoint", "finish", "cone", "sign_curve_l", "barrier",
        "overpass", "lamp", "tower_brick", "tower_glass", "tree_round", "billboard", "sign_gantry",
        "palm", "palm_tall", "rock_cliff", "lighthouse", "beach_hut", "guardrail",
        "saguaro", "saguaro_small", "butte", "rock_red", "dead_tree", "diner_sign",
        "pine_snow", "pine", "rock_snow", "snowbank", "cabin", "lamp_night",
        "asteroid_a", "asteroid_b", "crystal", "ring_gate", "satellite", "beacon",
        "dead_tree_twisted", "pumpkins", "tombstones", "cemetery_fence", "scarecrow", "haunted_house", "gas_lamp",
        "tunnel_portal", "rock_granite", "waterfall_cliff", "pylon", "pine_day",
    };
    return k >= 0 && k < PR_N ? n[k] : "?";
}

static const char *stage_key(int stage)
{
    switch (stage) {
    case STAGE_CITY: return "city";
    case STAGE_COAST: return "coast";
    case STAGE_DESERT: return "desert";
    case STAGE_MOUNTAIN: return "mountain";
    case STAGE_HALLOWEEN: return "halloween";
    case STAGE_TUNNELS: return "tunnels";
    default: return "space";
    }
}

/* --------------------------------------------------------------------------
 * Paints
 * -------------------------------------------------------------------------- */

static void paint_base(tb_paint_t *p)
{
    p->c[RG_EMPTY] = 0;
    p->c[RG_GLASS] = 0x2A3440;
    p->c[RG_CHROME] = 0xD4D8DC;
    p->c[RG_TRIM] = 0x1E2022;
    p->c[RG_TYRE] = 0x1C1C1C;
    p->c[RG_RIM] = 0xB8BCC2;
    p->c[RG_TAIL] = 0xB01810;
    p->c[RG_HEAD] = 0xF0F0E4;
    p->c[RG_PLATE] = 0xE6E6DC;
    p->c[RG_INTERIOR] = 0x2C2622;
    p->c[RG_UNDER] = 0x141414;
    p->c[RG_AMBER] = 0xE88A12;
    p->c[RG_EXTRA] = 0x2A2A2C;
    p->c[RG_SPARE] = 0x808080;
}

typedef struct {
    uint32_t a, b, rim;
    int      price;
} paint_def_t;

static const paint_def_t *paints(int *n)
{
    static const paint_def_t p[] = {
        { 0xD01818, 0x141414, 0xB8BCC2,    0 },     /* red, black strakes     */
        { 0x14285A, 0xF2F2F2, 0xB8BCC2,  150 },     /* navy, white stripes    */
        { 0xF2F2F0, 0xD01818, 0xD8C890,  150 },     /* white, red band        */
        { 0xF2C012, 0x141414, 0x303030,  200 },     /* yellow, black          */
        { 0x1E8A3C, 0xF0E6C8, 0xC8B070,  250 },     /* racing green, cream    */
        { 0xB8BEC6, 0x1E50C8, 0xE0E4E8,  250 },     /* silver, blue           */
        { 0xFF6A12, 0x5A1E8C, 0x202020,  300 },     /* orange, purple         */
        { 0x121214, 0xD8A830, 0xD8A830,  400 },     /* black and gold         */
        { 0xFF4FA8, 0x2AD8E8, 0xF0F0F0,  400 },     /* pink, cyan             */
        { 0x6A2AD8, 0x9CFF2A, 0x202020,  500 },     /* violet, lime           */
        { 0x0A0A0A, 0xF4F4F4, 0x909090,  500 },     /* black and white        */
        { 0x2A9ED8, 0xF2C012, 0xF2C012,  600 },     /* sky blue, yellow       */
    };
    *n = (int)(sizeof(p) / sizeof(p[0]));
    return p;
}

int tb_paint_n(void)
{
    int n;
    paints(&n);
    return n;
}

int tb_paint_price(int i)
{
    int n;
    const paint_def_t *p = paints(&n);
    return i >= 0 && i < n ? p[i].price : 0;
}

void tb_paint_get(int i, tb_paint_t *out)
{
    int n;
    const paint_def_t *p = paints(&n);
    if (i < 0 || i >= n) i = 0;
    paint_base(out);
    out->c[RG_PAINT_A] = p[i].a;
    out->c[RG_PAINT_B] = p[i].b;
    out->c[RG_RIM] = p[i].rim;
}

void tb_paint_traffic(int i, tb_paint_t *out)
{
    static const uint32_t c[16] = {
        0xE8E8E4, 0x202224, 0x9CA2A8, 0x7A1E1E, 0x1E3A6A, 0x3A5A3A, 0xC8B89A, 0x5A5E64,
        0xB02A20, 0x2A6AB0, 0xE0C040, 0x6A4A32, 0xD0D4D8, 0x3A2A4A, 0x8A9A5A, 0xE87A2A,
    };
    paint_base(out);
    if (i == TB_PAINT_HEARSE) {
        /* always black with a dark red pinstripe: in a colour it stops
         * reading as a hearse; wine curtains behind the rear window */
        out->c[RG_PAINT_A] = 0x18181C;
        out->c[RG_PAINT_B] = 0x6A1A1A;
        out->c[RG_INTERIOR] = 0x6A2434;
        return;
    }
    out->c[RG_PAINT_A] = c[i & 15];
    out->c[RG_PAINT_B] = c[(i * 7 + 3) & 15];
    if ((i & 3) == 0) out->c[RG_PAINT_B] = c[i & 15];
}

void tb_lut_build(tb_lut_t *l, const tb_paint_t *p, uint32_t fog, int fog_k, bool brake, bool lights)
{
    /* the fog as a fixed part added after the light: c * m * (1 - f) + fog * f */
    int kf = fog_k > 0 ? fog_k : 0;
    int fr = ((int)(fog >> 16) & 255) * kf, fg = ((int)(fog >> 8) & 255) * kf, fb = ((int)fog & 255) * kf;
    int keep = 256 - kf;
    for (int id = 0; id < RG_N; id++) {
        uint32_t c = p->c[id];
        bool glow = false;
        if (id == RG_TAIL) {
            if (brake) { c = 0xFF3020; glow = true; }
            else if (lights) { c = 0xD02010; glow = true; }
        }
        if (id == RG_HEAD && lights) glow = true;
        int cr = (int)(c >> 16) & 255, cg = (int)(c >> 8) & 255, cb = (int)c & 255;
        /* night: everything dimmer except what gives light */
        int night = lights && !glow ? 150 : 256;
        uint16_t *out = l->c[id];
        for (int k = 0; k < 32; k++) {
            int light = glow ? 230 + k : k * 8 + 4;
            int m = light * night / 196;                /* 256 = as in full sun */
            int r = cr * m >> 8, g = cg * m >> 8, b = cb * m >> 8;
            /* speculars go past white towards white, not towards a hue */
            int over = (r > 255 ? r - 255 : 0) + (g > 255 ? g - 255 : 0) + (b > 255 ? b - 255 : 0);
            if (over) {
                r += over / 3; g += over / 3; b += over / 3;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
            }
            if (kf) {
                r = (r * keep + fr) >> 8;
                g = (g * keep + fg) >> 8;
                b = (b * keep + fb) >> 8;
            }
            out[k] = tb_rgb(r, g, b);
        }
    }
}

/* --------------------------------------------------------------------------
 * Drawing vehicles
 * -------------------------------------------------------------------------- */

void tb_vspr_draw(tb_img_t *im, const tb_vspr_t *s, const tb_lut_t *lut, int x, int y, int opa)
{
    int sx0 = x - s->ox, sy0 = y - s->oy;
    int x0 = sx0 < im->cx0 ? im->cx0 : sx0, y0 = sy0 < im->cy0 ? im->cy0 : sy0;
    int x1 = sx0 + s->w > im->cx1 ? im->cx1 : sx0 + s->w;
    int y1 = sy0 + s->h > im->cy1 ? im->cy1 : sy0 + s->h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *d = im->px + (size_t)yy * im->w;
        const uint16_t *sp = s->px + (size_t)(yy - sy0) * s->w - sx0;
        for (int xx = x0; xx < x1; xx++) {
            uint16_t v = sp[xx];
            int a4 = (v >> 8) & 15;
            if (!a4) continue;
            uint16_t c = lut->c[v >> 12][(v & 255) >> 3];
            int a = a4 * 17 * opa >> 8;
            d[xx] = a >= 250 ? c : tb_blend(d[xx], c, a);
        }
    }
}

void tb_vmip_draw(tb_img_t *im, const tb_vmip_t *m, const tb_lut_t *lut, float x, float y,
                  float scale, int clip_y, int opa)
{
    if (!m->n || scale <= 0.002f) return;
    int l = 0;
    float ls = scale;
    while (l + 1 < m->n && ls < 0.5f) {
        ls *= 2.0f;
        l++;
    }
    const tb_vspr_t *s = &m->lv[l];
    float x0f = x - (float)s->ox * ls, y0f = y - (float)s->oy * ls;
    int x0 = tb_ifloor(x0f), y0 = tb_ifloor(y0f);
    int x1 = tb_ifloor(x0f + s->w * ls) + 1, y1 = tb_ifloor(y0f + s->h * ls) + 1;
    int cy1 = clip_y < im->cy1 ? clip_y : im->cy1;
    if (x0 < im->cx0) x0 = im->cx0;
    if (y0 < im->cy0) y0 = im->cy0;
    if (x1 > im->cx1) x1 = im->cx1;
    if (y1 > cy1) y1 = cy1;
    if (x0 >= x1 || y0 >= y1) return;
    int32_t step = (int32_t)(65536.0f / ls);
    int32_t sxs = (int32_t)(((float)x0 + 0.5f - x0f) * 65536.0f / ls);
    int32_t sy = (int32_t)(((float)y0 + 0.5f - y0f) * 65536.0f / ls);
    for (int yy = y0; yy < y1; yy++, sy += step) {
        int iy = sy >> 16;
        if (iy < 0 || iy >= s->h) continue;
        uint16_t *d = im->px + (size_t)yy * im->w;
        const uint16_t *sp = s->px + (size_t)iy * s->w;
        int32_t sx = sxs;
        for (int xx = x0; xx < x1; xx++, sx += step) {
            int ix = sx >> 16;
            if (ix < 0 || ix >= s->w) continue;
            uint16_t v = sp[ix];
            int a4 = (v >> 8) & 15;
            if (a4 < 2) continue;
            uint16_t c = lut->c[v >> 12][(v & 255) >> 3];
            int a = a4 * 17 * opa >> 8;
            d[xx] = a >= 250 ? c : tb_blend(d[xx], c, a);
        }
    }
}

static bool vmip_build(tb_vmip_t *m, const tb_vspr_t *src)
{
    m->lv[0] = *src;
    m->n = 1;
    while (m->n < TB_MIPS) {
        const tb_vspr_t *a = &m->lv[m->n - 1];
        if (a->w < 4 || a->h < 4) break;
        tb_vspr_t *b = &m->lv[m->n];
        b->w = (int16_t)(a->w / 2);
        b->h = (int16_t)(a->h / 2);
        b->ox = (int16_t)(a->ox / 2);
        b->oy = (int16_t)(a->oy / 2);
        b->px = (uint16_t *)tb_malloc((size_t)b->w * b->h * 2);
        if (!b->px) break;
        for (int y = 0; y < b->h; y++) {
            for (int x = 0; x < b->w; x++) {
                int asum = 0, lsum = 0, lw = 0, best = -1, bid = 0;
                for (int k = 0; k < 4; k++) {
                    uint16_t v = a->px[(size_t)(y * 2 + (k >> 1)) * a->w + x * 2 + (k & 1)];
                    int al = (v >> 8) & 15;
                    asum += al;
                    if (al) {
                        lsum += (v & 255) * al;
                        lw += al;
                    }
                    if (al > best) {
                        best = al;
                        bid = v >> 12;
                    }
                }
                int al = (asum + 2) / 4;
                int li = lw ? lsum / lw : 0;
                b->px[(size_t)y * b->w + x] = (uint16_t)((bid << 12) | (al << 8) | li);
            }
        }
        m->n++;
        tb_yield();
    }
    return true;
}

static void vmip_free(tb_vmip_t *m)
{
    for (int i = 0; i < m->n; i++) {
        free(m->lv[i].px);
        m->lv[i].px = NULL;
    }
    m->n = 0;
}

/* --------------------------------------------------------------------------
 * The pack
 * -------------------------------------------------------------------------- */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

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
        for (size_t k = 0; k < m; k++, o++) dst[o] = dst[o - off];
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
    if (!s_fp || e->off + e->clen > s_len) return NULL;
    uint8_t *raw = (uint8_t *)tb_malloc(e->rawlen ? e->rawlen : 1);
    uint8_t *src = (uint8_t *)tb_malloc(e->clen ? e->clen : 1);
    bool ok = raw && src && fseek(s_fp, (long)e->off, SEEK_SET) == 0 &&
              fread(src, 1, e->clen, s_fp) == e->clen && lz4_decode(src, e->clen, raw, e->rawlen);
    free(src);
    tb_yield();
    if (!ok) {
        free(raw);
        return NULL;
    }
    return raw;
}

bool tb_art_open(const char *path)
{
    tb_art_close();
    s_fp = fopen(path, "rb");
    if (!s_fp) return false;
    fseek(s_fp, 0, SEEK_END);
    s_len = (size_t)ftell(s_fp);
    fseek(s_fp, 0, SEEK_SET);
    uint8_t hdr[8];
    if (fread(hdr, 1, 8, s_fp) != 8 || memcmp(hdr, "TBPK", 4) != 0) {
        tb_art_close();
        return false;
    }
    int n = rd16(hdr + 6);
    uint8_t *tab = (uint8_t *)tb_malloc((size_t)n * ENTRY_SIZE);
    s_ent = (entry_t *)tb_calloc((size_t)n, sizeof(entry_t));
    if (!tab || !s_ent || fread(tab, ENTRY_SIZE, (size_t)n, s_fp) != (size_t)n) {
        free(tab);
        tb_art_close();
        return false;
    }
    for (int i = 0; i < n; i++) {
        const uint8_t *p = tab + (size_t)i * ENTRY_SIZE;
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
    free(tab);
    s_nent = n;
    return true;
}

bool tb_art_ok(void)
{
    return s_fp != NULL;
}

/* colour + alpha (RGB565 little-endian, then alpha), into panel order */
static bool load_sprite(const char *name, tb_sprite_t *out, float *ppm)
{
    memset(out, 0, sizeof(*out));
    int i = find(name);
    if (i < 0 || s_ent[i].type != T_SPRITE) return false;
    uint8_t *raw = unpack(i);
    if (!raw) return false;
    const entry_t *e = &s_ent[i];
    size_t np = (size_t)e->w * e->h;
    uint16_t *px = (uint16_t *)tb_malloc(np * 2);
    uint8_t *a = (uint8_t *)tb_malloc(np);
    if (!px || !a) {
        free(px);
        free(a);
        free(raw);
        return false;
    }
    for (size_t k = 0; k < np; k++) px[k] = tb_swap(rd16(raw + k * 2));
    memcpy(a, raw + np * 2, np);
    free(raw);
    out->px = px;
    out->a = a;
    out->w = (int16_t)e->w;
    out->h = (int16_t)e->h;
    out->ox = e->ax;
    out->oy = e->ay;
    if (ppm) *ppm = (float)e->ppm8 / 8.0f;
    return true;
}

static bool load_plane(const char *name, tb_sprite_t *out)
{
    memset(out, 0, sizeof(*out));
    int i = find(name);
    if (i < 0 || s_ent[i].type != T_PLANE) return false;
    uint8_t *raw = unpack(i);
    if (!raw) return false;
    const entry_t *e = &s_ent[i];
    out->px = NULL;
    out->a = raw;
    out->w = (int16_t)e->w;
    out->h = (int16_t)e->h;
    out->ox = e->ax;
    out->oy = e->ay;
    return true;
}

static bool load_vehicle(const char *name, tb_vspr_t *out, float *ppm)
{
    int i = find(name);
    if (i < 0 || s_ent[i].type != T_VEHICLE) return false;
    uint8_t *raw = unpack(i);
    if (!raw) return false;
    const entry_t *e = &s_ent[i];
    size_t np = (size_t)e->w * e->h;
    uint16_t *px = (uint16_t *)tb_malloc(np * 2);
    if (!px) {
        free(raw);
        return false;
    }
    /* stored as (id << 4 | alpha >> 4), light */
    for (size_t k = 0; k < np; k++) {
        uint8_t ia = raw[k * 2], li = raw[k * 2 + 1];
        px[k] = (uint16_t)(((ia >> 4) << 12) | ((ia & 15) << 8) | li);
    }
    free(raw);
    out->px = px;
    out->w = (int16_t)e->w;
    out->h = (int16_t)e->h;
    out->ox = e->ax;
    out->oy = e->ay;
    if (ppm) *ppm = (float)e->ppm8 / 8.0f;
    return true;
}

static void free_far(int v)
{
    for (int k = 0; k < TB_FAR_VIEWS; k++) {
        vmip_free(&s_far[v][k]);
        tb_mip_free(&s_far_sh[v][k]);
    }
}

/* The vehicles a race needs, as a mask of models (1 << VH_* / CAR_*): the
 * ones not in it are freed, the missing ones read from the pack. Each is
 * 120-220 KB with its mip levels and shadows (the truck 500 KB), so loading
 * all of them for the whole session made every new vehicle a cost for
 * every stage. The cars' own shadows (small) are read once. */
bool tb_art_load_vehicles(uint32_t mask)
{
    char nm[NAME_LEN];
    if (!s_vehicles) {
        for (int c = 0; c < CAR_N; c++) {
            snprintf(nm, sizeof nm, "nsh_%s", veh_name(c));
            load_plane(nm, &s_near_sh[c]);
        }
        s_vehicles = true;
    }
    static const char views[3] = { 'l', 'c', 'r' };
    for (int v = 0; v < VH_N; v++) {
        bool want = (mask >> v) & 1u;
        bool have = s_far[v][1].n != 0;
        if (!want && have) free_far(v);
        if (!want || have) continue;
        for (int k = 0; k < TB_FAR_VIEWS; k++) {
            tb_vspr_t sp;
            float ppm = 70.0f;
            snprintf(nm, sizeof nm, "far_%s_%c", veh_name(v), views[k]);
            if (load_vehicle(nm, &sp, &ppm)) {
                vmip_build(&s_far[v][k], &sp);
                s_far[v][k].ppm = ppm;
            }
            tb_sprite_t sh;
            float sppm = ppm * 0.5f;
            int i = -1;
            snprintf(nm, sizeof nm, "fsh_%s_%c", veh_name(v), views[k]);
            if ((i = find(nm)) >= 0) sppm = (float)s_ent[i].ppm8 / 8.0f;
            if (load_plane(nm, &sh)) {
                tb_mip_build(&s_far_sh[v][k], &sh);
                s_far_sh[v][k].ppm = sppm;
            }
        }
    }
    return true;
}

uint32_t tb_art_vehicles_loaded(void)
{
    uint32_t m = 0;
    for (int v = 0; v < VH_N; v++) {
        if (s_far[v][1].n) m |= 1u << v;
    }
    return m;
}

static void free_near(void)
{
    for (int f = 0; f < TB_NEAR_FRAMES; f++) {
        for (int c = 0; c < CAR_N; c++) {
            free(s_near[c][f].px);
            s_near[c][f].px = NULL;
        }
    }
    s_near_car = -1;
}

/* the player's car seen from behind, seven yaw frames (~350 KB): only the
 * car being driven or looked at in the garage */
bool tb_art_load_near(int car)
{
    if (car < 0 || car >= CAR_N) return false;
    if (s_near_car == car) return true;
    free_near();
    if (!s_fp) return false;
    char nm[NAME_LEN];
    int ok = 0;
    for (int f = 0; f < TB_NEAR_FRAMES; f++) {
        snprintf(nm, sizeof nm, "near_%s_y%d", veh_name(car), f);
        if (load_vehicle(nm, &s_near[car][f], NULL)) ok++;
    }
    s_near_car = car;
    return ok == TB_NEAR_FRAMES;
}

void tb_art_drop_near(void)
{
    free_near();
}

void tb_art_drop_backdrop(void)
{
    free(s_bg.px);
    free(s_bg.a);
    memset(&s_bg, 0, sizeof s_bg);
}

static void free_stage(void)
{
    for (int k = 0; k < PR_N; k++) {
        tb_mip_free(&s_prop[k]);
        tb_mip_free(&s_prop_sh[k]);
    }
    free(s_bg.px);
    free(s_bg.a);
    memset(&s_bg, 0, sizeof s_bg);
    s_stage = -1;
}

bool tb_art_load_stage(const tb_track_t *t)
{
    if (s_stage == t->stage) return true;
    free_stage();
    if (!s_fp) return false;
    bool used[PR_N];
    memset(used, 0, sizeof used);
    for (int i = 0; i < t->nprop; i++) used[t->prop[i].kind] = true;
    char nm[NAME_LEN];
    for (int k = 0; k < PR_N; k++) {
        if (!used[k]) continue;
        tb_sprite_t sp;
        float ppm = 32.0f;
        snprintf(nm, sizeof nm, "p_%s", prop_name(k));
        if (load_sprite(nm, &sp, &ppm)) {
            tb_mip_build(&s_prop[k], &sp);
            s_prop[k].ppm = ppm;
        }
        snprintf(nm, sizeof nm, "psh_%s", prop_name(k));
        if (load_plane(nm, &sp)) {
            tb_mip_build(&s_prop_sh[k], &sp);
            s_prop_sh[k].ppm = ppm;
        }
    }
    snprintf(nm, sizeof nm, "bg_%s", stage_key(t->stage));
    load_sprite(nm, &s_bg, NULL);
    s_stage = t->stage;
    return true;
}

void tb_art_close(void)
{
    free_stage();
    free_near();
    for (int c = 0; c < CAR_N; c++) free(s_near_sh[c].a);
    memset(s_near_sh, 0, sizeof s_near_sh);
    for (int v = 0; v < VH_N; v++) free_far(v);
    s_vehicles = false;
    if (s_fp) fclose(s_fp);
    s_fp = NULL;
    free(s_ent);
    s_ent = NULL;
    s_nent = 0;
}

const tb_vspr_t *tb_art_near(int car, int frame)
{
    if (car < 0 || car >= CAR_N || frame < 0 || frame >= TB_NEAR_FRAMES) return NULL;
    return s_near[car][frame].px ? &s_near[car][frame] : NULL;
}

const tb_sprite_t *tb_art_near_shadow(int car, int frame)
{
    (void)frame;
    if (car < 0 || car >= CAR_N) return NULL;
    return s_near_sh[car].a ? &s_near_sh[car] : NULL;
}

const tb_vmip_t *tb_art_far(int model, int view)
{
    if (model < 0 || model >= VH_N || view < 0 || view >= TB_FAR_VIEWS) return NULL;
    return s_far[model][view].n ? &s_far[model][view] : NULL;
}

const tb_mip_t *tb_art_far_shadow(int model, int view)
{
    if (model < 0 || model >= VH_N || view < 0 || view >= TB_FAR_VIEWS) return NULL;
    return s_far_sh[model][view].n ? &s_far_sh[model][view] : NULL;
}

const tb_mip_t *tb_art_prop(int kind)
{
    return kind >= 0 && kind < PR_N && s_prop[kind].n ? &s_prop[kind] : NULL;
}

const tb_mip_t *tb_art_prop_shadow(int kind)
{
    return kind >= 0 && kind < PR_N && s_prop_sh[kind].n ? &s_prop_sh[kind] : NULL;
}

const tb_sprite_t *tb_art_backdrop(void)
{
    return s_bg.px ? &s_bg : NULL;
}
