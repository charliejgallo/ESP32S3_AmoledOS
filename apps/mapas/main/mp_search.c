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
    snprintf(h.name, sizeof h.name, "%s", f[1]);
    snprintf(h.sub, sizeof h.sub, "%s", f[2]);
    h.kind = (uint8_t)kind_of(f[2]);
    h.lat = (int32_t)strtol(f[3], NULL, 10);
    h.lon = (int32_t)strtol(f[4], NULL, 10);
    static const int KB[] = { 0, 10, 30, 15, 5 };
    h.score = m + KB[h.kind] + (int)strlen(f[0]) / 4;
    keep(out, n, max, &h);
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
                line(p, key, out, &n, max);
                p = nl + 1;
            }
            have = (int)(buf + len - p);
            if (r <= 0) {
                if (have > 0) line(p, key, out, &n, max);
                break;
            }
            if (have >= CH - 1) have = 0;           /* a line longer than a chunk: drop it */
            memmove(buf, p, (size_t)have);
            if (*cancel) break;
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
