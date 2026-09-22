/*
 * MONSTER HOP - the sound (see mh_audio.h)
 */
#include "mh_audio.h"

#include "aos_hal.h"

#include <math.h>
#include <string.h>

#define RATE        16000
#define TARGET      1600            /* samples to keep queued: 100 ms       */
#define VOICES      8

enum { V_OFF = 0, V_NOISE, V_TONE, V_SQUARE, V_WOBBLE };

typedef struct {
    uint8_t  kind;
    int32_t  t, dur;                /* samples                               */
    int32_t  amp;
    int32_t  attack;
    uint32_t phase, step, step_end; /* 16.16 over a 256-entry sine            */
    int32_t  lp, lp_k;
    int32_t  hp;
    uint8_t  hp_on;
    uint32_t seed;
} voice_t;

static voice_t  s_v[VOICES];
static bool     s_open, s_sfx = true, s_mus_on = true;
static uint32_t s_rng = 0x1234567u;
static int16_t  s_sin[256];
static uint32_t s_note_step[128];

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static uint32_t hz_step(int hz)
{
    return (uint32_t)hz * 1049u;        /* 2^24 / 16000: 256 << 16 per cycle */
}

static void tables(void)
{
    for (int i = 0; i < 256; i++) {
        int q = i & 127;
        int x = q * 2 - 127;
        int y = 32767 - (x * x * 32767) / (127 * 127);
        s_sin[i] = (int16_t)(i < 128 ? y : -y);
    }
    for (int n = 0; n < 128; n++) {
        float hz = 440.0f * powf(2.0f, (float)(n - 69) / 12.0f);
        s_note_step[n] = (uint32_t)(hz * 1048.576f);
    }
}

static voice_t *voice_get(void)
{
    for (int i = 0; i < VOICES; i++) {
        if (s_v[i].kind == V_OFF) return &s_v[i];
    }
    int best = 0, left = 0x7FFFFFFF;
    for (int i = 0; i < VOICES; i++) {
        int l = s_v[i].dur - s_v[i].t;
        if (l < left) {
            left = l;
            best = i;
        }
    }
    return &s_v[best];
}

static void noise(int ms, int amp, int attack_ms, int lp_k, bool hp, int delay_ms)
{
    voice_t *v = voice_get();
    memset(v, 0, sizeof(*v));
    v->kind = V_NOISE;
    v->dur = ms * RATE / 1000;
    v->amp = amp;
    v->attack = attack_ms * RATE / 1000;
    v->lp_k = lp_k;
    v->hp_on = hp;
    v->seed = rnd() | 1;
    v->t = -(delay_ms * RATE / 1000);
}

static void tone(int hz, int hz_end, int ms, int amp, int attack_ms, int delay_ms, uint8_t kind)
{
    voice_t *v = voice_get();
    memset(v, 0, sizeof(*v));
    v->kind = kind;
    v->dur = ms * RATE / 1000;
    v->amp = amp;
    v->attack = attack_ms * RATE / 1000;
    v->step = hz_step(hz);
    v->step_end = hz_step(hz_end);
    v->t = -(delay_ms * RATE / 1000);
}

static inline int32_t env(const voice_t *v)
{
    if (v->t < v->attack) return v->amp * v->t / (v->attack ? v->attack : 1);
    int32_t left = v->dur - v->t, span = v->dur - v->attack;
    if (span <= 0) return 0;
    int32_t f = (left << 10) / span;
    return ((v->amp * f) >> 10) * f >> 10;
}

static int32_t voice_sample(voice_t *v)
{
    int32_t e = env(v), s = 0;
    switch (v->kind) {
    case V_NOISE: {
        v->seed ^= v->seed << 13;
        v->seed ^= v->seed >> 17;
        v->seed ^= v->seed << 5;
        int32_t n = (int32_t)(v->seed & 0xFFFF) - 32768;
        v->lp += ((n - v->lp) * v->lp_k) >> 8;
        s = v->lp;
        if (v->hp_on) {
            int32_t hp = s - v->hp;
            v->hp += (s - v->hp) >> 4;
            s = hp;
        }
        break;
    }
    case V_TONE:
    case V_SQUARE:
    case V_WOBBLE: {
        int32_t k = v->dur ? (v->t << 8) / v->dur : 0;
        uint32_t st = v->step + (uint32_t)((((int32_t)v->step_end - (int32_t)v->step) >> 8) * k);
        if (v->kind == V_WOBBLE) {
            int32_t w = s_sin[(v->t * 7 / 16) & 255];
            st += (uint32_t)(((int32_t)(st >> 8) * w) >> 11);
        }
        v->phase += st;
        s = s_sin[(v->phase >> 16) & 255];
        if (v->kind == V_SQUARE) s = s > 0 ? 16000 + (s >> 2) : -16000 + (s >> 2);
        break;
    }
    default:
        break;
    }
    v->t++;
    if (v->t >= v->dur) v->kind = V_OFF;
    return (s * e) >> 15;
}

