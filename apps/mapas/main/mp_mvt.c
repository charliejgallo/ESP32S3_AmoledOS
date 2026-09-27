/*
 * MAPAS - Mapbox Vector Tile decoder, down to what the watch draws.
 *
 * The tile comes from the network or the card, so every length is checked
 * against the end of the buffer: a cut download must give a smaller tile,
 * never a read past the end.
 *
 * Protobuf in three messages (the spec is two pages):
 *   Tile    { repeated Layer layers = 3; }
 *   Layer   { name = 1; repeated Feature features = 2; repeated string keys = 3;
 *             repeated Value values = 4; extent = 5; }
 *   Feature { id = 1; packed uint32 tags = 2 (key, value, ...); type = 3;
 *             packed uint32 geometry = 4; }
 * and the geometry is a list of commands (MoveTo, LineTo, ClosePath) with
 * zigzag deltas.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#include "mp_mem.h"
#include "mp_tile.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Protobuf
 * ------------------------------------------------------------------------- */

typedef struct {
    const uint8_t *p, *end;
} pb_t;

static bool pb_varint(pb_t *b, uint32_t *out)
{
    uint32_t r = 0;
    for (int s = 0; s < 35; s += 7) {
        if (b->p >= b->end) return false;
        uint8_t c = *b->p++;
        if (s < 32) r |= (uint32_t)(c & 0x7F) << s;
        if (c < 0x80) {
            *out = r;
            return true;
        }
    }
    /* a 64-bit varint: keep the low part, skip the rest */
    while (b->p < b->end && (*b->p & 0x80)) b->p++;
    if (b->p >= b->end) return false;
    b->p++;
    *out = r;
    return true;
}

/* The next field: its number and wire type. For length-delimited ones, sub
 * is the payload; for varints, *v. Unknown types end the message. */
static bool pb_next(pb_t *b, int *field, int *type, uint32_t *v, pb_t *sub)
{
    uint32_t key;
    if (b->p >= b->end || !pb_varint(b, &key)) return false;
    *field = (int)(key >> 3);
    *type = (int)(key & 7);
    switch (*type) {
    case 0:
        return pb_varint(b, v);
    case 1:
        if (b->end - b->p < 8) return false;
        *v = (uint32_t)b->p[0] | (uint32_t)b->p[1] << 8 | (uint32_t)b->p[2] << 16 | (uint32_t)b->p[3] << 24;
        b->p += 8;
        return true;
    case 2: {
        uint32_t n;
        if (!pb_varint(b, &n) || (uint32_t)(b->end - b->p) < n) return false;
        sub->p = b->p;
        sub->end = b->p + n;
        b->p += n;
        return true;
    }
    case 5:
        if (b->end - b->p < 4) return false;
        *v = (uint32_t)b->p[0] | (uint32_t)b->p[1] << 8 | (uint32_t)b->p[2] << 16 | (uint32_t)b->p[3] << 24;
        b->p += 4;
        return true;
    default:
        return false;
    }
}

static bool pb_eq(const pb_t *s, const char *lit)
{
    size_t n = strlen(lit);
    return (size_t)(s->end - s->p) == n && memcmp(s->p, lit, n) == 0;
}

/* ---------------------------------------------------------------------------
 * The layers the watch knows
 * ------------------------------------------------------------------------- */

enum {
    L_NONE = 0, L_WATER, L_WATERWAY, L_LANDCOVER, L_LANDUSE, L_PARK, L_BUILDING,
    L_AEROWAY, L_TRANSPORTATION, L_TRANSPORTATION_NAME, L_BOUNDARY, L_PLACE,
    L_WATER_NAME,
};

