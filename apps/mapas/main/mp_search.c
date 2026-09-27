/*
 * MAPAS - search. See mp_search.h.
 */
#include "mp_search.h"
#include "mp_mem.h"
#include "mp_store.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---------------------------------------------------------------------------
 * Keys
 * ------------------------------------------------------------------------- */

/* Latin-1 letters (as the second byte after 0xC3) without their accent */
static char fold(unsigned char c2)
{
    static const char T[] =
        "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs"
        "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
    return T[c2 & 0x3F];
}

void mp_search_key(const char *in, char *out, int n)
{
    int o = 0;
    bool space = true;
    for (const unsigned char *p = (const unsigned char *)in; *p && o < n - 1; p++) {
        char c;
        if (*p == 0xC3 && p[1]) {
            c = fold(*++p);
        } else if (*p >= 0x80) {
            continue;
        } else {
            c = (char)*p;
        }
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == ' ' || c == '\t') {
            if (space) continue;
            space = true;
        } else {
            space = false;
        }
        out[o++] = c;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
}

/* ---------------------------------------------------------------------------
 * The offline indexes
 * ------------------------------------------------------------------------- */

static int kind_of(const char *k)
{
    if (!strcmp(k, "lugar")) return MP_KIND_PLACE;
    if (!strcmp(k, "calle")) return MP_KIND_STREET;
    if (!strcmp(k, "agua")) return MP_KIND_WATER;
    return MP_KIND_POI;
}

/* where in key the query sits: 0 at the start, 100 at a word's start, 200
 * inside a word, -1 nowhere */
static int match(const char *key, const char *q)
{
    const char *p = strstr(key, q);
    if (!p) return -1;
    int best = 200;
    while (p) {
        if (p == key) return 0;
        char b = p[-1];
        if (b == ' ' || b == '-' || b == '(' || b == '.' || b == '\'' || b == '/') best = 100;
        p = strstr(p + 1, q);
    }
    return best;
}

/* a name into a fixed buffer without cutting a letter in half */
static void copy_utf8(char *dst, size_t n, const char *src)
{
    size_t l = strlen(src);
    if (l >= n) {
        l = n - 1;
        while (l > 0 && ((unsigned char)src[l] & 0xC0) == 0x80) l--;
    }
    memcpy(dst, src, l);
    dst[l] = 0;
}

static void keep(mp_hit_t *out, int *n, int max, const mp_hit_t *h)
{
    /* the same name at nearly the same spot is one hit (a street's name in
     * two tiles) */
    for (int i = 0; i < *n; i++) {
        if (!strcmp(out[i].name, h->name) && labs((long)(out[i].lat - h->lat)) < 3000 &&
            labs((long)(out[i].lon - h->lon)) < 3000) {
            if (h->score < out[i].score) out[i] = *h;
            return;
        }
    }
    int k = *n < max ? (*n)++ : max - 1;
    if (k == max - 1 && *n == max && out[k].score <= h->score) return;
    while (k > 0 && out[k - 1].score > h->score) {
        out[k] = out[k - 1];
        k--;
    }
    out[k] = *h;
}

static void line(char *l, const char *q, mp_hit_t *out, int *n, int max)
{
    char *f[5] = { l, NULL, NULL, NULL, NULL };
    for (int i = 1; i < 5; i++) {
        char *t = strchr(f[i - 1], '\t');
        if (!t) return;
        *t = 0;
        f[i] = t + 1;
    }
    int m = match(f[0], q);
    if (m < 0) return;
    mp_hit_t h;
    memset(&h, 0, sizeof h);
    copy_utf8(h.name, sizeof h.name, f[1]);
    copy_utf8(h.sub, sizeof h.sub, f[2]);
    h.kind = (uint8_t)kind_of(f[2]);
    h.lat = (int32_t)strtol(f[3], NULL, 10);
    h.lon = (int32_t)strtol(f[4], NULL, 10);
    static const int KB[] = { 0, 10, 30, 15, 5 };
    h.score = m + KB[h.kind] + (int)strlen(f[0]) / 4;
    keep(out, n, max, &h);
}

/* The first version's text index: every line read and matched. ~1 s per
 * 450 KB on the board, which for a city (3 MB) was seconds per search. */
