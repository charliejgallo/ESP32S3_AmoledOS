/*
 * MILA - the level solver (Mac only), on the game's own rules (main/ml_rules.c)
 *
 *   cc -O2 -I../main -o /tmp/ml_solve solve.c ../main/ml_rules.c
 *   /tmp/ml_solve [-l STATES] < levels.txt
 *
 * Levels on stdin, separated by blank lines. One line out per level:
 *   ok <moves> <pushes> <states> <use> <solution>  (u r d l walk, U R D L push)
 * <use> counts the mechanics along the solution: slides.falls.gates.flaps.rolls.switches
 * (switches: how often the pushed thing changes, a measure of how tangled it is)
 *   fail <reason> <states>
 * Breadth-first over single steps, so <moves> is the minimum (the par).
 * States are canonical (objects are interchangeable, balls too), and a
 * position where an object is stuck in a corner off target with no spare
 * thing left is cut.
 */
#include "ml_rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t k[12];
} skey_t;

static skey_t   *keys;
static uint32_t *par;
static uint8_t  *mv;
static uint32_t  n, cap;
static uint32_t *ht;
static uint32_t  htmask;

static void pack(const ml_state_t *s, skey_t *k)
{
    memset(k, 0, sizeof *k);
    k->k[0] = s->mila;
    memcpy(k->k + 1, s->pos, ML_MAXT);
    k->k[9] = (uint8_t)(s->filled & 255);
    k->k[10] = (uint8_t)(s->filled >> 8);
}

static void unpack(const skey_t *k, ml_state_t *s)
{
    s->mila = k->k[0];
    memcpy(s->pos, k->k + 1, ML_MAXT);
    s->filled = (uint16_t)(k->k[9] | k->k[10] << 8);
}

static uint32_t hash(const skey_t *k)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 11; i++) h = (h ^ k->k[i]) * 16777619u;
    return h;
}

/* index of k, inserting it; *fresh tells which */
static int insert(const skey_t *k, uint32_t parent, uint8_t move, int *fresh)
{
    uint32_t h = hash(k) & htmask;
    while (ht[h] != 0xFFFFFFFFu) {
        if (!memcmp(keys[ht[h]].k, k->k, 11)) {
            *fresh = 0;
            return (int)ht[h];
        }
        h = (h + 1) & htmask;
    }
    if (n >= cap) return -1;
    keys[n] = *k;
    par[n] = parent;
    mv[n] = move;
    ht[h] = n;
    *fresh = 1;
    return (int)n++;
}

/* permanent blocks for an object: never moves again in that axis */
static int solid(const ml_map_t *m, int c)
{
    if (c < 0) return 1;
    int t = C_TERR(m->cell[c]);
    return t == T_WALL || t == T_VOID || t == T_FLAP_V || t == T_FLAP_H;
}

static uint8_t corner[ML_CELLS];

static void find_corners(const ml_map_t *m)
{
    for (int c = 0; c < ML_CELLS; c++) {
        corner[c] = 0;
        if (m->cell[c] & C_TARGET) continue;
        int t = C_TERR(m->cell[c]);
        if (t == T_WALL || t == T_VOID || t == T_HOLE) continue;
        int v = solid(m, ml_next(m, c, D_UP)) || solid(m, ml_next(m, c, D_DOWN));
        int h = solid(m, ml_next(m, c, D_LEFT)) || solid(m, ml_next(m, c, D_RIGHT));
        corner[c] = (uint8_t)(v && h);
    }
}

static int dead(const ml_map_t *m, const ml_state_t *s)
{
    int alive = 0, stuck = 0;
    for (int i = 0; i < m->nthings; i++) {
        if (s->pos[i] == ML_GONE) continue;
        alive++;
        if (m->kind[i] == K_OBJECT && corner[s->pos[i]]) stuck++;
    }
    return alive - stuck < m->ntargets;
}

static int closed(const ml_map_t *m, const ml_state_t *s)
{
    uint8_t seen[ML_CELLS] = {0};
    int stack[ML_CELLS], sp = 0;
    stack[sp++] = s->mila;
    seen[s->mila] = 1;
    while (sp) {
        int c = stack[--sp];
        for (int d = 0; d < 4; d++) {
            int q = ml_next(m, c, d);
            if (q < 0) return 0;
            int t = C_TERR(m->cell[q]);
            if (t == T_VOID) return 0;
            if (t == T_WALL || seen[q]) continue;
            seen[q] = 1;
            stack[sp++] = q;
        }
    }
    return 1;
}

