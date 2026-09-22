/*
 * GOLF - the sound: a small synthesiser on the streaming speaker
 *
 * Chatarra found the rule (apps/chatarra/main/ch_sound.c): once an app opens
 * aos_hal_spk_*, every aos_hal_beep() goes quiet, so the app owns ALL of its
 * sound. Here that is a handful of voices of integer arithmetic at 16 kHz,
 * topped up from the frame timer:
 *
 *   ambience   birds now and then and a breath of wind, on the course only
 *   swing      a whoosh on the way down
 *   impact     a click and a thud; woods add a metallic ping
 *   bounce     a soft thump; tree: leaves; splash; the cup's rattle
 *   applause   for a birdie or better
 *   ticks      the interface and the meter
 *
 * No floating point and no multiplies by more than 16 bits in the inner loop.
 * If the speaker cannot be opened (the microphone holds the codec, or an old
 * firmware), gf_snd() falls back to a beep and the game is what it was.
 */
#include "gf_audio.h"

#include "aos_hal.h"

#include <string.h>

#define RATE        16000
#define TARGET      1600            /* samples to keep queued: 100 ms       */
#define VOICES      8

enum { V_OFF = 0, V_NOISE, V_TONE, V_CHIRP, V_CLAPS };

typedef struct {
    uint8_t  kind;
    int32_t  t, dur;                /* samples                               */
    int32_t  amp;                   /* peak, 0..32767                         */
    int32_t  attack;                /* samples                                */
    uint32_t phase, step, step_end; /* 16.16 over a 256-entry sine            */
    int32_t  lp, lp_k;              /* one-pole low-pass, k in 1/256          */
    int32_t  hp;                    /* high-pass state (tone of the noise)    */
    uint8_t  hp_on;
    uint32_t seed;
} voice_t;

static voice_t  s_v[VOICES];
static bool     s_open, s_ambience;
static uint32_t s_rng = 0x1234567u;
static int32_t  s_bird_wait, s_wind_lp, s_wind_amp, s_wind_target;
static int16_t  s_sin[256];
static bool     s_sin_ready;

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static void sin_init(void)
{
    if (s_sin_ready) return;
    /* a parabola approximation per quadrant is plenty for effects */
    for (int i = 0; i < 256; i++) {
        int q = i & 127;
        int x = q * 2 - 127;                    /* -127..127 */
        int y = 32767 - (x * x * 32767) / (127 * 127);
        s_sin[i] = (int16_t)(i < 128 ? y : -y);
    }
    s_sin_ready = true;
}

static voice_t *voice_get(void)
{
    for (int i = 0; i < VOICES; i++) {
        if (s_v[i].kind == V_OFF) return &s_v[i];
    }
    /* all busy: steal the one closest to its end */
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

static uint32_t hz_step(int hz)
{
    return (uint32_t)hz * 1049u;        /* 2^24 / 16000 = 1048.6: 256 << 16 per cycle */
}

static void noise(int ms, int amp, int attack_ms, int lp_k, bool hp)
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
}

static void tone(int hz, int hz_end, int ms, int amp, int attack_ms, uint8_t kind)
{
    voice_t *v = voice_get();
    memset(v, 0, sizeof(*v));
    v->kind = kind;
    v->dur = ms * RATE / 1000;
    v->amp = amp;
    v->attack = attack_ms * RATE / 1000;
    v->step = hz_step(hz);
    v->step_end = hz_step(hz_end);
}

/* --------------------------------------------------------------------------
 * The effects
 * -------------------------------------------------------------------------- */

void gf_audio_tone(int hz, int ms)
{
    if (!s_open) {
        aos_hal_beep(hz, ms);
        return;
    }
    tone(hz, hz, ms + 20, 6000, 3, V_TONE);
}