static int layer_id(const pb_t *name)
{
    static const struct { const char *n; int id; } T[] = {
        { "water", L_WATER }, { "waterway", L_WATERWAY }, { "landcover", L_LANDCOVER },
        { "landuse", L_LANDUSE }, { "park", L_PARK }, { "building", L_BUILDING },
        { "aeroway", L_AEROWAY }, { "transportation", L_TRANSPORTATION },
        { "transportation_name", L_TRANSPORTATION_NAME }, { "boundary", L_BOUNDARY },
        { "place", L_PLACE }, { "water_name", L_WATER_NAME },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (pb_eq(name, T[i].n)) return T[i].id;
    return L_NONE;
}

/* The same list, from a layer's raw bytes: for mp_mvt_strip(). */
static bool layer_kept(pb_t layer)
{
    int f, t;
    uint32_t v;
    pb_t s;
    while (pb_next(&layer, &f, &t, &v, &s)) {
        if (f == 1 && t == 2) return layer_id(&s) != L_NONE;
    }
    return false;
}

bool mp_mvt_complete(const uint8_t *buf, int len)
{
    pb_t b = { buf, buf + len };
    int f, t;
    uint32_t v;
    pb_t s;
    while (b.p < b.end) {
        if (!pb_next(&b, &f, &t, &v, &s)) return false;
    }
    return true;
}

int mp_mvt_strip(const uint8_t *buf, int len, uint8_t *out)
{
    pb_t b = { buf, buf + len };
    int n = 0;
    for (;;) {
        const uint8_t *start = b.p;
        int f, t;
        uint32_t v;
        pb_t s;
        if (!pb_next(&b, &f, &t, &v, &s)) break;
        if (f == 3 && t == 2 && layer_kept(s)) {
            int sz = (int)(b.p - start);
            memmove(out + n, start, (size_t)sz);
            n += sz;
        }
    }
    return n;
}

/* ---------------------------------------------------------------------------
 * Growing arrays (PSRAM: anything over 1 KB goes there by itself)
 * ------------------------------------------------------------------------- */

typedef struct {
    mp_feat_t *f;   int nf, cf;
    uint32_t  *rn;  int nr, cr;
    mp_pt_t   *p;   int np, cp;
    char      *s;   int ns, cs;
    bool       oom;
} acc_t;

static bool grow(void **ptr, int *cap, int need, size_t elem, int first)
{
    if (need <= *cap) return true;
    int nc = *cap ? *cap : first;
    while (nc < need) nc *= 2;
    void *np = mp_realloc(*ptr, (size_t)nc * elem);
    if (!np) return false;
    *ptr = np;
    *cap = nc;
    return true;
}

/* ---------------------------------------------------------------------------
 * A layer: keys and values, then the features
 * ------------------------------------------------------------------------- */

#define MAX_KV 4096

typedef struct {
    int k_class, k_subclass, k_name, k_name_latin, k_admin, k_brunnel, k_maritime, k_rank;
    pb_t vals[MAX_KV];      /* each Value message */
    int  nvals;
} layer_kv_t;

/* A Value as a string (field 1), or empty. */
static pb_t val_str(const layer_kv_t *kv, uint32_t i)
{
    pb_t none = { 0, 0 };
    if (i >= (uint32_t)kv->nvals) return none;
    pb_t b = kv->vals[i];
    int f, t;
    uint32_t v;
    pb_t s;
    while (pb_next(&b, &f, &t, &v, &s))
        if (f == 1 && t == 2) return s;
    return none;
}

/* A Value as an integer (int, uint, sint, or a float/double's whole part is
 * not needed: OpenMapTiles sends admin_level and rank as ints). */
static int val_int(const layer_kv_t *kv, uint32_t i, int dflt)
{
    if (i >= (uint32_t)kv->nvals) return dflt;
    pb_t b = kv->vals[i];
    int f, t;
    uint32_t v;
    pb_t s;
    while (pb_next(&b, &f, &t, &v, &s)) {
        if (t != 0) continue;
        if (f == 4 || f == 5) return (int)v;
        if (f == 6) return (int)((v >> 1) ^ (uint32_t)-(int32_t)(v & 1));
        if (f == 7) return (int)v;
    }
    return dflt;
}

typedef struct {
    int mc;         /* drawing class, -1 = skip */
    int sub;
    int rank;
    int flags;
} cls_t;

static int road_rank(const pb_t *c)
{
    static const char *const R[] = { "motorway", "trunk", "primary", "secondary",
                                     "tertiary", "minor", "service", "track", "path" };
    for (int i = 0; i < 9; i++)
        if (pb_eq(c, R[i])) return i;
    return -1;
}

static cls_t classify(int layer, int gtype, const pb_t *cl, const pb_t *sub,
                      int admin, bool maritime, bool tunnel, int rank)
{
    cls_t r = { -1, 0, 0, 0 };
    bool poly = gtype == 3, line = gtype == 2, point = gtype == 1;
    switch (layer) {
    case L_WATER:
        if (poly) r.mc = MC_WATER;
        break;
    case L_WATERWAY:
        if (!line) break;
        r.mc = (pb_eq(cl, "river") || pb_eq(cl, "canal")) ? MC_RIVER : MC_STREAM;
        break;
    case L_LANDCOVER:
        if (!poly) break;
        if (pb_eq(cl, "wood")) r.mc = MC_LAND_WOOD;
        else if (pb_eq(cl, "grass") || pb_eq(cl, "wetland") || pb_eq(cl, "farmland")) r.mc = MC_LAND_GRASS;
        else if (pb_eq(cl, "sand") || pb_eq(cl, "rock") || pb_eq(cl, "ice")) r.mc = MC_LAND_SAND;
        break;
    case L_LANDUSE:
        if (!poly) break;
        /* residential, suburb, quarter, neighbourhood: not drawn (mp_render.c) */
        if (pb_eq(cl, "commercial") || pb_eq(cl, "retail") || pb_eq(cl, "industrial") ||
                   pb_eq(cl, "garages") || pb_eq(cl, "railway")) {
            r.mc = MC_INDUS;
        } else if (pb_eq(cl, "cemetery") || pb_eq(cl, "hospital") || pb_eq(cl, "school") ||
                   pb_eq(cl, "university") || pb_eq(cl, "college") || pb_eq(cl, "kindergarten") ||
                   pb_eq(cl, "stadium") || pb_eq(cl, "pitch") || pb_eq(cl, "playground") ||
                   pb_eq(cl, "military") || pb_eq(cl, "zoo") || pb_eq(cl, "theme_park")) {
            r.mc = MC_CIVIC;
        }
        break;
    case L_PARK:
        if (poly) r.mc = MC_PARK;
        break;
    case L_BUILDING:
        if (poly) r.mc = MC_BUILDING;
        break;
    case L_AEROWAY:
        if (poly && pb_eq(cl, "aerodrome")) r.mc = MC_AERO_AREA;
        else if (line && (pb_eq(cl, "runway") || pb_eq(cl, "taxiway"))) r.mc = MC_RUNWAY;
        break;
    case L_TRANSPORTATION: {
        if (!line) break;
        static const int M[] = { MC_MOTORWAY, MC_TRUNK, MC_PRIMARY, MC_SECONDARY, MC_TERTIARY,
                                 MC_MINOR, MC_SERVICE, MC_PATH, MC_PATH };
        int rr = road_rank(cl);
        if (rr >= 0) {
            r.mc = M[rr];
            r.sub = rr;
        } else if (pb_eq(cl, "rail") || pb_eq(cl, "transit")) {
            r.mc = MC_RAIL;
        } else if (pb_eq(cl, "ferry")) {
            r.mc = MC_FERRY;
        } else if (pb_eq(cl, "busway") || pb_eq(cl, "raceway")) {
            r.mc = MC_MINOR;
            r.sub = 5;
        }
        if (tunnel) r.flags |= MF_TUNNEL;
        (void)sub;
        break;
    }
    case L_TRANSPORTATION_NAME: {
        if (!line) break;
        int rr = road_rank(cl);
        if (rr < 0) {
            if (pb_eq(cl, "rail") || pb_eq(cl, "transit")) rr = 6;
            else break;
        }
        r.mc = MC_L_ROAD;
        r.sub = rr;
        r.rank = 100 + rr * 10;
        break;
    }
    case L_BOUNDARY:
        if (line && !maritime && admin > 0 && admin <= 4) {
            r.mc = MC_BOUNDARY;
            r.sub = admin;
        }
        break;
    case L_PLACE: {
        if (!point) break;
        static const struct { const char *n; int s; } P[] = {
            { "country", MP_PLACE_COUNTRY }, { "state", MP_PLACE_STATE },
            { "city", MP_PLACE_CITY }, { "town", MP_PLACE_TOWN },
            { "village", MP_PLACE_VILLAGE }, { "suburb", MP_PLACE_SUBURB },
            { "quarter", MP_PLACE_QUARTER }, { "neighbourhood", MP_PLACE_NEIGHBOURHOOD },
            { "hamlet", MP_PLACE_HAMLET }, { "island", MP_PLACE_VILLAGE },
            { "isolated_dwelling", MP_PLACE_OTHER }, { "locality", MP_PLACE_OTHER },
        };
        for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) {
            if (pb_eq(cl, P[i].n)) {
                r.mc = MC_L_PLACE;
                r.sub = P[i].s;
                r.rank = P[i].s * 10 + (rank > 0 && rank < 10 ? rank : 9);
                break;
            }
        }
        break;
    }
    case L_WATER_NAME:
        if (point || line) {
            r.mc = MC_L_WATER;
            r.rank = 200 + (pb_eq(cl, "ocean") ? 0 : pb_eq(cl, "sea") ? 1 : pb_eq(cl, "lake") ? 3 : 5);
        }
        break;
    }
    return r;
}