/* ---- effects ---- */

void mh_snd(int id)
{
    if (!s_sfx) return;
    if (!s_open) {
        switch (id) {
        case SND_KEY: aos_hal_beep(1568, 60); aos_hal_beep(2093, 90); break;
        case SND_COIN: aos_hal_beep(1976, 40); break;
        case SND_HURT: aos_hal_beep(200, 200); break;
        case SND_WIN: aos_hal_beep(1047, 90); aos_hal_beep(1568, 200); break;
        default: break;
        }
        return;
    }
    switch (id) {
    case SND_HOP:
        tone(520, 900, 70, 5500, 2, 0, V_TONE);
        break;
    case SND_SUPER:
        tone(420, 1500, 220, 6500, 4, 0, V_TONE);
        noise(220, 3500, 60, 60, true, 0);
        break;
    case SND_LAND:
        noise(50, 5000, 0, 70, false, 0);
        break;
    case SND_KEY:
        tone(1319, 1319, 90, 6500, 2, 0, V_TONE);
        tone(1760, 1760, 90, 6500, 2, 70, V_TONE);
        tone(2637, 2637, 260, 5500, 2, 140, V_TONE);
        break;
    case SND_COIN:
        tone(1976, 1976, 50, 5000, 1, 0, V_TONE);
        tone(2637, 2637, 150, 5000, 1, 45, V_TONE);
        break;
    case SND_HURT:
        tone(700, 180, 420, 9000, 2, 0, V_WOBBLE);
        noise(160, 7000, 0, 150, false, 0);
        break;
    case SND_SPLASH:
        noise(420, 12000, 5, 90, true, 0);
        noise(600, 6000, 60, 30, false, 40);
        break;
    case SND_FALL:
        tone(1400, 220, 900, 7000, 5, 0, V_TONE);
        break;
    case SND_CHECK:
        tone(988, 988, 120, 6000, 2, 0, V_TONE);
        tone(1319, 1319, 300, 6000, 2, 100, V_TONE);
        break;
    case SND_OPEN:
        tone(784, 784, 120, 6500, 2, 0, V_SQUARE);
        tone(1047, 1047, 120, 6500, 2, 110, V_SQUARE);
        tone(1319, 1319, 120, 6500, 2, 220, V_SQUARE);
        tone(1568, 1568, 420, 6500, 2, 330, V_SQUARE);
        break;
    case SND_WIN:
        tone(1047, 1047, 120, 7000, 2, 0, V_SQUARE);
        tone(1319, 1319, 120, 7000, 2, 120, V_SQUARE);
        tone(1568, 1568, 120, 7000, 2, 240, V_SQUARE);
        tone(2093, 2093, 600, 7000, 2, 360, V_SQUARE);
        tone(523, 523, 900, 5000, 2, 0, V_TONE);
        break;
    case SND_LEVER:
        noise(40, 9000, 0, 220, true, 0);
        noise(40, 9000, 0, 220, true, 110);
        tone(300, 200, 90, 5000, 0, 0, V_TONE);
        break;
    case SND_CHEST:
        tone(180, 260, 260, 5000, 30, 0, V_WOBBLE);
        tone(1568, 2637, 300, 5000, 5, 200, V_TONE);
        break;
    case SND_PUSH:
        noise(260, 7000, 30, 40, false, 0);
        break;
    case SND_BUMP:
        tone(160, 100, 80, 8000, 0, 0, V_TONE);
        break;
    case SND_HOWL:
        tone(420, 880, 350, 6000, 60, 0, V_WOBBLE);
        tone(880, 520, 600, 6000, 20, 330, V_WOBBLE);
        break;
    case SND_TIMEUP:
        tone(523, 494, 260, 8000, 3, 0, V_SQUARE);
        tone(392, 370, 360, 8000, 3, 220, V_SQUARE);
        tone(262, 247, 800, 8000, 3, 480, V_SQUARE);
        break;
    case SND_OVER:
        tone(392, 392, 260, 7000, 3, 0, V_TONE);
        tone(370, 370, 260, 7000, 3, 260, V_TONE);
        tone(349, 349, 260, 7000, 3, 520, V_TONE);
        tone(330, 300, 900, 7000, 3, 780, V_WOBBLE);
        break;
    case SND_LIFE:
        tone(1047, 1047, 80, 6000, 2, 0, V_TONE);
        tone(1319, 1319, 80, 6000, 2, 70, V_TONE);
        tone(1568, 1568, 80, 6000, 2, 140, V_TONE);
        tone(2093, 2093, 250, 6000, 2, 210, V_TONE);
        break;
    case SND_TIME:
        tone(1800, 1800, 40, 5000, 1, 0, V_TONE);
        tone(1350, 1350, 40, 5000, 1, 180, V_TONE);
        break;
    case SND_STOMP:
        tone(90, 35, 500, 16000, 0, 0, V_TONE);
        noise(400, 12000, 0, 40, false, 0);
        break;
    case SND_CAST:
        tone(330, 330, 700, 4000, 80, 0, V_WOBBLE);
        tone(392, 392, 700, 4000, 80, 0, V_WOBBLE);
        tone(466, 466, 700, 4000, 80, 0, V_WOBBLE);
        break;
    case SND_LOW:
        tone(1500, 1500, 40, 5000, 1, 0, V_TONE);
        break;
    case SND_SELECT:
        tone(1200, 1500, 50, 4500, 1, 0, V_TONE);
        break;
    case SND_GO:
        tone(880, 880, 120, 7000, 2, 0, V_SQUARE);
        tone(1760, 1760, 300, 7000, 2, 120, V_SQUARE);
        break;
    default:
        break;
    }
}

