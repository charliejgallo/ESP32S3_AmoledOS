/*
 * MILA - the rules (see ml_rules.h and DESIGN.md section 1)
 */
#include "ml_rules.h"

#include <stdio.h>
#include <string.h>

int ml_next(const ml_map_t *m, int c, int d)
{
    int x = ml_cx(c) + ml_dx(d), y = ml_cy(c) + ml_dy(d);
    if (x < 0 || y < 0 || x >= m->w || y >= m->h) return -1;
    return y * ML_MAXW + x;
}

int ml_thing_at(const ml_map_t *m, const ml_state_t *s, int c)
{
    for (int i = 0; i < m->nthings; i++)
        if (s->pos[i] == c) return i;
    return -1;
}

int ml_terrain(const ml_map_t *m, const ml_state_t *s, int c)
{
    int t = C_TERR(m->cell[c]);
    if (t == T_HOLE && (s->filled >> m->hole[c] & 1)) return T_FLOOR;
    return t;
}

bool ml_plates_down(const ml_map_t *m, const ml_state_t *s)
{
    if (!m->nplates) return true;
    for (int c = 0; c < ML_CELLS; c++) {
        if (C_TERR(m->cell[c]) != T_PLATE) continue;
        if (s->mila != c && ml_thing_at(m, s, c) < 0) return false;
    }
    return true;
}

bool ml_gate_open(const ml_map_t *m, const ml_state_t *s, int c)
{
    if (s->mila == c || ml_thing_at(m, s, c) >= 0) return true;    /* never closes on anyone */
    return ml_plates_down(m, s);
}

bool ml_mila_can(const ml_map_t *m, const ml_state_t *s, int from, int c, int d)
{
    int tf = ml_terrain(m, s, from);
    bool vert = (d == D_UP || d == D_DOWN);
    if (tf == T_FLAP_V && !vert) return false;
    if (tf == T_FLAP_H && vert) return false;
    switch (ml_terrain(m, s, c)) {
    case T_FLOOR: case T_WET: case T_PLATE: return true;
    case T_GATE:   return ml_gate_open(m, s, c);
    case T_FLAP_V: return vert;
    case T_FLAP_H: return !vert;
    default:       return false;
    }
}

/* can a thing move onto c (the hole case is handled by the caller) */
static bool thing_can(const ml_map_t *m, const ml_state_t *s, int c)
{
    if (c < 0 || s->mila == c || ml_thing_at(m, s, c) >= 0) return false;
    switch (ml_terrain(m, s, c)) {
    case T_FLOOR: case T_WET: case T_PLATE: case T_HOLE: return true;
    case T_GATE: return ml_gate_open(m, s, c);
    default:     return false;
    }
}

bool ml_step(const ml_map_t *m, ml_state_t *st, int d, ml_step_t *out)
{
    ml_step_t o = { .moved = false, .thing = -1 };
    int p = st->mila;
    int q = ml_next(m, p, d);
    if (q < 0 || !ml_mila_can(m, st, p, q, d)) {
        if (out) *out = o;
        return false;
    }
    int t = ml_thing_at(m, st, q);
    if (t >= 0) {
        /* a flap cell never holds a thing, so Mila pushes from anywhere */
        int r = ml_next(m, q, d);
        if (!thing_can(m, st, r)) {
            if (out) *out = o;
            return false;
        }
        ml_state_t s = *st;
        int cur = r, cells = 1;
        s.pos[t] = (uint8_t)cur;
        bool fell = false;
        for (;;) {
            if (ml_terrain(m, &s, cur) == T_HOLE) {
                s.filled |= (uint16_t)(1u << m->hole[cur]);
                s.pos[t] = ML_GONE;
                fell = true;
                break;
            }
            bool on = (m->kind[t] == K_BALL) || ml_terrain(m, &s, cur) == T_WET;
            if (!on) break;
            int nx = ml_next(m, cur, d);
            if (!thing_can(m, &s, nx)) break;
            cur = nx;
            cells++;
            s.pos[t] = (uint8_t)cur;
        }
        s.mila = (uint8_t)q;
        *st = s;
        o.thing = (int8_t)t;
        o.from = (uint8_t)q;
        o.to = (uint8_t)cur;
        o.cells = (uint8_t)cells;
        o.fell = fell;
    } else {
        st->mila = (uint8_t)q;
    }
    o.moved = true;
    if (out) *out = o;
    return true;
}