static void legacy_file(FILE *fp, const char *key, mp_hit_t *out, int *n, int max,
                        char *buf, int CH, volatile bool *cancel)
{
    int have = 0;
    for (;;) {
        int r = (int)fread(buf + have, 1, (size_t)(CH - have), fp);
        if (r <= 0 && have == 0) break;
        int len = have + (r > 0 ? r : 0);
        buf[len] = 0;
        char *p = buf;
        for (;;) {
            char *nl = strchr(p, '\n');
            if (!nl) break;
            *nl = 0;
            line(p, key, out, n, max);
            p = nl + 1;
        }
        have = (int)(buf + len - p);
        if (r <= 0) {
            if (have > 0) line(p, key, out, n, max);
            break;
        }
        if (have >= CH - 1) have = 0;           /* a line longer than a chunk: drop it */
        memmove(buf, p, (size_t)have);
        if (*cancel) break;
    }
}

/* ---------------------------------------------------------------------------
 * AIX2: one key per word, sorted, found by blocks
 *
 * The format is written by tools/map_pack.py (write_idx) and the portal's
 * /mapas page; see the comment there. A search reads the header, the block
 * table (the first key of every 256: 9 bytes each, a few KB for a city),
 * then only the stretch of keys that starts with the query's longest word,
 * and at most CHECK name records, in file order.
 * ------------------------------------------------------------------------- */

#define KEY_LEN   9
#define MAX_CAND  600
#define CHECK     48
#define MAX_WORDS 6
#define WORD_LEN  24

typedef struct {
    char     k[KEY_LEN];
    uint8_t  cls, flags, nlen;
    uint32_t off;
} ikey_t;                               /* 16 bytes, as '<9sBBBI' */

typedef struct {
    uint32_t off;
    int      score;
} cand_t;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool alnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

/* the query's words (already in key form) */
static int words(const char *q, char w[][WORD_LEN], int max)
{
    int n = 0;
    while (*q && n < max) {
        while (*q && !alnum(*q)) q++;
        int k = 0;
        while (alnum(*q)) {
            if (k < WORD_LEN - 1) w[n][k++] = *q;
            q++;
        }
        if (k) w[n++][k] = 0;
    }
    return n;
}

/* every query word starts some word of the name; *first: the first one
 * starts the name */
static bool words_match(const char *name, char w[][WORD_LEN], int nw, bool *first)
{
    *first = false;
    for (int i = 0; i < nw; i++) {
        size_t l = strlen(w[i]);
        bool found = false;
        for (const char *p = name; *p && !found; p++) {
            if (!alnum(*p) || (p > name && alnum(p[-1]))) continue;
            if (!strncmp(p, w[i], l)) {
                found = true;
                if (i == 0 && p == name) *first = true;
            }
        }
        if (!found) return false;
    }
    return true;
}

static const int CLS_KIND[4] = { MP_KIND_PLACE, MP_KIND_STREET, MP_KIND_WATER, MP_KIND_POI };
static const int KIND_W[5] = { 0, 10, 30, 15, 5 };      /* by MP_KIND_*: places first */

static int cand_score_cmp(const void *a, const void *b)
{
    return ((const cand_t *)a)->score - ((const cand_t *)b)->score;
}

static int cand_off_cmp(const void *a, const void *b)
{
    uint32_t x = ((const cand_t *)a)->off, y = ((const cand_t *)b)->off;
    return x < y ? -1 : x > y;
}