/* ---- music ---- */

/* a theme: steps of a 16th (or an 8th, for the 6/8 one), per step a bass
 * note (0 = none), a lead note (0 = rest, 1 = hold) and the drums
 * (1 kick, 2 snare, 4 hat) */
typedef struct {
    int ms;
    int n;
    const int8_t *bass, *lead, *drum;
} theme_t;

static const int8_t CITY_B[32] = { 45, 0, 0, 45, 0, 0, 48, 0, 45, 0, 0, 45, 0, 0, 43, 0,
                                   41, 0, 0, 41, 0, 0, 45, 0, 40, 0, 43, 0, 44, 0, 40, 0 };
static const int8_t CITY_L[32] = { 69, 1, 0, 0, 72, 1, 71, 1, 69, 1, 1, 1, 0, 0, 64, 0,
                                   65, 1, 0, 0, 69, 1, 68, 1, 64, 1, 1, 1, 0, 0, 0, 0 };
static const int8_t CITY_D[32] = { 5, 0, 4, 0, 6, 0, 4, 0, 5, 0, 4, 1, 6, 0, 4, 0,
                                   5, 0, 4, 0, 6, 0, 4, 0, 5, 0, 4, 1, 6, 0, 4, 2 };

static const int8_t CAST_B[48] = { 38, 0, 0, 0, 45, 0, 0, 0, 45, 0, 0, 0, 33, 0, 0, 0, 40, 0, 0, 0, 40, 0, 0, 0,
                                   34, 0, 0, 0, 41, 0, 0, 0, 41, 0, 0, 0, 33, 0, 0, 0, 40, 0, 0, 0, 37, 0, 0, 0 };
static const int8_t CAST_L[48] = { 74, 1, 1, 1, 77, 1, 76, 1, 74, 1, 73, 1, 69, 1, 1, 1, 1, 1, 0, 0, 70, 1, 69, 1,
                                   70, 1, 1, 1, 74, 1, 72, 1, 70, 1, 69, 1, 69, 1, 1, 1, 1, 1, 1, 1, 73, 1, 1, 1 };
static const int8_t CAST_D[48] = { 1, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0,
                                   1, 0, 0, 0, 4, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0, 6, 0, 0, 0 };