/* Is the name all Latin-1? The watch's fonts carry nothing else. */
static bool latin1(const pb_t *s)
{
    for (const uint8_t *c = s->p; c < s->end; c++) {
        if (*c < 0x80) continue;
        if ((*c == 0xC2 || *c == 0xC3) && c + 1 < s->end) {
            c++;
            continue;
        }
        return false;
    }
    return true;
}

static int zz(uint32_t v)
{
    return (int)((v >> 1) ^ (uint32_t)-(int32_t)(v & 1));
}

/* The feature's geometry, appended to the accumulator. Returns the number of
 * rings (0 = nothing usable). */
static int geometry(acc_t *a, pb_t g, int gtype, int *first_ring)
{
    int cx = 0, cy = 0;
    int rings = 0;
    *first_ring = a->nr;
    int ring_start = a->np;
    while (g.p < g.end) {
        uint32_t c;
        if (!pb_varint(&g, &c)) break;
        int id = (int)(c & 7), count = (int)(c >> 3);
        if (id == 7) {                  /* ClosePath: the ring closes by itself */
            continue;
        }
        if (id != 1 && id != 2) break;
        if (id == 1) {
            /* a new ring or part starts: close the previous one */
            if (a->np > ring_start) {
                if (!grow((void **)&a->rn, &a->cr, a->nr + 1, sizeof(uint32_t), 1024)) { a->oom = true; return 0; }
                a->rn[a->nr++] = (uint32_t)(a->np - ring_start);
                rings++;
            }
            ring_start = a->np;
        }
        if (!grow((void **)&a->p, &a->cp, a->np + count, sizeof(mp_pt_t), 4096)) {
            a->oom = true;
            return 0;
        }
        for (int i = 0; i < count; i++) {
            uint32_t dx, dy;
            if (!pb_varint(&g, &dx) || !pb_varint(&g, &dy)) {
                g.p = g.end;
                break;
            }
            cx += zz(dx);
            cy += zz(dy);
            if (cx < -32000) cx = -32000;
            if (cx > 32000) cx = 32000;
            if (cy < -32000) cy = -32000;
            if (cy > 32000) cy = 32000;
            /* drop a repeated point: it adds nothing and costs a segment */
            if (a->np > ring_start && a->p[a->np - 1].x == cx && a->p[a->np - 1].y == cy) continue;
            a->p[a->np].x = (int16_t)cx;
            a->p[a->np].y = (int16_t)cy;
            a->np++;
        }
    }
    if (a->np > ring_start) {
        if (!grow((void **)&a->rn, &a->cr, a->nr + 1, sizeof(uint32_t), 1024)) { a->oom = true; return 0; }
        a->rn[a->nr++] = (uint32_t)(a->np - ring_start);
        rings++;
    }
    (void)gtype;
    return rings;
}

