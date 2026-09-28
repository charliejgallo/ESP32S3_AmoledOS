/*
 * MONSTER HOP - the cast (see mh_cast.h)
 */
#include "mh_cast.h"

#include <stdlib.h>

#include <stdio.h>
#include <string.h>

static const char *const s_dirs[4] = { "n", "e", "s", "w" };

const char *mh_cap_name(int i)
{
    static const char *const n[CAP_N] = { "cap", "back", "beanie", "bucket", "propeller", "crown" };
    return i >= 0 && i < CAP_N ? n[i] : "";
}
const char *mh_back_name(int i)
{
    static const char *const n[BACK_N] = { "backpack", "cape", "tank", "wings" };
    return i >= 0 && i < BACK_N ? n[i] : "";
}
const char *mh_hand_name(int i)
{
    static const char *const n[HAND_N] = { "flashlight", "torch", "balloon", "bucket" };
    return i >= 0 && i < HAND_N ? n[i] : "";
}
const char *mh_pet_name(int i)
{
    static const char *const n[PET_N] = { "dog", "cat", "bat" };
    return i >= 0 && i < PET_N ? n[i] : "";
}
const char *mh_zone_key(int zone)
{
    static const char *const n[ZONE_N] = { "city", "castle", "desert", "forest", "test", "dino", "bay" };
    return zone >= 0 && zone < ZONE_N ? n[zone] : "test";
}

/* one animation of a rig: its four facings (or only s), and their shadows */
typedef struct {
    uint8_t slot;
    const char *name;
    uint8_t dirs;       /* 4 = n e s w, 1 = s only */
} anim_def_t;

static void rig_free(mh_rig_t *r)
{
    for (int i = 0; i < MH_RIG_MAX; i++) {
        for (int d = 0; d < 4; d++) {
            mh_anim_free(&r->a[i][d]);
            mh_anim_free(&r->sh[i][d]);
        }
    }
    r->any = false;
}

static void rig_load(mh_rig_t *r, const char *prefix, const anim_def_t *defs, int n, bool shadows)
{
    rig_free(r);
    char nm[48];
    for (int i = 0; i < n; i++) {
        const anim_def_t *a = &defs[i];
        for (int d = 0; d < 4; d++) {
            if (a->dirs == 1 && d != DIR_S) continue;
            snprintf(nm, sizeof nm, "%s_%s_%s", prefix, a->name, s_dirs[d]);
            if (mh_art_load(nm, &r->a[a->slot][d])) r->any = true;
            if (shadows) {
                snprintf(nm, sizeof nm, "%s_%s_%s_sh", prefix, a->name, s_dirs[d]);
                mh_art_load(nm, &r->sh[a->slot][d]);
            }
        }
        mh_yield();
    }
}

static const anim_def_t HERO_ANIMS[] = {
    { HA_IDLE, "idle", 4 }, { HA_HOP, "hop", 4 }, { HA_SUPER, "super", 4 }, { HA_PUSH, "push", 4 },
    { HA_USE, "use", 4 }, { HA_WIN, "win", 1 }, { HA_HURT, "hurt", 1 }, { HA_SINK, "sink", 1 },
    { HA_FALL, "fall", 1 },
};
static const anim_def_t PET_ANIMS[] = { { HA_IDLE, "idle", 4 }, { HA_HOP, "hop", 4 } };

bool mh_cast_hero(mh_cast_t *c, const mh_wear_t *w)
{
    char nm[40];
    if (!c->body.any) rig_load(&c->body, "tommy", HERO_ANIMS, 9, true);
    rig_free(&c->cap);
    rig_free(&c->back);
    rig_free(&c->hand);
    rig_free(&c->pet);
    if (w->cap >= 0) {
        snprintf(nm, sizeof nm, "cap_%s", mh_cap_name(w->cap));
        rig_load(&c->cap, nm, HERO_ANIMS, 9, false);
    }
    if (w->back >= 0) {
        snprintf(nm, sizeof nm, "back_%s", mh_back_name(w->back));
        rig_load(&c->back, nm, HERO_ANIMS, 9, false);
    }
    if (w->hand >= 0) {
        snprintf(nm, sizeof nm, "hand_%s", mh_hand_name(w->hand));
        rig_load(&c->hand, nm, HERO_ANIMS, 9, false);
    }
    if (w->pet >= 0) {
        snprintf(nm, sizeof nm, "pet_%s", mh_pet_name(w->pet));
        rig_load(&c->pet, nm, PET_ANIMS, 2, true);
    }
    if (!c->stand_in.n) {
        mh_art_load("tommy_test", &c->stand_in);
        mh_art_load("tommy_test_sh", &c->stand_in_sh);
    }
    return c->body.any || c->stand_in.n;
}