static const int8_t DES_B[32] = { 40, 0, 40, 0, 0, 0, 40, 0, 41, 0, 40, 0, 0, 0, 40, 0,
                                  40, 0, 40, 0, 0, 0, 40, 0, 44, 0, 41, 0, 40, 0, 0, 0 };
static const int8_t DES_L[32] = { 76, 1, 77, 1, 80, 1, 1, 1, 81, 1, 80, 1, 77, 1, 76, 1,
                                  0, 0, 76, 1, 77, 1, 80, 1, 81, 1, 80, 1, 77, 1, 76, 1 };
static const int8_t DES_D[32] = { 1, 4, 0, 4, 0, 4, 1, 4, 2, 4, 1, 4, 0, 4, 0, 4,
                                  1, 4, 0, 4, 0, 4, 1, 4, 2, 4, 1, 4, 2, 0, 2, 4 };

static const int8_t FOR_B[24] = { 40, 0, 0, 47, 0, 0, 38, 0, 0, 45, 0, 0, 37, 0, 0, 44, 0, 0, 35, 0, 0, 42, 0, 0 };
static const int8_t FOR_L[24] = { 71, 1, 74, 1, 76, 1, 74, 1, 1, 71, 1, 0, 69, 1, 71, 1, 73, 1, 71, 1, 1, 1, 1, 0 };
static const int8_t FOR_D[24] = { 1, 0, 0, 4, 0, 0, 2, 0, 0, 4, 0, 0, 1, 0, 0, 4, 0, 0, 2, 0, 0, 4, 0, 4 };

static const int8_t MEN_B[32] = { 36, 0, 43, 0, 36, 0, 43, 0, 39, 0, 46, 0, 39, 0, 46, 0,
                                  41, 0, 48, 0, 41, 0, 48, 0, 43, 0, 47, 0, 43, 0, 38, 0 };
static const int8_t MEN_L[32] = { 72, 1, 0, 72, 75, 1, 72, 1, 79, 1, 1, 1, 78, 1, 77, 1,
                                  75, 1, 0, 75, 77, 1, 75, 1, 74, 1, 1, 1, 71, 1, 1, 0 };
static const int8_t MEN_D[32] = { 5, 0, 4, 0, 6, 0, 4, 0, 5, 0, 4, 0, 6, 0, 4, 0,
                                  5, 0, 4, 0, 6, 0, 4, 0, 5, 0, 4, 1, 6, 0, 6, 0 };

static const theme_t *theme_of(int i)
{
    static const theme_t t[MUS_N] = {
        { 140, 32, CITY_B, CITY_L, CITY_D },
        { 120, 48, CAST_B, CAST_L, CAST_D },
        { 128, 32, DES_B, DES_L, DES_D },
        { 190, 24, FOR_B, FOR_L, FOR_D },
        { 125, 32, MEN_B, MEN_L, MEN_D },
    };
    return i >= 0 && i < MUS_N ? &t[i] : NULL;
}

/* the music has voices of its own, so effects never cut it */
typedef struct {
    uint32_t ph, step;
    int32_t  amp, amp_t;
    int32_t  t;
} mvoice_t;

static int      s_theme = MUS_NONE;
static bool     s_boss;
static int32_t  s_step_left;        /* samples to the next step             */
static int      s_pos;
static mvoice_t s_bass, s_lead;
static int32_t  s_kick_t = -1, s_snare_t = -1, s_hat_t = -1;
static uint32_t s_kick_ph;
static int32_t  s_nlp, s_nhp;

void mh_music(int theme, bool boss)
{
    if (theme == s_theme && boss == s_boss) return;
    s_theme = theme;
    s_boss = boss;
    s_pos = 0;
    s_step_left = 0;
    s_bass.amp_t = s_lead.amp_t = 0;
}

static void music_step(void)
{
    const theme_t *th = theme_of(s_theme);
    if (!th) return;
    int b = th->bass[s_pos], l = th->lead[s_pos], d = th->drum[s_pos];
    int shift = s_boss ? -12 : 0;
    if (b > 0) {
        int n = b + shift;
        if (n < 12) n = 12;
        s_bass.step = s_note_step[n];
        s_bass.amp = 7000;
        s_bass.t = 0;
    }
    if (l == 0) s_lead.amp_t = 0;
    else if (l > 1) {
        s_lead.step = s_note_step[l + (s_boss ? -5 : 0)];
        s_lead.amp_t = 3600;
        s_lead.t = 0;
    }
    if (d & 1) { s_kick_t = 0; s_kick_ph = 0; }
    if (d & 2) s_snare_t = 0;
    if (d & 4) s_hat_t = 0;
    s_pos = (s_pos + 1) % th->n;
}

