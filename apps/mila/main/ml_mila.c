/*
 * MILA - Mila's frames and what she wears (see ml_mila.h)
 */
#include "ml_mila.h"
#include "ml_gfx.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    uint8_t     dirs;           /* bits of MD_*                             */
    uint8_t     set;
} anim_info_t;

#define ALL4 0x0F
#define ONLY(d) (1 << (d))
#define EW (ONLY(MD_E) | ONLY(MD_W))

static const anim_info_t s_anims[MA_N] = {
    [MA_IDLE] = { "idle", ALL4, ML_SET_GAME },
    [MA_WALK] = { "walk", ALL4, ML_SET_GAME },
    [MA_PUSH] = { "push", ALL4, ML_SET_GAME },
    [MA_WIN] = { "win", ONLY(MD_S), ML_SET_GAME },
    [MA_YAWN] = { "yawn", ONLY(MD_S), ML_SET_GAME },
    [MA_C_WALK] = { "c_walk", ALL4, ML_SET_CASITA },
    [MA_C_RUN] = { "c_run", EW, ML_SET_CASITA },
    [MA_C_SIT] = { "c_sit", ALL4, ML_SET_CASITA },
    [MA_C_SLEEP] = { "c_sleep", ONLY(MD_S), ML_SET_CASITA },
    [MA_C_BELLY] = { "c_belly", ONLY(MD_S), ML_SET_CASITA },
    [MA_C_POUNCE] = { "c_pounce", EW, ML_SET_CASITA },
    [MA_C_BAT] = { "c_bat", EW, ML_SET_CASITA },
    [MA_C_JUMP] = { "c_jump", EW, ML_SET_CASITA },
    [MA_C_SCRATCH] = { "c_scratch", ONLY(MD_N), ML_SET_CASITA },
    [MA_C_EAT] = { "c_eat", ONLY(MD_N), ML_SET_CASITA },
    [MA_C_GROOM] = { "c_groom", ONLY(MD_S), ML_SET_CASITA },
    [MA_C_MEOW] = { "c_meow", ONLY(MD_S), ML_SET_CASITA },
    [MA_C_PURR] = { "c_purr", ONLY(MD_S), ML_SET_CASITA },
    [MA_C_PEEK] = { "c_peek", ONLY(MD_S), ML_SET_CASITA },
    [MA_C_LIE] = { "c_lie", ONLY(MD_S), ML_SET_CASITA },
    [MA_TURN] = { "turn", 0, ML_SET_TURN },
};

static const char DCH[4] = { 'n', 'e', 's', 'w' };

const char *ml_anim_name(int anim)
{
    return anim >= 0 && anim < MA_N ? s_anims[anim].name : "";
}

int ml_anim_dirs(int anim)
{
    return anim >= 0 && anim < MA_N ? s_anims[anim].dirs : 0;
}

static void frames_free(ml_frames_t *f)
{
    ml_anim_free(&f->body);
    ml_anim_free(&f->sh);
    ml_anim_free(&f->hat);
    ml_anim_free(&f->neck);
}

static void load_one(ml_mila_t *m, int anim)
{
    const anim_info_t *ai = &s_anims[anim];
    char nm[48];
    for (int d = 0; d < 4; d++) {
        bool has = ai->dirs ? (ai->dirs >> d & 1) : d == 0;
        if (!has) continue;
        ml_frames_t *f = &m->a[anim][d];
        if (ai->dirs) snprintf(nm, sizeof nm, "mila_%s_%c", ai->name, DCH[d]);
        else snprintf(nm, sizeof nm, "mila_%s", ai->name);
        if (!m->layers_only) {
            ml_art_load(nm, &f->body);
            char sh[56];
            snprintf(sh, sizeof sh, "%s_sh", nm);
            if (ml_art_has(sh)) ml_art_load(sh, &f->sh);
        }
        if (m->hat[0]) {
            if (ai->dirs) snprintf(nm, sizeof nm, "%s_%s_%c", m->hat, ai->name, DCH[d]);
            else snprintf(nm, sizeof nm, "%s_%s", m->hat, ai->name);
            ml_art_load(nm, &f->hat);
        }
        if (m->neck[0]) {
            if (ai->dirs) snprintf(nm, sizeof nm, "%s_%s_%c", m->neck, ai->name, DCH[d]);
            else snprintf(nm, sizeof nm, "%s_%s", m->neck, ai->name);
            ml_art_load(nm, &f->neck);
        }
        ml_yield();
    }
    m->loaded[anim] = 1;
}

