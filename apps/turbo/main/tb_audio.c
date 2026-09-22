/*
 * TURBO - the sound: a small synthesiser on the streaming speaker
 *
 * The same machine as Golf's (apps/golf/main/gf_audio.c): once an app opens
 * aos_hal_spk_*, aos_hal_beep() goes quiet, so the app owns all its sound,
 * made of integer voices at 16 kHz topped up from the frame timer. On top of
 * the one-shot voices runs the ENGINE: two detuned pulse waves an octave
 * apart whose pitch follows the revs, a little noise for the exhaust, and
 * under it the tyres (a squeal when the car slides or brakes hard), the
 * gravel off the road and the wind, all set once a frame by
 * tb_audio_engine().
 */
#include "tb_audio.h"

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
static bool     s_open;
static uint32_t s_rng = 0x1234567u;
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

/* the engine and the road, set per frame */
static int32_t  s_e_step, s_e_step_t;         /* 16.16 over the sine table   */
static int32_t  s_e_amp, s_e_amp_t;
static int32_t  s_sq_amp, s_sq_amp_t;         /* tyre squeal                 */
static int32_t  s_gr_amp, s_gr_amp_t;         /* gravel                      */
static int32_t  s_wd_amp, s_wd_amp_t;         /* wind                        */
static uint32_t s_e_ph1, s_e_ph2, s_sq_ph;
static int32_t  s_nlp, s_nlp2, s_nhp;
static bool     s_engine;

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

void tb_audio_tone(int hz, int ms)
{
    if (!s_open) {
        aos_hal_beep(hz, ms);
        return;
    }
    tone(hz, hz, ms + 20, 6000, 3, V_TONE);
}