static void do_layer(acc_t *a, pb_t lb, layer_kv_t *kv, uint16_t *extent)
{
    int f, t;
    uint32_t v;
    pb_t s;
    pb_t name = { 0, 0 };

    /* first pass: name, keys, values, extent */
    kv->k_class = kv->k_subclass = kv->k_name = kv->k_name_latin = -1;
    kv->k_admin = kv->k_brunnel = kv->k_maritime = kv->k_rank = -1;
    kv->nvals = 0;
    int nkeys = 0;
    pb_t b = lb;
    while (pb_next(&b, &f, &t, &v, &s)) {
        if (f == 1 && t == 2) {
            name = s;
        } else if (f == 3 && t == 2) {
            if (pb_eq(&s, "class")) kv->k_class = nkeys;
            else if (pb_eq(&s, "subclass")) kv->k_subclass = nkeys;
            else if (pb_eq(&s, "name")) kv->k_name = nkeys;
            else if (pb_eq(&s, "name:latin")) kv->k_name_latin = nkeys;
            else if (pb_eq(&s, "admin_level")) kv->k_admin = nkeys;
            else if (pb_eq(&s, "brunnel")) kv->k_brunnel = nkeys;
            else if (pb_eq(&s, "maritime")) kv->k_maritime = nkeys;
            else if (pb_eq(&s, "rank")) kv->k_rank = nkeys;
            nkeys++;
        } else if (f == 4 && t == 2) {
            if (kv->nvals < MAX_KV) kv->vals[kv->nvals++] = s;
        } else if (f == 5 && t == 0) {
            *extent = (uint16_t)(v ? v : 4096);
        }
    }
    int layer = layer_id(&name);
    if (layer == L_NONE) return;
    bool want_name = layer == L_TRANSPORTATION_NAME || layer == L_PLACE || layer == L_WATER_NAME;

    /* second pass: the features */
    b = lb;
    while (pb_next(&b, &f, &t, &v, &s)) {
        if (f != 2 || t != 2) continue;
        pb_t fb = s, tags = { 0, 0 }, geom = { 0, 0 };
        int gtype = 0;
        int f2, t2;
        uint32_t v2;
        pb_t s2;
        while (pb_next(&fb, &f2, &t2, &v2, &s2)) {
            if (f2 == 2 && t2 == 2) tags = s2;
            else if (f2 == 3 && t2 == 0) gtype = (int)v2;
            else if (f2 == 4 && t2 == 2) geom = s2;
        }
        if (!geom.p || gtype < 1 || gtype > 3) continue;

        pb_t cl = { 0, 0 }, sub = { 0, 0 }, nm = { 0, 0 }, nml = { 0, 0 };
        int admin = 0, rank = 0;
        bool maritime = false, tunnel = false;
        while (tags.p && tags.p < tags.end) {
            uint32_t k, vi;
            if (!pb_varint(&tags, &k) || !pb_varint(&tags, &vi)) break;
            int ki = (int)k;
            if (ki == kv->k_class) cl = val_str(kv, vi);
            else if (ki == kv->k_subclass) sub = val_str(kv, vi);
            else if (ki == kv->k_name) nm = val_str(kv, vi);
            else if (ki == kv->k_name_latin) nml = val_str(kv, vi);
            else if (ki == kv->k_admin) admin = val_int(kv, vi, 0);
            else if (ki == kv->k_maritime) maritime = val_int(kv, vi, 0) != 0;
            else if (ki == kv->k_rank) rank = val_int(kv, vi, 0);
            else if (ki == kv->k_brunnel) {
                pb_t br = val_str(kv, vi);
                tunnel = pb_eq(&br, "tunnel");
            }
        }
        if (!cl.p) cl.p = cl.end = (const uint8_t *)"";
        cls_t c = classify(layer, gtype, &cl, &sub, admin, maritime, tunnel, rank);
        if (c.mc < 0) continue;

        int32_t name_off = -1;
        if (want_name) {
            /* the local name when the fonts can draw it, else its Latin form */
            pb_t n = (nm.p && latin1(&nm)) ? nm : (nml.p && latin1(&nml)) ? nml : nm;
            int nl = n.p ? (int)(n.end - n.p) : 0;
            if (nl <= 0 || nl > 80 || !latin1(&n)) continue;
            if (!grow((void **)&a->s, &a->cs, a->ns + nl + 1, 1, 4096)) { a->oom = true; return; }
            memcpy(a->s + a->ns, n.p, (size_t)nl);
            a->s[a->ns + nl] = 0;
            name_off = a->ns;
            a->ns += nl + 1;
        }

        if (!grow((void **)&a->f, &a->cf, a->nf + 1, sizeof(mp_feat_t), 256)) { a->oom = true; return; }
        mp_feat_t *ft = &a->f[a->nf];
        ft->pt = (uint32_t)a->np;
        int first_ring;
        int rings = geometry(a, geom, gtype, &first_ring);
        if (a->oom) return;
        if (rings <= 0) continue;
        ft->ring = (uint32_t)first_ring;
        ft->nring = (uint16_t)(rings > 65535 ? 65535 : rings);
        ft->flags = (uint8_t)(c.flags | (gtype == 3 ? MF_POLY : 0) | (gtype == 1 ? MF_POINT : 0));
        ft->sub = (uint8_t)c.sub;
        ft->name = name_off;
        ft->rank = (int16_t)c.rank;
        /* the class travels in rank's place until the sort; see below */
        ft->rank = (int16_t)((c.mc << 10) | (c.rank & 0x3FF));
        int16_t x0 = 32767, y0 = 32767, x1 = -32768, y1 = -32768;
        for (int i = (int)ft->pt; i < a->np; i++) {
            if (a->p[i].x < x0) x0 = a->p[i].x;
            if (a->p[i].x > x1) x1 = a->p[i].x;
            if (a->p[i].y < y0) y0 = a->p[i].y;
            if (a->p[i].y > y1) y1 = a->p[i].y;
        }
        ft->bx0 = x0;
        ft->by0 = y0;
        ft->bx1 = x1;
        ft->by1 = y1;
        a->nf++;
    }
}

