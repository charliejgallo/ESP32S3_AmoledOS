/*
 * VISOR 3D - meshes (see v3_mesh.h)
 *
 * An STL is a bag of loose triangles: every vertex repeated in each triangle
 * that uses it, six times over on a closed surface. They are WELDED on
 * loading by snapping every vertex to a grid over the model's box and
 * merging those that land in the same cell, which is also how a model too big
 * to draw is REDUCED (vertex clustering): the same pass with a coarser grid.
 * Triangles whose three corners fall into fewer than three cells vanish.
 * A fine grid (4096 cells a side) welds without changing anything a watch can
 * show; a coarse one keeps the shape and loses the detail, which on a 1.8"
 * screen is the right trade.
 *
 * The file is read twice: once for the box, once to cluster, streaming, so a
 * 100 000-triangle STL never sits in memory whole (5 MB as loose floats).
 */
#include "v3_mesh.h"

/* The errors are shown to the user (the viewer's strip translates them with
 * _()); marked here for tools/gen_lang.py, with no dependency on the UI. */
#ifndef N_
#define N_(s) (s)
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#define TBITS       18                  /* hash table: 262144 slots, 1 MB  */
#define TSIZE       (1u << TBITS)
#define MAX_CL      (1u << 17)          /* clusters (vertices) at most     */
#define WELD_GRID   4096

typedef struct {
    uint16_t q[3];
    uint16_t n;
    float    s[3];
} cluster_t;

typedef struct {
    FILE    *f;
    long     size;
    bool     ascii;
    uint32_t nt;                        /* declared (binary) or counted     */
    volatile int *progress;
} src_t;

/* ---- reading triangles, whatever the flavour -------------------------- */

typedef bool (*tri_fn)(void *ctx, const float p[9]);

static bool (*s_yield)(void);
static bool s_cancel;
static int  s_stage;                    /* for the progress: see v3_mesh.h */

void v3_mesh_set_yield(bool (*fn)(void))
{
    s_yield = fn;
}

/* false = cancelled: every reading loop stops on it. */
static inline bool maybe_yield(uint32_t n)
{
    if (s_yield && (n & 1023) == 0 && !s_yield()) s_cancel = true;
    return !s_cancel;
}

static inline void report(volatile int *p, uint32_t done, uint32_t total)
{
    if (p && total) *p = s_stage * 1000 + (int)(done * 100 / total);
}

/* STL is Z-up (CAD); the viewer is Y-up: (x, y, z) -> (x, z, -y). */
static void zup_to_yup(float p[9])
{
    for (int k = 0; k < 3; k++) {
        float y = p[k * 3 + 1], z = p[k * 3 + 2];
        p[k * 3 + 1] = z;
        p[k * 3 + 2] = -y;
    }
}

static bool read_binary(src_t *s, tri_fn fn, void *ctx)
{
    fseek(s->f, 84, SEEK_SET);
    uint8_t *buf = malloc(50 * 128);    /* PSRAM, not the worker's stack */
    if (!buf) return false;
    uint32_t done = 0;
    bool ok = true;
    while (ok && done < s->nt) {
        uint32_t n = s->nt - done > 128 ? 128 : s->nt - done;
        if (fread(buf, 50, n, s->f) != n) { ok = false; break; }
        for (uint32_t i = 0; i < n && ok; i++) {
            float p[9];
            memcpy(p, buf + i * 50 + 12, 36);
            zup_to_yup(p);
            ok = fn(ctx, p);
        }
        done += n;
        if (!maybe_yield(done)) ok = false;
        report(s->progress, done, s->nt);
    }
    free(buf);
    return ok;
}

/* fgets is not in the firmware's table: lines by hand, over fread. A line
 * longer than the buffer is cut, which for "vertex x y z" never happens. */
typedef struct {
    FILE *f;
    char *buf;
    int   len, pos;
} lines_t;

static bool next_line(lines_t *r, char *line, int size)
{
    int n = 0;
    for (;;) {
        if (r->pos >= r->len) {
            r->len = (int)fread(r->buf, 1, 2048, r->f);
            r->pos = 0;
            if (r->len <= 0) {
                line[n] = 0;
                return n > 0;
            }
        }
        char c = r->buf[r->pos++];
        if (c == '\n') break;
        if (n < size - 1) line[n++] = c;
    }
    line[n] = 0;
    return true;
}