static void aix2_file(FILE *fp, const uint8_t *h, const char *key, mp_hit_t *out, int *n, int max,
                      volatile bool *cancel)
{
    uint32_t nkeys = rd32(h + 8), nkinds = rd32(h + 12), kinds_off = rd32(h + 16);
    uint32_t blocks_off = rd32(h + 20), keys_off = rd32(h + 24), names_off = rd32(h + 28);
    uint32_t block = rd32(h + 32);
    if (!nkeys || !block || block > 4096 || nkeys > 4000000 || blocks_off < kinds_off || keys_off < blocks_off || names_off < keys_off) return;
    uint32_t nblocks = (nkeys + block - 1) / block;
    if (keys_off - blocks_off != nblocks * KEY_LEN || blocks_off - kinds_off > 8192) return;

    char w[MAX_WORDS][WORD_LEN];
    int nw = words(key, w, MAX_WORDS);
    if (!nw) return;
    /* the longest word leads: fewer keys start with it */
    int lead = 0;
    for (int i = 1; i < nw; i++)
        if (strlen(w[i]) > strlen(w[lead])) lead = i;
    char q[KEY_LEN + 1];
    snprintf(q, sizeof q, "%.9s", w[lead]);
    size_t L = strlen(q);
    bool full = nw == 1 && strlen(w[lead]) <= KEY_LEN;   /* the key alone answers */

    size_t kb = blocks_off - kinds_off;
    /* one block: the kinds (and a NUL), the block table, a chunk of keys
     * (aligned), the candidates */
    uint8_t *mem = (uint8_t *)mp_malloc(kb + 1 + (size_t)nblocks * KEY_LEN + 4 + (size_t)block * sizeof(ikey_t) +
                                        MAX_CAND * sizeof(cand_t));
    if (!mem) return;
    char *kinds = (char *)mem;
    uint8_t *blocks = mem + kb + 1;
    ikey_t *chunk = (ikey_t *)(((uintptr_t)(blocks + (size_t)nblocks * KEY_LEN) + 3) & ~(uintptr_t)3);
    cand_t *cand = (cand_t *)(chunk + block);
    if (fseek(fp, (long)kinds_off, SEEK_SET) || fread(kinds, 1, kb, fp) != kb ||
        fread(blocks, KEY_LEN, nblocks, fp) != nblocks) {
        mp_free(mem);
        return;
    }
    kinds[kb] = 0;

    /* the last block whose first key sorts before the query */
    uint32_t b = 0;
    {
        uint32_t lo = 0, hi = nblocks;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            if (memcmp(blocks + (size_t)mid * KEY_LEN, q, L) < 0) lo = mid + 1;
            else hi = mid;
        }
        b = lo ? lo - 1 : 0;
    }

    int nc = 0;
    bool done = false;
    for (; b < nblocks && !done && !*cancel; b++) {
        uint32_t first = b * block, cnt = nkeys - first < block ? nkeys - first : block;
        if (fseek(fp, (long)(keys_off + first * sizeof(ikey_t)), SEEK_SET) ||
            fread(chunk, sizeof(ikey_t), cnt, fp) != cnt)
            break;
        for (uint32_t i = 0; i < cnt; i++) {
            int c = memcmp(chunk[i].k, q, L);
            if (c < 0) continue;
            if (c > 0) {
                done = true;
                break;
            }
            int kind = CLS_KIND[chunk[i].cls & 3];
            int score = (lead == 0 && (chunk[i].flags & 1) ? 0 : 100) + KIND_W[kind] + chunk[i].nlen / 4;
            int j;
            for (j = 0; j < nc && cand[j].off != chunk[i].off; j++) {}
            if (j < nc) {
                if (score < cand[j].score) cand[j].score = score;
            } else if (nc < MAX_CAND) {
                cand[nc].off = chunk[i].off;
                cand[nc].score = score;
                nc++;
            } else {
                done = true;
                break;
            }
        }
    }

    /* the best by their keys; their names read in file order */
    qsort(cand, (size_t)nc, sizeof(cand_t), cand_score_cmp);
    int nchk = nc < CHECK ? nc : CHECK;
    if (!full) nchk = nc < CHECK * 3 ? nc : CHECK * 3;   /* some will not match every word */
    qsort(cand, (size_t)nchk, sizeof(cand_t), cand_off_cmp);
    for (int i = 0; i < nchk && !*cancel; i++) {
        uint8_t rec[80];
        if (fseek(fp, (long)(names_off + cand[i].off), SEEK_SET)) continue;
        size_t r = fread(rec, 1, sizeof rec - 1, fp);
        if (r < 10) continue;
        rec[r] = 0;
        mp_hit_t hh;
        memset(&hh, 0, sizeof hh);
        hh.lat = (int32_t)rd32(rec);
        hh.lon = (int32_t)rd32(rec + 4);
        uint8_t ki = rec[8];
        copy_utf8(hh.name, sizeof hh.name, (const char *)rec + 9);
        const char *ks = kinds;
        for (uint8_t k = 0; k < ki && k < nkinds && *ks; k++) ks += strlen(ks) + 1;
        copy_utf8(hh.sub, sizeof hh.sub, ks);
        hh.kind = (uint8_t)kind_of(ks);
        char nk[MP_HIT_NAME];
        mp_search_key(hh.name, nk, sizeof nk);
        bool at_start;
        if (!words_match(nk, w, nw, &at_start)) continue;
        hh.score = (at_start ? 0 : 100) + KIND_W[hh.kind] + (int)strlen(nk) / 4;
        keep(out, n, max, &hh);
    }
    mp_free(mem);
}

int mp_search_files(const char *key, mp_hit_t *out, int max, volatile bool *cancel)
{
    int n = 0;
    const char *dir = mp_maps_dir();
    if (!dir || !key[0]) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    const int CH = 16 * 1024;
    char *buf = (char *)mp_malloc((size_t)CH + 256);
    if (!buf) {
        closedir(d);
        return 0;
    }
    struct dirent *e;
    while ((e = readdir(d)) && !*cancel) {
        const char *dot = strrchr(e->d_name, '.');
        if (e->d_name[0] == '.' || !dot || strcasecmp(dot, ".idx")) continue;
        char path[400];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *fp = fopen(path, "rb");
        if (!fp) continue;
        uint8_t h[40];
        if (fread(h, 1, sizeof h, fp) == sizeof h && !memcmp(h, "AIX2", 4)) {
            aix2_file(fp, h, key, out, &n, max, cancel);
        } else {
            fseek(fp, 0, SEEK_SET);
            legacy_file(fp, key, out, &n, max, buf, CH, cancel);
        }
        fclose(fp);
    }
    closedir(d);
    mp_free(buf);
    return n;
}

