/*
 * MONSTER HOP - progress (see mh_prog.h)
 */
#include "mh_prog.h"

#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <string.h>

static const char *key(char *b, size_t n, const char *p, int i)
{
    snprintf(b, n, "%s%d", p, i);
    return b;
}

static int32_t get(const char *k, int32_t def)
{
    int32_t v;
    return aos_hal_pref_get_i32(k, &v) ? v : def;
}

void mh_prog_reset(mh_prog_t *p)
{
    memset(p, 0, sizeof(*p));
    p->diff = 1;
    p->sfx = p->music = true;
    for (int c = 0; c < CAT_N; c++) {
        /* what is free is owned */
        for (int i = 0; i < mh_shop_count(c) && i < 32; i++)
            if (mh_shop_item(c, i)->price == 0) p->own[c] |= 1u << i;
        p->eq[c] = -1;
    }
    p->eq[CAT_CAP] = 0;
    p->eq[CAT_SHIRT] = 0;
    p->eq[CAT_PET] = 0;
    p->eq[CAT_SKIN] = 1;
    p->eq[CAT_HAIR] = 1;
}

void mh_prog_load(mh_prog_t *p)
{
    char k[20];
    mh_prog_reset(p);
    p->coins = get("mh_coins", 0);
    p->diff = (int)get("mh_diff", 1);
    if (p->diff < 0 || p->diff > 2) p->diff = 1;
    p->sfx = get("mh_sfx", 1) != 0;
    p->music = get("mh_music", 1) != 0;
    for (int i = 0; i < MH_NLEVELS; i++) {
        p->stars[i] = (uint8_t)(get(key(k, sizeof k, "mh_st", i), 0) & 3);
        p->best_ms[i] = get(key(k, sizeof k, "mh_bt", i), 0);
    }
    p->stickers = (uint32_t)get("mh_stk", 0);
    p->trophies = (uint32_t)get("mh_tro", 0);
    for (int i = 0; i < SX_N; i++) p->stat[i] = get(key(k, sizeof k, "mh_sx", i), 0);
    for (int c = 0; c < CAT_N; c++) {
        p->own[c] |= (uint32_t)get(key(k, sizeof k, "mh_own", c), 0);
        int32_t e = get(key(k, sizeof k, "mh_eq", c), -99);
        if (e != -99 && e >= -1 && e < mh_shop_count(c) && (e < 0 || mh_prog_owns(p, c, e))) p->eq[c] = (int8_t)e;
    }
}

void mh_prog_save(const mh_prog_t *p)
{
    char k[20];
    aos_hal_pref_set_i32("mh_coins", p->coins);
    aos_hal_pref_set_i32("mh_diff", p->diff);
    aos_hal_pref_set_i32("mh_sfx", p->sfx);
    aos_hal_pref_set_i32("mh_music", p->music);
    for (int i = 0; i < MH_NLEVELS; i++) {
        aos_hal_pref_set_i32(key(k, sizeof k, "mh_st", i), p->stars[i]);
        aos_hal_pref_set_i32(key(k, sizeof k, "mh_bt", i), p->best_ms[i]);
    }
    aos_hal_pref_set_i32("mh_stk", (int32_t)p->stickers);
    aos_hal_pref_set_i32("mh_tro", (int32_t)p->trophies);
    for (int i = 0; i < SX_N; i++) aos_hal_pref_set_i32(key(k, sizeof k, "mh_sx", i), p->stat[i]);
    for (int c = 0; c < CAT_N; c++) {
        aos_hal_pref_set_i32(key(k, sizeof k, "mh_own", c), (int32_t)p->own[c]);
        aos_hal_pref_set_i32(key(k, sizeof k, "mh_eq", c), p->eq[c]);
    }
}

bool mh_prog_owns(const mh_prog_t *p, int cat, int i)
{
    return cat >= 0 && cat < CAT_N && i >= 0 && i < 32 && (p->own[cat] & (1u << i));
}

int mh_prog_stars(const mh_prog_t *p)
{
    int s = 0;
    for (int i = 0; i < MH_NLEVELS; i++) s += p->stars[i];
    return s;
}

int mh_zone_first(int zone)
{
    switch (zone) {
    case ZONE_CITY: return 0;
    case ZONE_CASTLE: return 4;
    case ZONE_DESERT: return 8;
    case ZONE_FOREST: return 12;
    case ZONE_DINO: return 16;
    case ZONE_BAY: return 20;
    default: return -1;
    }
}

static bool zone_full(const mh_prog_t *p, int z)
{
    int f = mh_zone_first(z);
    if (f < 0) return false;
    for (int k = 0; k < 4; k++) if (p->stars[f + k] < 3) return false;
    return true;
}

/* the zones built so far, and their trophies */
static const struct { uint8_t zone, trophy; } s_zone_tr[] = {
    { ZONE_CITY, TR_ZONE_CITY }, { ZONE_CASTLE, TR_ZONE_CASTLE }, { ZONE_DESERT, TR_ZONE_DESERT },
    { ZONE_FOREST, TR_ZONE_FOREST }, { ZONE_DINO, TR_ZONE_DINO }, { ZONE_BAY, TR_ZONE_BAY },
};

