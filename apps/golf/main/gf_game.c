/*
 * GOLF - the rules (see gf_game.h)
 */
#include "gf_game.h"

#include <math.h>
#include <string.h>

#ifndef N_
#define N_(s) s
#endif

/* proper names, not translated */
static const char *const gf_rival_names[] = {
    "Tomás Rueda", "Paula Crema", "Ángel Mármol", "Jazmín Vidal", "Rori Mack",
    "Nina Cordero", "Beto Sevilla", "Lidia Koval", "Vicky Garro", "Cacho Cabrera",
};
#define RIVALS (int)(sizeof(gf_rival_names) / sizeof(gf_rival_names[0]))

const char *gf_rival_name(int i)
{
    return gf_rival_names[i % RIVALS];
}

int gf_rival_n(void)
{
    return RIVALS;
}

static uint32_t rnd(gf_game_t *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

static float rndf(gf_game_t *g)
{
    return (float)(rnd(g) & 0xFFFFFF) / 16777215.0f;
}

void gf_game_new(gf_game_t *g, int mode, int diff, int nplayers, uint32_t seed, int practice_hole)
{
    memset(g, 0, sizeof(*g));
    g->mode = (uint8_t)mode;
    g->diff = (uint8_t)diff;
    g->seed = seed ? seed : 1;
    g->rng = g->seed * 2654435761U + 1;
    if (g->rng == 0) g->rng = 1;
    g->nplayers = (uint8_t)(nplayers < 1 ? 1 : (nplayers > GF_MAX_PLAYERS ? GF_MAX_PLAYERS : nplayers));
    int total = gf_course()->nholes;
    /* the tees move back with the difficulty */
    g->tee_i = (uint8_t)(diff == DIFF_EASY ? 2 : (diff == DIFF_NORMAL ? 1 : 0));

    switch (mode) {
    case MODE_QUICK: {
        /* three different holes at random, in course order */
        g->nholes = 3;
        uint8_t pick[GF_MAX_HOLES];
        for (int i = 0; i < total; i++) pick[i] = (uint8_t)i;
        for (int i = total - 1; i > 0; i--) {
            int j = (int)(rnd(g) % (uint32_t)(i + 1));
            uint8_t t = pick[i]; pick[i] = pick[j]; pick[j] = t;
        }
        for (int i = 0; i < 3; i++) g->holes[i] = pick[i];
        for (int i = 0; i < 3; i++)
            for (int j = i + 1; j < 3; j++)
                if (g->holes[j] < g->holes[i]) { uint8_t t = g->holes[i]; g->holes[i] = g->holes[j]; g->holes[j] = t; }
        break;
    }
    case MODE_PRACTICE:
        g->nholes = 1;
        g->holes[0] = (uint8_t)(practice_hole >= 0 && practice_hole < total ? practice_hole : 0);
        break;
    default:
        g->nholes = (uint8_t)total;
        for (int i = 0; i < total; i++) g->holes[i] = (uint8_t)i;
        break;
    }

    if (mode == MODE_TOUR) {
        g->nrivals = GF_RIVALS;
        int base = diff == DIFF_EASY ? 35 : (diff == DIFF_NORMAL ? 60 : 82);
        int first = (int)(rnd(g) % (uint32_t)RIVALS);
        for (int r = 0; r < GF_RIVALS; r++) {
            g->rival[r].skill = (uint8_t)(base + (int)(rnd(g) % 16) - 6);
            g->rival[r].name = (uint8_t)((first + r * 3) % RIVALS);
        }
    }
}

int gf_game_hole(const gf_game_t *g)
{
    return g->holes[g->hi];
}

void gf_game_hole_start(gf_game_t *g, gf_world_t *w)
{
    const gf_hole_t *h = &gf_course()->holes[gf_game_hole(g)];
    g->pin_i = (uint8_t)((g->seed >> (g->hi * 2)) % 3);
    gf_world_load(w, h, g->tee_i, g->pin_i);

    /* the wind: stronger with the difficulty */
    float maxw = g->diff == DIFF_EASY ? 3.0f : (g->diff == DIFF_NORMAL ? 6.0f : 9.0f);
    maxw *= (float)gf_course()->wind10 * 0.1f;         /* the coast blows */
    float sp = maxw * (0.15f + 0.85f * rndf(g));
    float ang = rndf(g) * 6.2831853f;
    g->wind_x = sp * sinf(ang);
    g->wind_y = sp * cosf(ang);

    for (int i = 0; i < g->nplayers; i++) {
        gf_player_t *p = &g->pl[i];
        p->cur = 0;
        p->done = 0;
        p->picked = 0;
        p->x = p->px = w->tee_x;
        p->y = p->py = w->tee_y;
        p->lie = LIE_TEE;
    }
    g->turn = 0;
}

float gf_game_dist_to_pin(const gf_world_t *w, float x, float y)
{
    float dx = w->pin_x - x, dy = w->pin_y - y;
    return sqrtf(dx * dx + dy * dy);
}

int gf_game_next(gf_game_t *g, const gf_world_t *w)
{
    /* on the tee: in order */
    for (int i = 0; i < g->nplayers; i++) {
        if (!g->pl[i].done && g->pl[i].cur == 0) {
            g->turn = (uint8_t)i;
            return i;
        }
    }
    int best = -1;
    float bd = -1;
    for (int i = 0; i < g->nplayers; i++) {
        if (g->pl[i].done) continue;
        float d = gf_game_dist_to_pin(w, g->pl[i].x, g->pl[i].y);
        if (d > bd + 0.01f) {
            bd = d;
            best = i;
        }
    }
    if (best >= 0) g->turn = (uint8_t)best;
    return best;
}

void gf_game_apply(gf_game_t *g, const gf_world_t *w, const gf_shot_t *s)
{
    gf_player_t *p = &g->pl[g->turn];
    int par = w->def->par;
    p->cur++;
    p->px = s->x0;
    p->py = s->y0;
    switch (s->result) {
    case RES_HOLED:
        p->x = w->pin_x;
        p->y = w->pin_y;
        p->done = 1;
        break;
    case RES_WATER:
        p->cur++;
        p->x = s->drop_x;
        p->y = s->drop_y;
        p->lie = (uint8_t)gf_lie(w, p->x, p->y);
        break;
    case RES_OB:
        p->cur++;
        /* again from where it was hit */
        p->x = s->x0;
        p->y = s->y0;
        p->lie = (uint8_t)s->lie0;
        break;
    default:
        p->x = s->x;
        p->y = s->y;
        p->lie = (uint8_t)s->lie;
        break;
    }
    if (!p->done && p->cur >= par * 3) {
        p->done = 1;
        p->picked = 1;
    }
    if (p->done) {
        p->strokes[g->hi] = p->cur;
    }
}

bool gf_game_hole_over(const gf_game_t *g)
{
    for (int i = 0; i < g->nplayers; i++) {
        if (!g->pl[i].done) return false;
    }
    return true;
}

/* A rival's score on a hole: par plus a spread that tightens with skill. */
static int rival_score(gf_game_t *g, const gf_rival_t *r, int par)
{
    float sk = (float)r->skill / 100.0f;
    float u = rndf(g);
    /* probabilities of eagle, birdie, par, bogey, double */
    float pe = 0.01f + 0.03f * sk * sk * (par == 5 ? 3.0f : 1.0f);
    float pb = 0.06f + 0.26f * sk;
    float pp = 0.30f + 0.28f * sk;
    float pbo = 0.30f - 0.18f * sk;
    if (u < pe) return par - 2;
    u -= pe;
    if (u < pb) return par - 1;
    u -= pb;
    if (u < pp) return par;
    u -= pp;
    if (u < pbo) return par + 1;
    u -= pbo;
    return par + 2 + (u > 0.25f ? 0 : 1);
}

bool gf_game_next_hole(gf_game_t *g)
{
    int par = gf_course()->holes[gf_game_hole(g)].par;
    for (int r = 0; r < g->nrivals; r++) {
        g->rival[r].strokes[g->hi] = (uint8_t)rival_score(g, &g->rival[r], par);
    }
    if (g->hi + 1 >= g->nholes) {
        return false;
    }
    g->hi++;
    return true;
}

int gf_game_total(const gf_game_t *g, int player)
{
    int t = 0;
    for (int i = 0; i < g->nholes; i++) t += g->pl[player].strokes[i];
    return t;
}

int gf_game_par_so_far(const gf_game_t *g, int upto)
{
    int t = 0;
    for (int i = 0; i <= upto && i < g->nholes; i++) t += gf_course()->holes[g->holes[i]].par;
    return t;
}

int gf_game_to_par(const gf_game_t *g, int player)
{
    int t = 0;
    for (int i = 0; i < g->nholes; i++) {
        if (g->pl[player].strokes[i]) {
            t += g->pl[player].strokes[i] - gf_course()->holes[g->holes[i]].par;
        }
    }
    return t;
}

int gf_game_rival_total(const gf_game_t *g, int r)
{
    int t = 0;
    for (int i = 0; i < g->nholes; i++) t += g->rival[r].strokes[i];
    return t;
}

int gf_game_place(const gf_game_t *g)
{
    int me = gf_game_total(g, 0), place = 1;
    for (int r = 0; r < g->nrivals; r++) {
        if (gf_game_rival_total(g, r) < me) place++;
    }
    return place;
}

int gf_game_coins(const gf_game_t *g)
{
    if (g->mode == MODE_PRACTICE) return 0;
    int c = 0;
    for (int i = 0; i < g->nholes; i++) {
        int s = g->pl[0].strokes[i];
        if (!s) continue;
        int par = gf_course()->holes[g->holes[i]].par;
        c += 10;
        if (s == 1) c += 150;
        else if (s <= par - 2) c += 60;
        else if (s == par - 1) c += 25;
        else if (s == par) c += 10;
    }
    if (g->mode == MODE_TOUR && g->hi + 1 >= g->nholes) {
        int place = gf_game_place(g);
        c += place == 1 ? 200 : (place == 2 ? 100 : (place == 3 ? 50 : 0));
    }
    if (g->diff == DIFF_NORMAL) c = c * 3 / 2;
    if (g->diff == DIFF_PRO) c = c * 2;
    return c;
}

int gf_game_suggest_club(float dist, int lie, bool on_green)
{
    if (on_green) return CLUB_PT;
    if (lie == LIE_FRINGE && dist < 12.0f) return CLUB_PT;
    /* the shortest club whose full carry reaches, from this lie */
    for (int c = CLUB_LW; c >= CLUB_DR; c--) {
        if (c == CLUB_DR && lie != LIE_TEE) continue;
        if (gf_phys_carry(c, 1.0f, lie) >= dist * 0.97f) return c;
    }
    return lie == LIE_TEE ? CLUB_DR : CLUB_3W;
}

const char *gf_score_name(int strokes, int par)
{
    if (strokes == 1) return N_("¡Hoyo en uno!");
    switch (strokes - par) {
    case -3: return N_("¡Albatros!");
    case -2: return N_("¡Águila!");
    case -1: return N_("¡Birdie!");
    case 0:  return N_("Par");
    case 1:  return N_("Bogey");
    case 2:  return N_("Doble bogey");
    case 3:  return N_("Triple bogey");
    default: return strokes < par ? N_("¡Increíble!") : N_("Recogida");
    }
}