/* the monsters' animations, per kind */
static int mon_anims(int kind, anim_def_t *out)
{
    int n = 0;
#define A(s, nm, d) out[n].slot = (s), out[n].name = (nm), out[n].dirs = (d), n++
    switch (kind) {
    case MON_ZOMBIE:    A(MA_IDLE, "idle", 4); A(MA_WALK, "walk", 4); A(MA_NOTICE, "notice", 4); A(MA_LUNGE, "lunge", 4); break;
    case MON_VAMPIRE:   A(MA_IDLE, "idle", 4); A(MA_GLIDE, "glide", 4); A(MA_TRANSFORM, "transform", 1); break;
    case MON_MUMMY:     A(MA_IDLE, "idle", 4); A(MA_WALK, "walk", 4); A(MA_PUSH, "push", 4); break;
    case MON_WEREWOLF:  A(MA_IDLE, "idle", 4); A(MA_HOWL, "howl", 4); A(MA_RUN, "run", 4); A(MA_STUN, "stun", 4); break;
    case MON_ZOMBIEDOG: A(MA_IDLE, "idle", 4); A(MA_RUN, "run", 4); break;
    case MON_ARMOR:     A(MA_IDLE, "idle", 4); A(MA_WALK, "walk", 4); break;
    case MON_CROW:      A(MA_PERCH, "perch", 4); A(MA_FLY, "fly", 4); A(MA_DIVE, "dive", 4); break;
    case MON_BRUTE:     A(MA_WALK, "walk", 4); A(MA_STOMP, "stomp", 4); break;
    case MON_COUNT:     A(MA_GLIDE, "glide", 4); A(MA_CAST, "cast", 4); break;
    case MON_PHARAOH:   A(MA_WALK, "walk", 4); A(MA_WHIP, "whip", 4); break;
    case MON_ALPHA:     A(MA_RUN, "run", 4); A(MA_HOWL, "howl", 4); A(MA_STUN, "stun", 4); break;
    case MON_RAPTOR:    A(MA_IDLE, "idle", 4); A(MA_WALK, "walk", 4); A(MA_NOTICE, "notice", 4); A(MA_RUN, "run", 4); break;
    case MON_TRIKE:     A(MA_IDLE, "idle", 4); A(MA_WALK, "walk", 4); A(MA_HOWL, "howl", 4); A(MA_RUN, "run", 4);
                        A(MA_STUN, "stun", 4); break;
    case MON_PTERO:     A(MA_PERCH, "perch", 4); A(MA_FLY, "fly", 4); A(MA_DIVE, "dive", 4); break;
    case MON_FISHMAN:   A(MA_LURK, "lurk", 1); A(MA_EMERGE, "emerge", 1); A(MA_IDLE, "idle", 4); A(MA_WALK, "walk", 4);
                        A(MA_DIVE, "dive", 1); break;
    case MON_CRAB:      A(MA_IDLE, "idle", 4); A(MA_WALK, "walk", 4); A(MA_SNAP, "snap", 4); break;
    case MON_JELLY:     A(MA_FLOAT, "float", 1); break;
    case MON_TREX:      A(MA_RUN, "run", 4); A(MA_ROAR, "roar", 4); A(MA_STOMP, "stomp", 4); break;
    case MON_KRAKEN:    A(MA_IDLE, "idle", 1); A(MA_SLAM, "slam", 1); break;
    default: break;
    }
#undef A
    return n;
}

static const char *mon_prefix(int kind)
{
    static const char *const n[MON_N] = { "zombie", "vampire", "mummy", "werewolf", "zombiedog", "armor",
                                          "crow", "brute", "count", "pharaoh", "alpha", "raptor", "trike",
                                          "ptero", "fishman", "crab", "jelly", "trex", "kraken" };
    return kind >= 0 && kind < MON_N ? n[kind] : "";
}

/* bats and scarabs are rigs too, kept in the slots after the monsters */
static const anim_def_t BAT_ANIMS[] = { { MA_FLY, "fly", 4 } };
static const anim_def_t SCARAB_ANIMS[] = { { MA_CRAWL, "crawl", 4 } };
static const anim_def_t COMPY_ANIMS[] = { { MA_RUN, "run", 4 } };

static void ob_load(mh_cast_t *c, int i, const char *name)
{
    char nm[48];
    mh_anim_free(&c->ob[i]);
    mh_anim_free(&c->ob_sh[i]);
    mh_anim_free(&c->ob_gl[i]);
    mh_art_load(name, &c->ob[i]);
    snprintf(nm, sizeof nm, "%s_sh", name);
    if (mh_art_has(nm)) mh_art_load(nm, &c->ob_sh[i]);
    snprintf(nm, sizeof nm, "%s_gl", name);
    if (mh_art_has(nm)) mh_art_load(nm, &c->ob_gl[i]);
}