static void make_lut(ml_lut_t *l, const char *item, uint32_t col)
{
    ml_pal_t p;
    char nm[32];
    memset(&p, 0, sizeof p);
    snprintf(nm, sizeof nm, "pal_%s", item);
    if (!ml_pal_load(nm, &p)) {
        for (int i = 0; i < 16; i++) p.c[i] = 0xC8C8C8;
    }
    p.c[4] = 0x2A2430;                  /* id 4: always dark (SPEC)          */
    if (col) p.c[1] = col;
    ml_lut_build(l, &p, 0xFFFFFF, 0);
}

void ml_mila_load(ml_mila_t *m, int sets, const char *hat, uint32_t hat_col, const char *neck,
                  uint32_t neck_col)
{
    bool outfit = strcmp(m->hat, hat ? hat : "") || strcmp(m->neck, neck ? neck : "");
    if (outfit) {
        /* a new outfit: every layer is reloaded */
        for (int a = 0; a < MA_N; a++) {
            for (int d = 0; d < 4; d++) frames_free(&m->a[a][d]);
            m->loaded[a] = 0;
        }
        snprintf(m->hat, sizeof m->hat, "%s", hat ? hat : "");
        snprintf(m->neck, sizeof m->neck, "%s", neck ? neck : "");
    }
    m->hat_col = hat_col;
    m->neck_col = neck_col;
    if (m->hat[0]) make_lut(&m->hat_lut, m->hat, hat_col);
    if (m->neck[0]) make_lut(&m->neck_lut, m->neck, neck_col);
    for (int a = 0; a < MA_N; a++) {
        bool want = (s_anims[a].set & sets) != 0;
        if (want && !m->loaded[a]) load_one(m, a);
        if (!want && m->loaded[a]) {
            for (int d = 0; d < 4; d++) frames_free(&m->a[a][d]);
            m->loaded[a] = 0;
        }
    }
    m->sets = sets;
}

void ml_mila_free(ml_mila_t *m)
{
    for (int a = 0; a < MA_N; a++) {
        for (int d = 0; d < 4; d++) frames_free(&m->a[a][d]);
        m->loaded[a] = 0;
    }
    m->hat[0] = m->neck[0] = 0;
    m->sets = 0;
}

const ml_frames_t *ml_mila_frames(const ml_mila_t *m, int anim, int d)
{
    if (anim < 0 || anim >= MA_N || !m->loaded[anim]) return NULL;
    int dirs = s_anims[anim].dirs;
    if (!dirs) return (m->layers_only || m->a[anim][0].body.n) ? &m->a[anim][0] : NULL;
    if (!(dirs >> d & 1)) {
        /* the nearest facing it has: e<->w first, then s */
        static const int alt[4][3] = { { MD_S, MD_E, MD_W }, { MD_W, MD_S, MD_N }, { MD_E, MD_W, MD_N },
                                       { MD_E, MD_S, MD_N } };
        for (int i = 0; i < 3; i++) {
            if (dirs >> alt[d][i] & 1) {
                d = alt[d][i];
                break;
            }
        }
    }
    return (m->layers_only || m->a[anim][d].body.n) ? &m->a[anim][d] : NULL;
}

int ml_mila_frame(const ml_frames_t *f, float t, bool loop)
{
    int n = f->body.n;
    if (n <= 1) return 0;
    float ms = f->body.ms ? (float)f->body.ms : 100.0f;
    int k = (int)(t * 1000.0f / ms);
    if (k < 0) k = 0;
    return loop ? k % n : (k >= n ? n - 1 : k);
}