bool ml_won(const ml_map_t *m, const ml_state_t *s)
{
    if (!m->ntargets) return false;
    for (int c = 0; c < ML_CELLS; c++)
        if ((m->cell[c] & C_TARGET) && ml_thing_at(m, s, c) < 0) return false;
    return true;
}

static void sort_range(uint8_t *a, int n)
{
    for (int i = 1; i < n; i++) {
        uint8_t v = a[i];
        int j = i - 1;
        while (j >= 0 && a[j] > v) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = v;
    }
}

void ml_canon(const ml_map_t *m, ml_state_t *s)
{
    int nobj = 0;
    while (nobj < m->nthings && m->kind[nobj] == K_OBJECT) nobj++;
    sort_range(s->pos, nobj);
    sort_range(s->pos + nobj, m->nthings - nobj);
}

int ml_parse(const char *text, ml_map_t *m, ml_state_t *st, char *err, int errn)
{
    memset(m, 0, sizeof *m);
    memset(st, 0, sizeof *st);
    memset(m->hole, 255, sizeof m->hole);
    uint8_t objs[ML_MAXT], balls[ML_MAXT];
    int nobj = 0, nball = 0, mila = -1;
    int x = 0, y = 0, w = 0;
    /* skip leading blank lines */
    while (*text == '\n' || *text == '\r') text++;
    for (const char *p = text;; p++) {
        char ch = *p;
        if (ch == '\r') continue;
        if (ch == '\n' || ch == 0) {
            if (x > w) w = x;
            if (x > 0 || ch == '\n') y++;
            x = 0;
            if (ch == 0) break;
            if (p[1] == '\n' || p[1] == 0) break;      /* a blank line ends it */
            continue;
        }
        if (x >= ML_MAXW || y >= ML_MAXH) {
            snprintf(err, errn, "bigger than %dx%d", ML_MAXW, ML_MAXH);
            return -1;
        }
        int c = y * ML_MAXW + x, t = T_FLOOR, flags = 0;
        switch (ch) {
        case '-': t = T_VOID; break;
        case ' ': break;
        case '#': t = T_WALL; break;
        case '.': flags = C_TARGET; break;
        case '$': if (nobj < ML_MAXT) objs[nobj] = (uint8_t)c; nobj++; break;
        case '*': flags = C_TARGET; if (nobj < ML_MAXT) objs[nobj] = (uint8_t)c; nobj++; break;
        case '@': mila = c; break;
        case '+': flags = C_TARGET; mila = c; break;
        case '~': t = T_WET; break;
        case '_': t = T_PLATE; break;
        case 'G': t = T_GATE; break;
        case 'v': t = T_FLAP_V; break;
        case 'h': t = T_FLAP_H; break;
        case 'o': t = T_HOLE; break;
        case 'b': if (nball < ML_MAXT) balls[nball] = (uint8_t)c; nball++; break;
        case 'B': flags = C_TARGET; if (nball < ML_MAXT) balls[nball] = (uint8_t)c; nball++; break;
        default:
            snprintf(err, errn, "unknown '%c' at %d,%d", ch, x, y);
            return -1;
        }
        m->cell[c] = (uint8_t)(t | flags);
        if (t == T_HOLE) {
            if (m->nholes >= ML_MAXHOLES) {
                snprintf(err, errn, "more than %d holes", ML_MAXHOLES);
                return -1;
            }
            m->hole[c] = m->nholes++;
        }
        if (t == T_PLATE) m->nplates++;
        if (flags & C_TARGET) m->ntargets++;
        x++;
    }
    m->w = (uint8_t)w;
    m->h = (uint8_t)y;
    if (mila < 0) {
        snprintf(err, errn, "no Mila");
        return -1;
    }
    if (nobj + nball > ML_MAXT) {
        snprintf(err, errn, "more than %d things", ML_MAXT);
        return -1;
    }
    if (nobj + nball < m->ntargets) {
        snprintf(err, errn, "%d things for %d targets", nobj + nball, m->ntargets);
        return -1;
    }
    m->nthings = (uint8_t)(nobj + nball);
    for (int i = 0; i < nobj; i++) {
        m->kind[i] = K_OBJECT;
        st->pos[i] = objs[i];
    }
    for (int i = 0; i < nball; i++) {
        m->kind[nobj + i] = K_BALL;
        st->pos[nobj + i] = balls[i];
    }
    for (int i = m->nthings; i < ML_MAXT; i++) st->pos[i] = ML_GONE;
    st->mila = (uint8_t)mila;
    return 0;
}