static bool read_ascii(src_t *s, tri_fn fn, void *ctx, bool count_only)
{
    fseek(s->f, 0, SEEK_SET);
    lines_t r = { .f = s->f, .buf = malloc(2048) };
    if (!r.buf) return false;
    char line[160];
    float p[9];
    int k = 0;
    uint32_t n = 0;
    bool ok = true;
    while (ok && next_line(&r, line, sizeof line)) {
        const char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        if (strncmp(t, "vertex", 6) != 0) continue;
        if (count_only) {
            if (++k == 3) {
                k = 0;
                if (!maybe_yield(++n)) { ok = false; break; }
            }
            continue;
        }
        char *e;
        t += 6;
        p[k * 3 + 0] = strtof(t, &e);
        p[k * 3 + 1] = strtof(e, &e);
        p[k * 3 + 2] = strtof(e, &e);
        if (++k == 3) {
            k = 0;
            zup_to_yup(p);
            ok = fn(ctx, p) && maybe_yield(n + 1);
            n++;
            report(s->progress, n, s->nt);
        }
    }
    free(r.buf);
    if (count_only) s->nt = n;
    return ok;
}

static bool for_each_tri(src_t *s, tri_fn fn, void *ctx)
{
    return s->ascii ? read_ascii(s, fn, ctx, false) : read_binary(s, fn, ctx);
}

/* ---- pass 1: the box ---------------------------------------------------- */

typedef struct { float lo[3], hi[3]; } box_t;

static bool box_fn(void *ctx, const float p[9])
{
    box_t *b = ctx;
    for (int k = 0; k < 9; k++) {
        int a = k % 3;
        if (p[k] < b->lo[a]) b->lo[a] = p[k];
        if (p[k] > b->hi[a]) b->hi[a] = p[k];
    }
    return true;
}

/* ---- pass 2: clustering ------------------------------------------------- */

typedef struct {
    box_t      box;
    float      inv[3];                  /* cells per unit, per axis         */
    int        grid;
    uint32_t  *table;                   /* cluster index + 1, 0 = empty     */
    cluster_t *cl;
    uint32_t   ncl;
    uint32_t  *tri;                     /* kept triangles                   */
    uint32_t   ntri, cap;
    bool       overflow;
} clus_t;

static uint32_t cell_of(clus_t *c, const float *v)
{
    uint16_t q[3];
    for (int a = 0; a < 3; a++) {
        int iq = (int)((v[a] - c->box.lo[a]) * c->inv[a]);
        if (iq < 0) iq = 0;
        if (iq >= c->grid) iq = c->grid - 1;
        q[a] = (uint16_t)iq;
    }
    uint32_t h = ((uint32_t)q[0] * 73856093u) ^ ((uint32_t)q[1] * 19349663u) ^
                 ((uint32_t)q[2] * 83492791u);
    for (uint32_t i = h & (TSIZE - 1);; i = (i + 1) & (TSIZE - 1)) {
        uint32_t e = c->table[i];
        if (!e) {
            if (c->ncl >= MAX_CL) {
                c->overflow = true;
                return 0;
            }
            cluster_t *k = &c->cl[c->ncl];
            memcpy(k->q, q, sizeof q);
            k->n = 0;
            k->s[0] = k->s[1] = k->s[2] = 0;
            c->table[i] = ++c->ncl;
            e = c->ncl;
        }
        cluster_t *k = &c->cl[e - 1];
        if (k->q[0] == q[0] && k->q[1] == q[1] && k->q[2] == q[2]) {
            if (k->n < 65535) {
                k->n++;
                k->s[0] += v[0]; k->s[1] += v[1]; k->s[2] += v[2];
            }
            return e - 1;
        }
    }
}

static bool clus_fn(void *ctx, const float p[9])
{
    clus_t *c = ctx;
    uint32_t a = cell_of(c, p), b = cell_of(c, p + 3), d = cell_of(c, p + 6);
    if (c->overflow) return false;
    if (a == b || b == d || a == d) return true;        /* collapsed */
    if (c->ntri >= c->cap) {
        c->overflow = true;
        return false;
    }
    c->tri[c->ntri * 3 + 0] = a;
    c->tri[c->ntri * 3 + 1] = b;
    c->tri[c->ntri * 3 + 2] = d;
    c->ntri++;
    return true;
}

/* ---- the result --------------------------------------------------------- */