void gf_snd(int id)
{
    if (!s_open) {
        /* the beeper version of each */
        switch (id) {
        case SND_TICK:    aos_hal_beep(900, 12); break;
        case SND_METER:   aos_hal_beep(700, 10); break;
        case SND_IMPACT:  aos_hal_beep(1500, 25); break;
        case SND_IMPACT_WOOD: aos_hal_beep(1200, 25); break;
        case SND_BOUNCE:  aos_hal_beep(500, 8); break;
        case SND_TREE:    aos_hal_beep(260, 30); break;
        case SND_SPLASH:  aos_hal_beep(180, 120); break;
        case SND_CUP:     aos_hal_beep(1800, 40); aos_hal_beep(2400, 60); break;
        case SND_GOOD:    aos_hal_beep(1319, 70); aos_hal_beep(1568, 70); aos_hal_beep(2093, 120); break;
        case SND_BAD:     aos_hal_beep(392, 120); aos_hal_beep(330, 180); break;
        case SND_BUY:     aos_hal_beep(1319, 60); aos_hal_beep(1760, 90); break;
        case SND_NO:      aos_hal_beep(300, 80); break;
        default: break;
        }
        return;
    }
    switch (id) {
    case SND_TICK:
        tone(1400, 1200, 18, 5000, 1, V_TONE);
        break;
    case SND_METER:
        tone(900, 900, 14, 3500, 1, V_TONE);
        break;
    case SND_WHOOSH:
        /* a filtered noise that swells and dies with the downswing */
        noise(260, 9000, 150, 70, true);
        break;
    case SND_IMPACT:
        noise(25, 20000, 0, 200, true);
        tone(180, 90, 70, 12000, 0, V_TONE);
        break;
    case SND_IMPACT_WOOD:
        noise(20, 20000, 0, 220, true);
        tone(2600, 2450, 180, 7000, 0, V_TONE);         /* the titanium ping */
        tone(160, 80, 80, 12000, 0, V_TONE);
        break;
    case SND_BOUNCE:
        tone(150, 90, 60, 7000, 0, V_TONE);
        noise(18, 4000, 0, 90, false);
        break;
    case SND_TREE:
        noise(320, 9000, 20, 150, true);
        noise(200, 5000, 60, 60, true);
        break;
    case SND_SPLASH:
        noise(90, 18000, 0, 180, false);
        noise(520, 10000, 30, 60, false);
        tone(420, 180, 160, 4000, 0, V_TONE);
        break;
    case SND_CUP:
        tone(2200, 2150, 120, 9000, 0, V_TONE);
        tone(3150, 3100, 90, 6000, 0, V_TONE);
        tone(1600, 1500, 200, 5000, 40, V_TONE);
        noise(160, 5000, 30, 200, true);
        break;
    case SND_GOOD:
        tone(1047, 1047, 110, 7000, 4, V_TONE);
        tone(1319, 1319, 220, 6000, 40, V_TONE);
        tone(1568, 1568, 330, 6000, 90, V_TONE);
        break;
    case SND_BAD:
        tone(392, 370, 180, 7000, 4, V_TONE);
        tone(311, 290, 320, 6000, 120, V_TONE);
        break;
    case SND_APPLAUSE: {
        voice_t *v = voice_get();
        memset(v, 0, sizeof(*v));
        v->kind = V_CLAPS;
        v->dur = 2200 * RATE / 1000;
        v->amp = 11000;
        v->attack = RATE / 5;
        v->lp_k = 190;
        v->hp_on = 1;
        v->seed = rnd() | 1;
        break;
    }
    case SND_BUY:
        tone(1319, 1319, 70, 7000, 2, V_TONE);
        tone(1760, 1760, 160, 7000, 50, V_TONE);
        break;
    case SND_NO:
        tone(300, 260, 120, 7000, 2, V_TONE);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * The mix
 * -------------------------------------------------------------------------- */

static inline int32_t env(const voice_t *v)
{
    /* linear attack, then an exponential-ish decay (squared) to the end */
    if (v->t < v->attack) return v->amp * v->t / (v->attack ? v->attack : 1);
    int32_t left = v->dur - v->t, span = v->dur - v->attack;
    if (span <= 0) return 0;
    int32_t f = (left << 10) / span;                /* 0..1024 */
    return ((v->amp * f) >> 10) * f >> 10;          /* 32-bit all the way */
}

static int32_t voice_sample(voice_t *v)
{
    int32_t e = env(v), s = 0;
    switch (v->kind) {
    case V_NOISE:
    case V_CLAPS: {
        v->seed ^= v->seed << 13;
        v->seed ^= v->seed >> 17;
        v->seed ^= v->seed << 5;
        int32_t n = (int32_t)(v->seed & 0xFFFF) - 32768;
        v->lp += ((n - v->lp) * v->lp_k) >> 8;
        s = v->lp;
        if (v->hp_on) {
            /* take the rumble out: a sharper, airier noise */
            int32_t hp = s - v->hp;
            v->hp += (s - v->hp) >> 4;
            s = hp;
        }
        if (v->kind == V_CLAPS) {
            /* claps: the noise gated by many overlapping short bursts */
            int32_t ph = v->t % 700;
            if (ph == 0 && (rnd() & 3)) v->phase = 280 + (rnd() & 255);
            int32_t g = ph < (int32_t)v->phase ? 1024 - ph * 1024 / (int32_t)v->phase : 0;
            s = (s * g) >> 10;
        }
        break;
    }
    case V_TONE:
    case V_CHIRP: {
        int32_t k = v->dur ? (v->t << 8) / v->dur : 0;
        uint32_t st = v->step + (uint32_t)((((int32_t)v->step_end - (int32_t)v->step) >> 8) * k);
        if (v->kind == V_CHIRP) {
            /* a bird: a fast warble on top of the sweep */
            int32_t w = s_sin[(v->t * 40 / 16) & 255];
            st += (uint32_t)(((int32_t)(st >> 8) * w) >> 10);
        }
        v->phase += st;
        s = s_sin[(v->phase >> 16) & 255];
        break;
    }
    default:
        break;
    }
    v->t++;
    if (v->t >= v->dur) v->kind = V_OFF;
    return (s * e) >> 15;
}

static void ambience_step(int n)
{
    if (!s_ambience) return;
    s_bird_wait -= n;
    if (s_bird_wait <= 0) {
        /* a phrase of two to four chirps */
        int notes = 2 + (int)(rnd() % 3);
        int base = 2600 + (int)(rnd() % 1800);
        for (int i = 0; i < notes; i++) {
            voice_t *v = voice_get();
            memset(v, 0, sizeof(*v));
            v->kind = V_CHIRP;
            v->dur = (60 + (int)(rnd() % 70)) * RATE / 1000;
            v->amp = 1400 + (int)(rnd() % 900);
            v->attack = RATE / 100;
            int f0 = base + (int)(rnd() % 600) - 300;
            v->step = hz_step(f0);
            v->step_end = hz_step(f0 + ((rnd() & 1) ? 700 : -500));
            /* start later: they queue up in time by negative t */
            v->t = -(int32_t)(i * (110 + (int)(rnd() % 60)) * RATE / 1000);
        }
        s_bird_wait = (int32_t)((2 + rnd() % 6) * RATE);
    }
}

void gf_audio_open(void)
{
    sin_init();
    memset(s_v, 0, sizeof(s_v));
    s_open = aos_hal_spk_open(RATE);
    s_bird_wait = RATE * 2;
    s_wind_amp = 0;
    s_wind_target = 900;
    if (!s_open) aos_hal_log("golf", "no speaker: beeps");
}

void gf_audio_close(void)
{
    if (s_open) aos_hal_spk_close();
    s_open = false;
}

void gf_audio_ambience(bool on)
{
    s_ambience = on;
}

void gf_audio_mute(bool mute)
{
    if (mute && s_open) gf_audio_close();
    else if (!mute && !s_open) gf_audio_open();
}

void gf_audio_tick(void)
{
    if (!s_open) return;
    int queued = aos_hal_spk_queued();
    int need = TARGET - queued;
    if (need <= 0) return;
    static int16_t buf[TARGET];
    if (need > TARGET) need = TARGET;
    ambience_step(need);
    for (int i = 0; i < need; i++) {
        int32_t mix = 0;
        for (int k = 0; k < VOICES; k++) {
            voice_t *v = &s_v[k];
            if (v->kind == V_OFF) continue;
            if (v->t < 0) {             /* a bird still waiting its turn */
                v->t++;
                continue;
            }
            mix += voice_sample(v);
        }
        /* the wind: low noise whose level drifts slowly */
        if (s_ambience) {
            if ((i & 1023) == 0) {
                if ((rnd() & 7) == 0) s_wind_target = 300 + (int32_t)(rnd() % 1400);
                s_wind_amp += (s_wind_target - s_wind_amp) >> 3;
            }
            int32_t nz = (int32_t)(rnd() & 0xFFFF) - 32768;
            s_wind_lp += (nz - s_wind_lp) >> 6;
            mix += (s_wind_lp * s_wind_amp) >> 14;
        }
        if (mix > 32767) mix = 32767;
        if (mix < -32767) mix = -32767;
        buf[i] = (int16_t)mix;
    }
    aos_hal_spk_write(buf, need);
}
