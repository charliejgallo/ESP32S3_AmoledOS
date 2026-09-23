/*
 * MILA - what the player has (see ml_prog.h)
 */
#include "ml_prog.h"

#include "aos_hal.h"

#include <stdio.h>
#include <string.h>

static const char *const s_stat_key[SX_N] = {
    "ml_st_mov", "ml_st_psh", "ml_st_sol", "ml_st_und", "ml_st_pet", "ml_st_toy", "ml_st_sec",
};

void ml_prog_load(ml_prog_t *p, const ml_worlds_t *w)
{
    memset(p, 0, sizeof(*p));
    char key[32], buf[ML_MAX_LEVELS + 2];
    for (int i = 0; i < w->nworlds; i++) {
        snprintf(key, sizeof key, "ml_s_%s", w->w[i].id);
        if (!aos_hal_pref_get_str(key, buf, sizeof buf)) continue;
        for (int k = 0; k < ML_MAX_LEVELS && buf[k]; k++)
            p->stars[i][k] = (uint8_t)(buf[k] >= '0' && buf[k] <= '3' ? buf[k] - '0' : 0);
    }
    int32_t v;
    if (aos_hal_pref_get_i32("ml_coins", &v)) p->coins = v;
    aos_hal_pref_get_str("ml_own", p->own, sizeof p->own);
    /* she starts with her red collar */
    if (!aos_hal_pref_get_str("ml_neck", p->neck, sizeof p->neck)) snprintf(p->neck, sizeof p->neck, "neck_bell");
    aos_hal_pref_get_str("ml_hat", p->hat, sizeof p->hat);
    if (aos_hal_pref_get_i32("ml_hatc", &v)) p->hat_col = v;
    if (aos_hal_pref_get_i32("ml_neckc", &v)) p->neck_col = v;
    if (aos_hal_pref_get_i32("ml_gift", &v)) p->gift_day = v;
    aos_hal_pref_get_str("ml_last", p->last, sizeof p->last);
    p->snd = 3;
    if (aos_hal_pref_get_i32("ml_snd", &v)) p->snd = v;
    for (int i = 0; i < SX_N; i++)
        if (aos_hal_pref_get_i32(s_stat_key[i], &v)) p->stat[i] = v;
    if (!ml_prog_owns(p, "neck_bell")) ml_prog_add(p, "neck_bell");
}

void ml_prog_save(const ml_prog_t *p, const ml_worlds_t *w)
{
    char key[32], buf[ML_MAX_LEVELS + 2];
    for (int i = 0; i < w->nworlds; i++) {
        int n = w->w[i].nlevels;
        for (int k = 0; k < n; k++) buf[k] = (char)('0' + p->stars[i][k]);
        buf[n] = 0;
        snprintf(key, sizeof key, "ml_s_%s", w->w[i].id);
        aos_hal_pref_set_str(key, buf);
    }
    aos_hal_pref_set_i32("ml_coins", p->coins);
    aos_hal_pref_set_str("ml_own", p->own);
    aos_hal_pref_set_str("ml_hat", p->hat);
    aos_hal_pref_set_str("ml_neck", p->neck);
    aos_hal_pref_set_i32("ml_hatc", p->hat_col);
    aos_hal_pref_set_i32("ml_neckc", p->neck_col);
    aos_hal_pref_set_i32("ml_gift", p->gift_day);
    aos_hal_pref_set_str("ml_last", p->last);
    aos_hal_pref_set_i32("ml_snd", p->snd);
    for (int i = 0; i < SX_N; i++) aos_hal_pref_set_i32(s_stat_key[i], p->stat[i]);
}

int ml_prog_world_stars(const ml_prog_t *p, const ml_worlds_t *w, int world)
{
    int n = 0;
    if (world < 0 || world >= w->nworlds) return 0;
    for (int k = 0; k < w->w[world].nlevels; k++) n += p->stars[world][k];
    return n;
}

int ml_prog_total_stars(const ml_prog_t *p, const ml_worlds_t *w)
{
    int n = 0;
    for (int i = 0; i < w->nworlds; i++) n += ml_prog_world_stars(p, w, i);
    return n;
}

bool ml_prog_owns(const ml_prog_t *p, const char *item)
{
    size_t l = strlen(item);
    for (const char *s = p->own; (s = strstr(s, item)) != NULL; s += l) {
        bool start = s == p->own || s[-1] == ' ';
        bool end = s[l] == 0 || s[l] == ' ';
        if (start && end) return true;
    }
    return false;
}

void ml_prog_add(ml_prog_t *p, const char *item)
{
    if (ml_prog_owns(p, item)) return;
    size_t n = strlen(p->own);
    if (n + strlen(item) + 2 >= sizeof p->own) return;
    snprintf(p->own + n, sizeof p->own - n, "%s%s", n ? " " : "", item);
}