/* Centred on the box, radius 1, and the face normals. */
static void finish(v3_mesh_t *m)
{
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (int i = 0; i < m->nv; i++) {
        for (int a = 0; a < 3; a++) {
            float x = m->v[i * 3 + a];
            if (x < lo[a]) lo[a] = x;
            if (x > hi[a]) hi[a] = x;
        }
    }
    float c[3] = { (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2 };
    float r2 = 0;
    for (int i = 0; i < m->nv; i++) {
        float *v = &m->v[i * 3];
        v[0] -= c[0]; v[1] -= c[1]; v[2] -= c[2];
        float d = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (d > r2) r2 = d;
    }
    float k = r2 > 0 ? 1.0f / sqrtf(r2) : 1.0f;
    for (int i = 0; i < m->nv * 3; i++) m->v[i] *= k;

    for (int t = 0; t < m->nt; t++) {
        const float *a = &m->v[m->idx[t * 3 + 0] * 3];
        const float *b = &m->v[m->idx[t * 3 + 1] * 3];
        const float *d = &m->v[m->idx[t * 3 + 2] * 3];
        float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        float vx = d[0] - a[0], vy = d[1] - a[1], vz = d[2] - a[2];
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        float l = sqrtf(nx * nx + ny * ny + nz * nz);
        if (l > 0) { nx /= l; ny /= l; nz /= l; }
        m->fn[t * 3 + 0] = nx;
        m->fn[t * 3 + 1] = ny;
        m->fn[t * 3 + 2] = nz;
    }
}

static bool load_stl(v3_mesh_t *m, FILE *f, long size, volatile int *progress)
{
    src_t s = { .f = f, .size = size, .progress = progress };
    uint8_t head[84];
    if (fread(head, 1, 84, f) != 84) {
        snprintf(m->err, sizeof m->err, "%s", N_("archivo demasiado corto"));
        return false;
    }
    uint32_t n;
    memcpy(&n, head + 80, 4);
    s.ascii = !((long)n * 50 + 84 == size);
    if (s.ascii) {
        if (strncmp((const char *)head, "solid", 5) != 0) {
            snprintf(m->err, sizeof m->err, "%s", N_("no es un STL"));
            return false;
        }
        read_ascii(&s, NULL, NULL, true);
    } else {
        s.nt = n;
    }
    if (s.nt == 0) {
        snprintf(m->err, sizeof m->err, "%s", N_("no tiene triángulos"));
        return false;
    }
    m->nt_file = (int)s.nt;

    box_t box = { { 1e30f, 1e30f, 1e30f }, { -1e30f, -1e30f, -1e30f } };
    volatile int *keep = s.progress;
    s.progress = NULL;                  /* the box pass is quick: one bar */
    if (!for_each_tri(&s, box_fn, &box)) {
        snprintf(m->err, sizeof m->err, "%s", s_cancel ? N_("cancelado") : N_("error de lectura"));
        return false;
    }
    s.progress = keep;

    clus_t c = { .box = box };
    c.table = malloc(TSIZE * sizeof(uint32_t));
    c.cl = malloc(MAX_CL * sizeof(cluster_t));
    c.cap = s.nt < V3_BUDGET * 2u ? s.nt : V3_BUDGET * 2u;
    c.tri = malloc((size_t)c.cap * 3 * sizeof(uint32_t));
    if (!c.table || !c.cl || !c.tri) {
        free(c.table); free(c.cl); free(c.tri);
        snprintf(m->err, sizeof m->err, "%s", N_("sin memoria"));
        return false;
    }

    /* Welding grid if it fits; otherwise the grid a surface of about the
     * budget's triangles needs (a closed surface over G cells a side has
     * roughly 6 G^2 of them), then corrected: coarser while it overflows,
     * finer while it comes out far under, keeping the last grid that fit. */
    int grid = s.nt <= V3_BUDGET ? WELD_GRID : (int)(sqrtf(V3_BUDGET / 6.0f) * 1.4f);
    int good = 0;
    bool ok = false;
    for (int attempt = 0; attempt < 6; attempt++) {
        s_stage = attempt + 1;
        c.grid = grid;
        for (int a = 0; a < 3; a++) {
            float span = box.hi[a] - box.lo[a];
            c.inv[a] = span > 0 ? (float)grid / span : 0;
        }
        memset(c.table, 0, TSIZE * sizeof(uint32_t));
        c.ncl = c.ntri = 0;
        c.overflow = false;
        bool read = for_each_tri(&s, clus_fn, &c);
        if (s_cancel) {
            snprintf(m->err, sizeof m->err, "%s", N_("cancelado"));
            break;
        }
        if (!read && !c.overflow) {
            snprintf(m->err, sizeof m->err, "%s", N_("error de lectura"));
            break;
        }
        bool fits = !c.overflow && c.ntri <= V3_BUDGET;
        if (fits) {
            ok = true;
            bool far_under = grid < WELD_GRID && c.ntri < V3_BUDGET / 3;
            if (!far_under || good == grid || attempt >= 4) break;
            good = grid;
            grid = grid * 3 / 2;                /* too coarse: finer */
            continue;
        }
        ok = false;
        if (good) {                             /* finer overflowed: back */
            grid = good;
            good = grid;                        /* and stop after it */
            continue;
        }
        grid = grid * 2 / 3;                    /* too many: coarser */
        if (grid < 8) break;
    }
    if (!ok || c.ntri == 0) {
        if (!m->err[0]) snprintf(m->err, sizeof m->err, "%s", N_("no se pudo reducir"));
        free(c.table); free(c.cl); free(c.tri);
        return false;
    }
    free(c.table);

    m->nv = (int)c.ncl;
    m->nt = (int)c.ntri;
    m->v = malloc((size_t)m->nv * 3 * sizeof(float));
    m->fn = malloc((size_t)m->nt * 3 * sizeof(float));
    m->idx = realloc(c.tri, (size_t)m->nt * 3 * sizeof(uint32_t));
    if (!m->idx) m->idx = c.tri;
    if (!m->v || !m->fn) {
        free(c.cl);
        snprintf(m->err, sizeof m->err, "%s", N_("sin memoria"));
        return false;
    }
    for (int i = 0; i < m->nv; i++) {
        float n = c.cl[i].n ? (float)c.cl[i].n : 1.0f;
        m->v[i * 3 + 0] = c.cl[i].s[0] / n;
        m->v[i * 3 + 1] = c.cl[i].s[1] / n;
        m->v[i * 3 + 2] = c.cl[i].s[2] / n;
    }
    free(c.cl);
    finish(m);
    return true;
}

static bool load_m3d(v3_mesh_t *m, FILE *f, volatile int *progress)
{
    uint8_t h[16];
    if (fread(h, 1, 16, f) != 16 || memcmp(h, "M3D1", 4) != 0) {
        snprintf(m->err, sizeof m->err, "%s", N_("no es un M3D"));
        return false;
    }
    uint32_t nv, nt, flags;
    memcpy(&nv, h + 4, 4);
    memcpy(&nt, h + 8, 4);
    memcpy(&flags, h + 12, 4);
    if (nv == 0 || nt == 0 || nt > V3_BUDGET * 4u || nv > MAX_CL) {
        snprintf(m->err, sizeof m->err, "%s", N_("demasiados triángulos"));
        return false;
    }
    m->nv = (int)nv;
    m->nt = (int)nt;
    m->nt_file = (int)nt;
    m->v = malloc((size_t)nv * 12);
    m->idx = malloc((size_t)nt * 12);
    m->fn = malloc((size_t)nt * 12);
    if (flags & V3_M3D_COLORS) m->fc = malloc((size_t)nt * 2);
    if (!m->v || !m->idx || !m->fn || ((flags & V3_M3D_COLORS) && !m->fc)) {
        snprintf(m->err, sizeof m->err, "%s", N_("sin memoria"));
        return false;
    }
    if (progress) *progress = 20;
    if (fread(m->v, 12, nv, f) != nv) goto short_file;
    if (progress) *progress = 60;
    if (fread(m->idx, 12, nt, f) != nt) goto short_file;
    if (m->fc && fread(m->fc, 2, nt, f) != nt) goto short_file;
    for (uint32_t i = 0; i < nt * 3; i++) {
        if (m->idx[i] >= nv) {
            snprintf(m->err, sizeof m->err, "%s", N_("índice inválido"));
            return false;
        }
    }
    if (progress) *progress = 100;
    finish(m);
    return true;
short_file:
    snprintf(m->err, sizeof m->err, "%s", N_("archivo incompleto"));
    return false;
}

bool v3_mesh_load(v3_mesh_t *m, const char *path, volatile int *progress)
{
    memset(m, 0, sizeof *m);
    s_cancel = false;
    s_stage = 1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(m->err, sizeof m->err, "%s", N_("no se pudo abrir el archivo"));
        return false;
    }
#if defined(ESP_PLATFORM) && defined(__SNBF)
    /* Unbuffered, which is what setvbuf(f, NULL, _IONBF, 0) does - but
     * setvbuf is not in the firmware's table. With newlib's 128-byte buffer
     * every fread of 6 KB became 50 reads through VFS, FATFS and the SD
     * driver: a 4 MB STL took seconds a pass. Unbuffered, newlib hands our
     * buffer straight to read(). Set before the first read, as setvbuf
     * requires. */
    f->_flags |= __SNBF;
#endif
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    const char *dot = strrchr(path, '.');
    bool ok = (dot && (!strcmp(dot, ".m3d") || !strcmp(dot, ".M3D")))
                  ? load_m3d(m, f, progress)
                  : load_stl(m, f, size, progress);
    fclose(f);
    if (!ok) {
        char err[64];
        memcpy(err, m->err, sizeof err);
        v3_mesh_free(m);
        memcpy(m->err, err, sizeof err);
    }
    return ok;
}

void v3_mesh_free(v3_mesh_t *m)
{
    free(m->v);
    free(m->idx);
    free(m->fn);
    free(m->fc);
    memset(m, 0, sizeof *m);
}