static int32_t music_sample(void)
{
    const theme_t *th = theme_of(s_theme);
    if (!th || !s_mus_on) return 0;
    if (s_step_left <= 0) {
        music_step();
        int ms = s_boss ? th->ms * 4 / 5 : th->ms;
        s_step_left = ms * RATE / 1000;
    }
    s_step_left--;
    int32_t mix = 0;
    /* bass: a sine with its octave, decaying over a step and a half */
    if (s_bass.amp > 16) {
        s_bass.ph += s_bass.step;
        int32_t s = s_sin[(s_bass.ph >> 16) & 255] + (s_sin[(s_bass.ph >> 15) & 255] >> 2);
        mix += (s * s_bass.amp) >> 15;
        if ((++s_bass.t & 15) == 0) s_bass.amp -= s_bass.amp >> 5;
    }
    /* lead: a soft square with vibrato */
    s_lead.amp += (s_lead.amp_t - s_lead.amp) >> 6;
    if (s_lead.amp > 16) {
        s_lead.t++;
        int32_t vib = s_sin[(s_lead.t * 6 / 16) & 255];
        s_lead.ph += s_lead.step + (uint32_t)(((int32_t)(s_lead.step >> 8) * vib) >> 13);
        int32_t s = s_sin[(s_lead.ph >> 16) & 255];
        s = s > 0 ? 12000 + (s >> 2) : -12000 + (s >> 2);
        mix += (s * s_lead.amp) >> 15;
    }
    uint32_t nz = rnd();
    int32_t n = (int32_t)(nz & 0xFFFF) - 32768;
    if (s_kick_t >= 0) {
        int32_t k = 1600 - s_kick_t;            /* 100 ms */
        if (k <= 0) s_kick_t = -1;
        else {
            s_kick_ph += hz_step(45 + k / 16);
            mix += (s_sin[(s_kick_ph >> 16) & 255] * (k * 7)) >> 15;
            s_kick_t++;
        }
    }
    if (s_snare_t >= 0) {
        int32_t k = 1400 - s_snare_t;
        if (k <= 0) s_snare_t = -1;
        else {
            int32_t hp = n - s_nhp;
            s_nhp += (n - s_nhp) >> 3;
            mix += (hp * k) >> 14;
            s_snare_t++;
        }
    }
    if (s_hat_t >= 0) {
        int32_t k = 400 - s_hat_t;
        if (k <= 0) s_hat_t = -1;
        else {
            int32_t hp = n - s_nlp;
            s_nlp += (n - s_nlp) >> 1;
            mix += (hp * k) >> 15;
            s_hat_t++;
        }
    }
    return mix;
}

/* ---- the machine ---- */

void mh_audio_open(void)
{
    tables();
    memset(s_v, 0, sizeof(s_v));
    s_open = aos_hal_spk_open(RATE);
    if (!s_open) aos_hal_log("mhop", "no speaker: beeps");
}

void mh_audio_close(void)
{
    if (s_open) aos_hal_spk_close();
    s_open = false;
    s_theme = MUS_NONE;
}

void mh_audio_enable(bool sfx, bool music)
{
    s_sfx = sfx;
    s_mus_on = music;
}

void mh_audio_tick(void)
{
    if (!s_open) return;
    int queued = aos_hal_spk_queued();
    int need = TARGET - queued;
    if (need <= 0) return;
    static int16_t buf[TARGET];
    if (need > TARGET) need = TARGET;
    for (int i = 0; i < need; i++) {
        int32_t mix = music_sample();
        for (int k = 0; k < VOICES; k++) {
            voice_t *v = &s_v[k];
            if (v->kind == V_OFF) continue;
            if (v->t < 0) {
                v->t++;
                continue;
            }
            mix += voice_sample(v);
        }
        if (mix > 32767) mix = 32767;
        if (mix < -32767) mix = -32767;
        buf[i] = (int16_t)mix;
    }
    aos_hal_spk_write(buf, need);
}