/* ---------------------------------------------------------------------------
 * Photon
 * ------------------------------------------------------------------------- */

static void put_utf8(char *o, int *k, int max, unsigned cp)
{
    if (cp < 0x80) {
        if (*k < max - 1) o[(*k)++] = (char)cp;
    } else if (cp < 0x800) {
        if (*k < max - 2) {
            o[(*k)++] = (char)(0xC0 | (cp >> 6));
            o[(*k)++] = (char)(0x80 | (cp & 0x3F));
        }
    } else if (*k < max - 3) {
        o[(*k)++] = (char)(0xE0 | (cp >> 12));
        o[(*k)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        o[(*k)++] = (char)(0x80 | (cp & 0x3F));
    }
}

/* the string value of "field" between from and to, JSON escapes undone */
static bool jstr(const char *from, const char *to, const char *field, char *out, int max)
{
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\":\"", field);
    const char *p = strstr(from, pat);
    if (!p || p >= to) return false;
    p += strlen(pat);
    int k = 0;
    while (*p && *p != '"' && p < to) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'u') {
                unsigned cp = (unsigned)strtoul((char[5]){ p[1], p[2], p[3], p[4], 0 }, NULL, 16);
                put_utf8(out, &k, max, cp);
                p += 5;
                continue;
            }
            char c = *p == 'n' ? ' ' : *p == 't' ? ' ' : *p;
            if (k < max - 1) out[k++] = c;
            p++;
            continue;
        }
        if (k < max - 1) out[k++] = *p;
        p++;
    }
    out[k] = 0;
    return k > 0;
}

int mp_search_photon(const char *json, mp_hit_t *out, int n, int max)
{
    int added = 0;
    const char *p = json;
    while ((p = strstr(p, "\"type\":\"Feature\"")) && n < max) {
        const char *next = strstr(p + 16, "\"type\":\"Feature\"");
        const char *end = next ? next : p + strlen(p);
        mp_hit_t h;
        memset(&h, 0, sizeof h);
        char street[40] = "", num[12] = "", town[40] = "", val[24] = "";
        bool has_name = jstr(p, end, "name", h.name, sizeof h.name);
        jstr(p, end, "street", street, sizeof street);
        jstr(p, end, "housenumber", num, sizeof num);
        if (!jstr(p, end, "locality", town, sizeof town) && !jstr(p, end, "district", town, sizeof town))
            jstr(p, end, "city", town, sizeof town);
        jstr(p, end, "osm_value", val, sizeof val);
        const char *c = strstr(p, "\"coordinates\":[");
        if (c && c < end) {
            c += 15;
            float lon = strtof(c, (char **)&c);
            if (*c == ',') {
                float lat = strtof(c + 1, NULL);
                h.lat = (int32_t)(lat * 1e6f);
                h.lon = (int32_t)(lon * 1e6f);
                if (!has_name && street[0]) {
                    snprintf(h.name, sizeof h.name, "%s%s%s", street, num[0] ? " " : "", num);
                    h.kind = MP_KIND_ADDRESS;
                } else {
                    h.kind = !strcmp(val, "city") || !strcmp(val, "town") || !strcmp(val, "village") ||
                             !strcmp(val, "suburb") || !strcmp(val, "neighbourhood")
                                 ? MP_KIND_PLACE
                                 : MP_KIND_POI;
                }
                if (h.name[0]) {
                    snprintf(h.sub, sizeof h.sub, "%s", town[0] ? town : val);
                    h.score = 1000 + added;
                    bool dup = false;
                    for (int i = 0; i < n; i++)
                        if (!strcmp(out[i].name, h.name) && labs((long)(out[i].lat - h.lat)) < 2000 &&
                            labs((long)(out[i].lon - h.lon)) < 2000)
                            dup = true;
                    if (!dup) {
                        out[n++] = h;
                        added++;
                    }
                }
            }
        }
        p = end;
        if (!next) break;
    }
    return added;
}

float mp_search_zoom(const mp_hit_t *h)
{
    switch (h->kind) {
    case MP_KIND_PLACE:   return 14.0f;
    case MP_KIND_STREET:  return 16.0f;
    case MP_KIND_WATER:   return 13.0f;
    default:              return 17.0f;
    }
}