static int popcount(uint32_t v)
{
    int n = 0;
    while (v) { n += (int)(v & 1); v >>= 1; }
    return n;
}

uint32_t mh_prog_trophies(mh_prog_t *p)
{
    uint32_t had = p->trophies, now = had;
    if (p->stat[SX_LEVELS] > 0) now |= 1u << TR_FIRST;
    bool bosses = true;
    for (size_t k = 0; k < sizeof s_zone_tr / sizeof s_zone_tr[0]; k++) {
        if (zone_full(p, s_zone_tr[k].zone)) now |= 1u << s_zone_tr[k].trophy;
        if (!p->stars[mh_zone_first(s_zone_tr[k].zone) + 3]) bosses = false;
    }
    if (bosses) now |= 1u << TR_BOSSES;
    if (popcount(p->stickers) >= MH_NLEVELS) now |= 1u << TR_ALBUM;
    if (p->stat[SX_KEYS] >= 100) now |= 1u << TR_KEYS100;
    if (p->stat[SX_HOPS] >= 5000) now |= 1u << TR_HOPS5000;
    if (p->stat[SX_BOUGHT] >= 10) now |= 1u << TR_SHOP10;
    if (p->stat[SX_RACES_WON] > 0) now |= 1u << TR_FRIEND;
    if (mh_prog_stars(p) >= MH_NLEVELS * 3) now |= 1u << TR_ALL_STARS;
    /* TR_CLEAN and TR_HARD are set by the result screen, which knows */
    p->trophies = now;
    return now & ~had;
}

const char *mh_trophy_name(int t)
{
    switch (t) {
    case TR_FIRST: return _("Primer paso");
    case TR_CLEAN: return _("Sin un rasguño");
    case TR_HARD: return _("Valiente");
    case TR_ZONE_CITY: return _("Rey del Pueblo Zombi");
    case TR_ZONE_CASTLE: return _("Señor del Castillo");
    case TR_ZONE_DESERT: return _("Sultán del Desierto");
    case TR_ZONE_FOREST: return _("Guardián del Bosque");
    case TR_BOSSES: return _("Cazador de jefes");
    case TR_ALBUM: return _("Álbum completo");
    case TR_KEYS100: return _("Llavero");
    case TR_HOPS5000: return _("Saltarín");
    case TR_SHOP10: return _("A la moda");
    case TR_FRIEND: return _("Mejor amigo");
    case TR_ALL_STARS: return _("Todas las estrellas");
    case TR_ZONE_DINO: return _("Domador del Valle");
    case TR_ZONE_BAY: return _("Capitán de la Bahía");
    default: return "";
    }
}

const char *mh_trophy_desc(int t)
{
    switch (t) {
    case TR_FIRST: return _("Termina un nivel");
    case TR_CLEAN: return _("Termina un nivel sin perder una vida");
    case TR_HARD: return _("Termina un nivel en Difícil");
    case TR_ZONE_CITY: return _("Tres estrellas en todo el Pueblo Zombi");
    case TR_ZONE_CASTLE: return _("Tres estrellas en todo el Castillo Vampiro");
    case TR_ZONE_DESERT: return _("Tres estrellas en todo el Desierto de las Momias");
    case TR_ZONE_FOREST: return _("Tres estrellas en todo el Bosque Lobizón");
    case TR_BOSSES: return _("Gana en todas las guaridas");
    case TR_ALBUM: {
        static char b[64];
        snprintf(b, sizeof b, _("Encuentra las %d figuritas"), MH_NLEVELS);
        return b;
    }
    case TR_KEYS100: return _("Junta 100 llaves");
    case TR_HOPS5000: return _("Da 5000 saltos");
    case TR_SHOP10: return _("Compra 10 cosas en la tienda");
    case TR_FRIEND: return _("Gana una carrera contra el otro reloj");
    case TR_ALL_STARS: {
        static char b[64];
        snprintf(b, sizeof b, _("Las %d estrellas"), MH_NLEVELS * 3);
        return b;
    }
    case TR_ZONE_DINO: return _("Tres estrellas en todo el Valle Perdido");
    case TR_ZONE_BAY: return _("Tres estrellas en toda la Bahía Abisal");
    default: return "";
    }
}

int mh_trophy_tier(int t)
{
    switch (t) {
    case TR_ALBUM: case TR_BOSSES: case TR_ALL_STARS: case TR_ZONE_FOREST: case TR_ZONE_BAY: return 2;
    case TR_ZONE_DINO: return 1;
    case TR_ZONE_CITY: case TR_ZONE_CASTLE: case TR_ZONE_DESERT: case TR_HARD: case TR_KEYS100: case TR_HOPS5000: return 1;
    default: return 0;
    }
}