void tb_snd(int id)
{
    if (!s_open) {
        switch (id) {
        case SND_TICK:  aos_hal_beep(900, 12); break;
        case SND_COUNT: aos_hal_beep(880, 120); break;
        case SND_GO:    aos_hal_beep(1760, 300); break;
        case SND_CHECKPOINT: aos_hal_beep(1319, 80); aos_hal_beep(1760, 140); break;
        case SND_CRASH: aos_hal_beep(140, 200); break;
        case SND_FINISH: aos_hal_beep(1047, 90); aos_hal_beep(1319, 90); aos_hal_beep(1568, 200); break;
        case SND_TIMEUP: aos_hal_beep(392, 200); aos_hal_beep(262, 400); break;
        case SND_LOW:   aos_hal_beep(1200, 30); break;
        case SND_BUY:   aos_hal_beep(1319, 60); aos_hal_beep(1760, 90); break;
        case SND_NO:    aos_hal_beep(300, 80); break;
        default: break;
        }
        return;
    }
    switch (id) {
    case SND_TICK:
        tone(1400, 1200, 18, 5000, 1, V_TONE);
        break;
    case SND_COUNT:
        tone(880, 880, 160, 8000, 2, V_TONE);
        break;
    case SND_GO:
        tone(1760, 1760, 420, 8000, 2, V_TONE);
        tone(880, 880, 420, 5000, 2, V_TONE);
        break;
    case SND_CHECKPOINT:
        tone(1319, 1319, 110, 7000, 3, V_TONE);
        tone(1760, 1760, 260, 7000, 90, V_TONE);
        tone(2637, 2637, 320, 3000, 150, V_TONE);
        break;
    case SND_CRASH:
        noise(420, 26000, 0, 230, false);
        noise(700, 14000, 20, 60, false);
        tone(120, 45, 300, 16000, 0, V_TONE);
        tone(2900, 2500, 240, 3000, 30, V_TONE);    /* glass and metal */
        break;
    case SND_BUMP:
        noise(60, 16000, 0, 200, false);
        tone(160, 90, 90, 12000, 0, V_TONE);
        break;
    case SND_PASS:
        noise(260, 7000, 120, 90, true);
        break;
    case SND_GEAR:
        tone(90, 60, 60, 5000, 0, V_TONE);
        break;
    case SND_FINISH:
        tone(1047, 1047, 130, 7000, 3, V_TONE);
        tone(1319, 1319, 250, 7000, 120, V_TONE);
        tone(1568, 1568, 380, 7000, 240, V_TONE);
        tone(2093, 2093, 700, 6000, 380, V_TONE);
        break;
    case SND_TIMEUP:
        tone(523, 494, 260, 8000, 3, V_TONE);
        tone(392, 370, 360, 8000, 220, V_TONE);
        tone(262, 247, 800, 8000, 480, V_TONE);
        break;
    case SND_LOW:
        tone(1500, 1500, 40, 6000, 1, V_TONE);
        break;
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

void tb_audio_engine(bool on, float rpm, float throttle, float speed01, float squeal, float gravel)
{
    s_engine = on;
    if (!on) {
        s_e_amp_t = s_sq_amp_t = s_gr_amp_t = s_wd_amp_t = 0;
        return;
    }
    if (rpm < 0) rpm = 0;
    if (rpm > 1) rpm = 1;
    /* 38 Hz at idle to 190 Hz at the limiter: the firing note of a V8-ish */
    int hz = 38 + (int)(rpm * 152.0f);
    s_e_step_t = (int32_t)hz_step(hz);
    s_e_amp_t = 3200 + (int32_t)(throttle * 3200.0f) + (int32_t)(rpm * 1600.0f);
    s_sq_amp_t = squeal > 0.05f ? (int32_t)(squeal * 5000.0f) : 0;
    s_gr_amp_t = (int32_t)(gravel * 9000.0f);
    s_wd_amp_t = (int32_t)(speed01 * speed01 * 2600.0f);
}

void tb_audio_open(void)
{
    sin_init();
    memset(s_v, 0, sizeof(s_v));
    s_open = aos_hal_spk_open(RATE);
    s_e_step = (int32_t)hz_step(40);
    if (!s_open) aos_hal_log("turbo", "no speaker: beeps");
}

void tb_audio_close(void)
{
    if (s_open) aos_hal_spk_close();
    s_open = false;
    s_engine = false;
}

bool tb_audio_is_open(void)
{
    return s_open;
}

void tb_audio_tick(void)
{
    if (!s_open) return;
    int queued = aos_hal_spk_queued();
    int need = TARGET - queued;
    if (need <= 0) return;
    static int16_t buf[TARGET];
    if (need > TARGET) need = TARGET;
    for (int i = 0; i < need; i++) {
        /* glide the engine's parameters, every 32 samples */
        if ((i & 31) == 0) {
            s_e_step += (s_e_step_t - s_e_step) >> 3;
            s_e_amp += (s_e_amp_t - s_e_amp) >> 3;
            s_sq_amp += (s_sq_amp_t - s_sq_amp) >> 3;
            s_gr_amp += (s_gr_amp_t - s_gr_amp) >> 3;
            s_wd_amp += (s_wd_amp_t - s_wd_amp) >> 4;
        }
        int32_t mix = 0;
        for (int k = 0; k < VOICES; k++) {
            voice_t *v = &s_v[k];
            if (v->kind == V_OFF) continue;
            if (v->t < 0) {
                v->t++;
                continue;
            }
            mix += voice_sample(v);
        }
        if (s_e_amp > 8 || s_wd_amp > 8 || s_gr_amp > 8 || s_sq_amp > 8) {
            /* the engine: a pulse (sign of the sine, 25 % duty) and its octave, detuned */
            s_e_ph1 += (uint32_t)s_e_step;
            s_e_ph2 += (uint32_t)s_e_step * 2u + 900u;
            int32_t p1 = ((s_e_ph1 >> 16) & 255) < 64 ? 12000 : -4000;
            int32_t p2 = s_sin[(s_e_ph2 >> 16) & 255] >> 1;
            uint32_t nz = rnd();
            int32_t n = (int32_t)(nz & 0xFFFF) - 32768;
            s_nlp += (n - s_nlp) >> 2;                       /* exhaust hiss */
            int32_t eng = p1 + p2 + (s_nlp >> 3);
            mix += (eng * s_e_amp) >> 15;
            /* wind and gravel: low noise, gravel rougher */
            s_nlp2 += (n - s_nlp2) >> 5;
            mix += (s_nlp2 * s_wd_amp) >> 14;
            if (s_gr_amp > 8) {
                int32_t g = (nz & 0x70000) ? s_nlp : -s_nlp;
                mix += (g * s_gr_amp) >> 15;
            }
            if (s_sq_amp > 8) {
                /* the squeal: a wobbling high tone in noise */
                s_sq_ph += hz_step(1900 + (int)((nz >> 20) & 255));
                int32_t hp = n - s_nhp;
                s_nhp += (n - s_nhp) >> 3;
                int32_t sq = s_sin[(s_sq_ph >> 16) & 255] + (hp >> 2);
                mix += (sq * s_sq_amp) >> 15;
            }
        }
        if (mix > 32767) mix = 32767;
        if (mix < -32767) mix = -32767;
        buf[i] = (int16_t)mix;
    }
    aos_hal_spk_write(buf, need);
}