bool mh_cast_level(mh_cast_t *c, const mh_level_t *lv)
{
    bool want[MON_N] = { false };
    bool bat = false, scarab = false, compy = false;
    for (int i = 0; i < lv->n_ents; i++) {
        const mh_ent_def_t *e = &lv->ent[i];
        if (e->type == ENT_MONSTER && e->a < MON_N) {
            want[e->a] = true;
            if (e->a == MON_VAMPIRE || e->a == MON_COUNT) bat = true;
        }
        if (e->type == ENT_LANE && e->a == LANE_BAT) bat = true;
        if (e->type == ENT_LANE && e->a == LANE_SCARAB) scarab = true;
        if (e->type == ENT_LANE && e->a == LANE_COMPY) compy = true;
    }
    anim_def_t defs[MH_RIG_MAX];
    for (int k = 0; k < MON_N; k++) {
        if (!want[k]) {
            if (c->mon[k]) {
                rig_free(c->mon[k]);
                free(c->mon[k]);
                c->mon[k] = NULL;
            }
            continue;
        }
        if (!c->mon[k]) c->mon[k] = (mh_rig_t *)mh_calloc(1, sizeof(mh_rig_t));
        if (!c->mon[k] || c->mon[k]->any) continue;
        int n = mon_anims(k, defs);
        rig_load(c->mon[k], mon_prefix(k), defs, n, true);
    }
    const char *z = mh_zone_key(lv->zone);
    char nm[48];
    static const char *const common[] = { "key", "coin", "heart", "hourglass", "chest", "lever",
                                          "lantern_off", "lantern_on", "fx_dust", "fx_splash",
                                          "fx_poof", "fx_sparkle", "fx_bubbles" };
    for (int i = 0; i <= OB_FX_BUBBLES; i++) {
        if (!c->ob[i].n) ob_load(c, i, common[i]);
    }
    if (!c->ob[OB_STICKER].n) ob_load(c, OB_STICKER, "sticker");
    if (c->zone != lv->zone || !c->ob[OB_GATE].n) {
        c->zone = lv->zone;
        static const char *const zoned[] = { "gate", "crate", "platform", "spikes", "vent", "dart_x", "dart_y",
                                             "boulder_x", "boulder_y", "runcar_e", "runcar_w", "logfloat_w",
                                             "logfloat_m", "logfloat_e", "lily", "beartrap", "coffin_open",
                                             "tide", "lava_x", "lava_y", "fallrock", "rockbits", "piranha",
                                             "wave_x", "wave_y", "tentacle_w", "tentacle_m", "tentacle_e" };
        _Static_assert(sizeof zoned / sizeof zoned[0] == OB_STICKER - OB_GATE, "a name for each zone object");
        for (int i = OB_GATE; i < OB_STICKER; i++) {
            snprintf(nm, sizeof nm, "%s_%s", z, zoned[i - OB_GATE]);
            ob_load(c, i, nm);
            mh_yield();
        }
    }
    if (bat != c->bat.any) rig_load(&c->bat, "bat", BAT_ANIMS, bat ? 1 : 0, true);
    if (scarab != c->scarab.any) rig_load(&c->scarab, "scarab", SCARAB_ANIMS, scarab ? 1 : 0, true);
    if (compy != c->compy.any) rig_load(&c->compy, "compy", COMPY_ANIMS, compy ? 1 : 0, true);
    return true;
}

void mh_cast_level_free(mh_cast_t *c)
{
    for (int k = 0; k < MON_N; k++) {
        if (!c->mon[k]) continue;
        rig_free(c->mon[k]);
        free(c->mon[k]);
        c->mon[k] = NULL;
    }
    rig_free(&c->bat);
    rig_free(&c->scarab);
    rig_free(&c->compy);
    for (int i = 0; i < OB_N; i++) {
        mh_anim_free(&c->ob[i]);
        mh_anim_free(&c->ob_sh[i]);
        mh_anim_free(&c->ob_gl[i]);
    }
    c->zone = -1;
}

void mh_cast_free(mh_cast_t *c)
{
    rig_free(&c->body);
    rig_free(&c->cap);
    rig_free(&c->back);
    rig_free(&c->hand);
    rig_free(&c->pet);
    for (int k = 0; k < MON_N; k++) {
        if (!c->mon[k]) continue;
        rig_free(c->mon[k]);
        free(c->mon[k]);
        c->mon[k] = NULL;
    }
    rig_free(&c->bat);
    rig_free(&c->scarab);
    rig_free(&c->compy);
    for (int i = 0; i < OB_N; i++) {
        mh_anim_free(&c->ob[i]);
        mh_anim_free(&c->ob_sh[i]);
        mh_anim_free(&c->ob_gl[i]);
    }
    mh_anim_free(&c->stand_in);
    mh_anim_free(&c->stand_in_sh);
}
