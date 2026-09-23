/*
 * MILA - the worlds table and the levels (see ml_level.h)
 */
#include "ml_level.h"
#include "ml_art.h"
#include "ml_gfx.h"

#include "aos_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s)
{
    size_t n = strlen(s);
    char *d = (char *)ml_malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static void copy_word(char *dst, int n, const char *src)
{
    int i = 0;
    while (src[i] && src[i] != ' ' && src[i] != '\n' && src[i] != '\r' && i < n - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

bool ml_worlds_load(ml_worlds_t *t)
{
    memset(t, 0, sizeof(*t));
    uint32_t len = 0;
    char *b = (char *)ml_art_blob("worlds", &len);
    if (!b) return false;
    ml_world_info_t *w = NULL;
    char *p = b, *end = b + len;
    while (p < end && *p) {
        char *eol = p;
        while (eol < end && *eol && *eol != '\n') eol++;
        char save = *eol;
        *eol = 0;
        if (!strncmp(p, "world ", 6) && t->nworlds < ML_MAX_WORLDS) {
            w = &t->w[t->nworlds++];
            copy_word(w->id, sizeof w->id, p + 6);
        } else if (w && !strncmp(p, "name ", 5)) {
            free(w->name);
            w->name = dup_str(p + 5);
        } else if (w && !strncmp(p, "kit ", 4)) {
            copy_word(w->kit, sizeof w->kit, p + 4);
        } else if (w && !strncmp(p, "panel ", 6)) {
            copy_word(w->panel, sizeof w->panel, p + 6);
        } else if (w && !strncmp(p, "gift ", 5)) {
            copy_word(w->gift, sizeof w->gift, p + 5);
            if (!strcmp(w->gift, "-")) w->gift[0] = 0;
        } else if (w && !strncmp(p, "need ", 5)) {
            w->need = atoi(p + 5);
        } else if (w && !strncmp(p, "music ", 6)) {
            w->music = atoi(p + 6);
        } else if (w && !strncmp(p, "mech ", 5)) {
            w->mech = (uint32_t)strtoul(p + 5, NULL, 10);
        } else if (w && !strncmp(p, "level ", 6) && w->nlevels < ML_MAX_LEVELS) {
            ml_level_ref_t *l = &w->lv[w->nlevels++];
            copy_word(l->id, sizeof l->id, p + 6);
            const char *sp = strchr(p + 6, ' ');
            l->title = dup_str(sp ? sp + 1 : "");
        } else if (!strncmp(p, "end", 3)) {
            w = NULL;
        }
        *eol = save;
        if (eol >= end || !*eol) break;
        p = eol + 1;
    }
    free(b);
    return t->nworlds > 0;
}

void ml_worlds_free(ml_worlds_t *t)
{
    for (int i = 0; i < t->nworlds; i++) {
        free(t->w[i].name);
        for (int k = 0; k < t->w[i].nlevels; k++) free(t->w[i].lv[k].title);
    }
    memset(t, 0, sizeof(*t));
}

int ml_worlds_find(const ml_worlds_t *t, const char *id)
{
    for (int i = 0; i < t->nworlds; i++)
        if (!strcmp(t->w[i].id, id)) return i;
    return -1;
}

const char *ml_pick_lang(const char *multi, char *buf, int n)
{
    buf[0] = 0;
    if (!multi) return buf;
    const char *cur = aos_i18n_current();
    char want[8];
    snprintf(want, sizeof want, "%s=", cur && cur[0] ? cur : "es");
    const char *hit = NULL, *es = NULL;
    for (const char *p = multi; *p;) {
        if (!strncmp(p, want, strlen(want))) hit = p + strlen(want);
        if (!strncmp(p, "es=", 3)) es = p + 3;
        const char *bar = strchr(p, '|');
        if (!bar) break;
        p = bar + 1;
    }
    const char *s = hit ? hit : es ? es : multi;
    int i = 0;
    while (s[i] && s[i] != '|' && i < n - 1) {
        buf[i] = s[i];
        i++;
    }
    buf[i] = 0;
    return buf;
}

/* ---- furniture ---- */

static bool floorish(const ml_map_t *m, int x, int y)
{
    if (x < 0 || y < 0 || x >= m->w || y >= m->h) return false;
    int t = C_TERR(m->cell[y * ML_MAXW + x]);
    return t != T_WALL && t != T_VOID && t != T_FLAP_V && t != T_FLAP_H;
}

static int n_floor4(const ml_map_t *m, int x, int y)
{
    return floorish(m, x + 1, y) + floorish(m, x - 1, y) + floorish(m, x, y + 1) + floorish(m, x, y - 1);
}

static uint32_t hash2(int x, int y, uint32_t salt)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ salt;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

static void furnish(ml_level_t *l, const char *given, int n1, int n2, uint32_t salt)
{
    const ml_map_t *m = &l->map;
    memset(l->deco, DECO_HIDDEN, sizeof l->deco);
    bool inner[ML_CELLS] = { false };
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            int c = y * ML_MAXW + x;
            if (C_TERR(m->cell[c]) != T_WALL) continue;
            bool seen = false;
            for (int dy = -1; dy <= 1 && !seen; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (floorish(m, x + dx, y + dy)) seen = true;
            if (!seen) continue;
            l->deco[c] = DECO_WALL;
            inner[c] = n1 > 0 && n_floor4(m, x, y) >= 3;
        }
    }
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x < m->w; x++) {
            int c = y * ML_MAXW + x;
            if (!inner[c] || l->deco[c] != DECO_WALL) continue;
            if (n2 > 0 && x + 1 < m->w && inner[c + 1]) {
                l->deco[c] = (uint8_t)(64 + hash2(x, y, salt) % (uint32_t)n2);
                l->deco[c + 1] = DECO_NONE;
                continue;
            }
            l->deco[c] = (uint8_t)(1 + hash2(x, y, salt) % (uint32_t)n1);
        }
    }
    /* what the level says: "x,y:k x,y:k" */
    for (const char *p = given; p && *p;) {
        int x, y, k, used = 0;
        if (sscanf(p, "%d,%d:%d%n", &x, &y, &k, &used) == 3 && x >= 0 && y >= 0 && x < ML_MAXW && y < ML_MAXH) {
            l->deco[y * ML_MAXW + x] = (uint8_t)k;
            if (k >= 64 && k < 128 && x + 1 < ML_MAXW) l->deco[y * ML_MAXW + x + 1] = DECO_NONE;
        }
        if (!used) break;
        p += used;
        while (*p == ' ') p++;
    }
}

bool ml_level_load(ml_level_t *l, const char *id, int n1, int n2)
{
    memset(l, 0, sizeof(*l));
    char nm[40];
    snprintf(nm, sizeof nm, "lvl_%s", id);
    uint32_t len = 0;
    uint8_t *b = ml_art_blob(nm, &len);
    if (!b || len < 5) {
        free(b);
        return false;
    }
    l->par = b[0] | (b[1] << 8);
    l->par_pushes = b[2] | (b[3] << 8);
    const char *text = (const char *)b + 4;
    size_t tl = strnlen(text, len - 4);
    const char *deco = (tl + 5 < len) ? text + tl + 1 : "";
    char err[64];
    bool ok = ml_parse(text, &l->map, &l->start, err, sizeof err) == 0;
    if (ok) {
        uint32_t salt = 0;
        for (const char *p = id; *p; p++) salt = salt * 31u + (uint8_t)*p;
        furnish(l, deco, n1, n2, salt);
    }
    free(b);
    return ok;
}