static void solve(const char *text, uint32_t limit)
{
    ml_map_t m;
    ml_state_t s0;
    char err[80];
    if (ml_parse(text, &m, &s0, err, sizeof err)) {
        printf("fail parse:%s 0\n", err);
        return;
    }
    if (!closed(&m, &s0)) {
        printf("fail open 0\n");
        return;
    }
    find_corners(&m);
    if (cap < limit) {
        free(keys); free(par); free(mv); free(ht);
        cap = limit;
        keys = malloc(sizeof *keys * cap);
        par = malloc(sizeof *par * cap);
        mv = malloc(cap);
        uint32_t hs = 1;
        while (hs < cap * 2) hs <<= 1;
        ht = malloc(sizeof *ht * hs);
        htmask = hs - 1;
        if (!keys || !par || !mv || !ht) {
            printf("fail memory 0\n");
            exit(1);
        }
    }
    memset(ht, 0xFF, sizeof *ht * (htmask + 1));
    n = 0;
    ml_state_t s = s0;
    ml_canon(&m, &s);
    skey_t k;
    pack(&s, &k);
    int fresh;
    insert(&k, 0xFFFFFFFFu, 0, &fresh);
    for (uint32_t i = 0; i < n; i++) {
        ml_state_t cur;
        unpack(&keys[i], &cur);
        if (ml_won(&m, &cur)) {
            char path[4096];
            int len = 0, pushes = 0;
            for (uint32_t j = i; par[j] != 0xFFFFFFFFu; j = par[j]) {
                if (len < (int)sizeof path - 1) path[len++] = (char)mv[j];
                if (mv[j] < 'a') pushes++;
            }
            for (int a = 0, b = len - 1; a < b; a++, b--) {
                char t = path[a];
                path[a] = path[b];
                path[b] = t;
            }
            path[len] = 0;
            /* replay it (with identities) and count the mechanics used */
            int slides = 0, falls = 0, gates = 0, flaps = 0, rolls = 0, sw = 0, last = -1;
            ml_state_t r = s0;
            for (int j = 0; j < len; j++) {
                int d = (int)(strchr("urdl", path[j] | 32) - "urdl");
                ml_step_t st;
                ml_step(&m, &r, d, &st);
                int tt = C_TERR(m.cell[r.mila]);
                if (tt == T_GATE && m.nplates) gates++;
                if (tt == T_FLAP_V || tt == T_FLAP_H) flaps++;
                if (st.thing >= 0) {
                    if (st.fell) falls++;
                    if (st.cells > 1) {
                        if (m.kind[st.thing] == K_BALL) rolls++;
                        else slides++;
                    }
                    if (st.to != ML_GONE && C_TERR(m.cell[st.to]) == T_GATE) gates++;
                    if (st.thing != last) sw++;
                    last = st.thing;
                }
            }
            printf("ok %d %d %u %d.%d.%d.%d.%d.%d %s\n", len, pushes, n,
                   slides, falls, gates, flaps, rolls, sw, path);
            return;
        }
        for (int d = 0; d < 4; d++) {
            ml_state_t nx = cur;
            ml_step_t st;
            if (!ml_step(&m, &nx, d, &st)) continue;
            ml_canon(&m, &nx);
            if (st.thing >= 0 && dead(&m, &nx)) continue;
            pack(&nx, &k);
            char c = "urdl"[d];
            if (st.thing >= 0) c = (char)(c - 32);
            if (insert(&k, i, (uint8_t)c, &fresh) < 0) {
                printf("fail limit %u\n", n);
                return;
            }
        }
    }
    printf("fail unsolvable %u\n", n);
}

int main(int argc, char **argv)
{
    uint32_t limit = 4000000;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-l") && i + 1 < argc) limit = (uint32_t)strtoul(argv[++i], NULL, 10);
    static char buf[1 << 20];
    size_t len = fread(buf, 1, sizeof buf - 1, stdin);
    buf[len] = 0;
    char *p = buf;
    for (;;) {
        while (*p == '\n' || *p == '\r') p++;
        if (!*p) break;
        char *e = strstr(p, "\n\n");
        if (e) *e = 0;
        /* comment lines (;) are skipped */
        char lvl[4096];
        int ln = 0;
        for (char *q = p; *q && ln < (int)sizeof lvl - 2;) {
            char *eol = strchr(q, '\n');
            size_t l = eol ? (size_t)(eol - q) : strlen(q);
            if (q[0] != ';') {
                memcpy(lvl + ln, q, l);
                ln += (int)l;
                lvl[ln++] = '\n';
            }
            q = eol ? eol + 1 : q + l;
        }
        lvl[ln] = 0;
        if (ln) solve(lvl, limit);
        if (!e) break;
        p = e + 2;
    }
    fflush(stdout);
    return 0;
}