static int feat_cmp(const void *x, const void *y)
{
    const mp_feat_t *a = (const mp_feat_t *)x, *b = (const mp_feat_t *)y;
    return (int)(a->rank >> 10) - (int)(b->rank >> 10);
}

mp_tile_t *mp_mvt_decode(const uint8_t *buf, int len, int z, uint32_t x, uint32_t y)
{
    mp_tile_t *t = (mp_tile_t *)mp_calloc(sizeof(mp_tile_t));
    if (!t) return NULL;
    t->z = (uint8_t)z;
    t->x = x;
    t->y = y;
    t->extent = 4096;

    layer_kv_t *kv = (layer_kv_t *)mp_malloc(sizeof(layer_kv_t));
    if (!kv) {
        mp_free(t);
        return NULL;
    }
    acc_t a;
    memset(&a, 0, sizeof a);

    pb_t b = { buf, buf + (len > 0 ? len : 0) };
    int f, ty;
    uint32_t v;
    pb_t s;
    while (!a.oom && pb_next(&b, &f, &ty, &v, &s)) {
        if (f == 3 && ty == 2) do_layer(&a, s, kv, &t->extent);
    }
    mp_free(kv);
    if (a.oom) {
        mp_free(a.f);
        mp_free(a.rn);
        mp_free(a.p);
        mp_free(a.s);
        mp_free(t);
        return NULL;
    }

    /* by class, stable enough: qsort is not stable, but within a class the
     * order only decides which of two equal roads is on top */
    if (a.nf > 1) qsort(a.f, (size_t)a.nf, sizeof(mp_feat_t), feat_cmp);
    int c = 0;
    for (int i = 0; i < a.nf; i++) {
        int mc = a.f[i].rank >> 10;
        while (c <= mc) t->cls[c++] = i;
        a.f[i].rank = (int16_t)(a.f[i].rank & 0x3FF);
    }
    while (c <= MC_COUNT) t->cls[c++] = a.nf;

    t->f = a.f;
    t->nf = a.nf;
    t->ring_n = a.rn;
    t->nring = a.nr;
    t->p = a.p;
    t->np = a.np;
    t->names = a.s;
    t->names_len = a.ns;
    t->bytes = (uint32_t)(sizeof(mp_tile_t) + (size_t)a.cf * sizeof(mp_feat_t) +
                          (size_t)a.cr * 4 + (size_t)a.cp * sizeof(mp_pt_t) + (size_t)a.cs);
    return t;
}

void mp_tile_free(mp_tile_t *t)
{
    if (!t) return;
    mp_free(t->f);
    mp_free(t->ring_n);
    mp_free(t->p);
    mp_free(t->names);
    mp_free(t);
}
