#include "af_dsp.h"

#include <math.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Pitch detection: NSDF (McLeod)                                              */
/* -------------------------------------------------------------------------- */

/* Why NSDF and not plain autocorrelation, which is half the code:
 *
 * measured with synthetic tones of known frequency (tools/mic_harness.c), the
 * raw autocorrelation gets 82.41 Hz right but returns 146.8 for 440 (= /3) and
 * 149.5 for 1046.5 (= /7). It is not noise: the ACF has equally high peaks at
 * ALL multiples of the period, so keeping the highest is a coin toss, and the
 * more multiples fit in the search range —that is, the higher the tone— the
 * more often it comes out wrong.
 *
 * Normalising by the energy of the two windows flattens them, and afterwards
 * the FIRST peak passing the threshold is chosen instead of the highest: the
 * true period is the shortest of the ones that work.
 */
#define AF_CLARITY_MIN  0.35f       /* below this it is noise, not a note */
#define AF_PEAK_K       0.85f       /* how close to the maximum counts as a peak */

/* Searches 'v' for the FIRST NSDF peak passing the threshold, between two lags.
 *
 * It leaves the curve in nsdf[] indexed by lag and returns the peak's lag, or
 * 0 if there is none clear. The energy is kept up to date instead of being
 * recomputed:
 *     m(t) = A(t) + B(t)
 *     A(t) = sum of v[i]^2, i in [0, n-t)  ->  A(t+1) = A(t) - v[n-t-1]^2
 *     B(t) = sum of v[j]^2, j in [t, n)    ->  B(t+1) = B(t) - v[t]^2
 * that is, two subtractions per lag instead of walking the window again.
 */
static int nsdf_buscar(const float *v, int n, int lag_min, int lag_max,
                       float *nsdf, float *pico_valor)
{
    if (lag_min < 1 || lag_max >= n || lag_min > lag_max) {
        return 0;
    }

    float A = 0.0f, B = 0.0f;
    for (int i = 0; i < n - lag_min; i++) A += v[i] * v[i];
    for (int j = lag_min; j < n; j++)     B += v[j] * v[j];

    float mejor = 0.0f;
    for (int lag = lag_min; lag <= lag_max; lag++) {
        const int hasta = n - lag;
        const float *a = v;
        const float *b = v + lag;
        float r = 0.0f;
        for (int i = 0; i < hasta; i++) {
            r += a[i] * b[i];
        }
        float m = A + B;
        nsdf[lag] = (m > 0.0f) ? (2.0f * r / m) : 0.0f;
        if (nsdf[lag] > mejor) {
            mejor = nsdf[lag];
        }
        A -= v[hasta - 1] * v[hasta - 1];
        B -= v[lag] * v[lag];
    }
    if (mejor < AF_CLARITY_MIN) {
        return 0;
    }

    /* The FIRST peak passing the threshold, not the highest: the true period
     * is the shortest of the ones that work. Keeping the maximum is what makes
     * an autocorrelation return the frequency divided by three. */
    const float umbral = AF_PEAK_K * mejor;
    for (int lag = lag_min + 1; lag < lag_max; lag++) {
        if (nsdf[lag] > umbral &&
            nsdf[lag] >= nsdf[lag - 1] && nsdf[lag] >= nsdf[lag + 1]) {
            if (pico_valor) *pico_valor = nsdf[lag];
            return lag;
        }
    }
    return 0;
}

/* Refines the lag with a parabola through the three points around the peak. It
 * is not a nicety: at 1 kHz with a 16 kHz sample rate, each sample of lag is
 * nearly 70 cents, and without this the tuner does not tune. */
static float interpolar(const float *nsdf, int pico)
{
    float y0 = nsdf[pico - 1], y1 = nsdf[pico], y2 = nsdf[pico + 1];
    float denom = 2.0f * (2.0f * y1 - y0 - y2);
    float ajuste = (denom != 0.0f) ? (y2 - y0) / denom : 0.0f;
    if (ajuste < -1.0f) ajuste = -1.0f;
    if (ajuste >  1.0f) ajuste =  1.0f;
    return (float)pico + ajuste;
}

/* Two-stage pitch estimation.
 *
 * MEASURED ON THE BOARD, which is where this design came from: a single
 * full-rate search over the musical range's 521 lags took 284 ms per analysis;
 * fixing the inner loop brought it down to 130, and that still eats 130 ms of
 * the LVGL thread out of every 200. The cost is (lags x samples), so no tuning
 * saves it: you have to search in fewer places.
 *
 * So the search is split in two, along the line the problem splits along by
 * itself:
 *
 *   HIGH (500..1300 Hz): at full rate, but they are short lags —12 to 32 at
 *       16 kHz—, that is, 21 lags and no more.
 *   LOW (30..600 Hz): 500 lags, and there decimation is needed. With the
 *       window averaged four at a time it becomes 512 samples at 4 kHz and 127
 *       lags: sixteen times less work. Afterwards the lag found is refined by
 *       searching for the maximum at full rate in a little window of +-6, which
 *       is another 13 lags.
 *
 * The HIGH one is tried first and wins if it finds anything: the true period is
 * the shortest of the ones that work. Done the other way round, a 1000 Hz tone
 * would give an equally good peak in the low stage —at twice the period— and
 * 500 Hz would come out, which is the same old octave error in different
 * clothes. The two bands overlap between 500 and 600 Hz so no gap is left.
 */
af_pitch_t af_pitch(const int16_t *x, int n, uint32_t rate,
                    float *scratch, int scratch_len)
{
    af_pitch_t out = { 0.0f, 0.0f };
    if (!x || !scratch || n < 256 || rate == 0) {
        return out;
    }
    if (scratch_len < AF_SCRATCH_FLOATS(n, rate)) {
        return out;                     /* the caller came up short */
    }

    const int nd    = n / AF_DEC;
    const int rated = (int)rate / AF_DEC;

    float *xf   = scratch;                  /* window at full rate         */
    float *xd   = scratch + n;              /* decimated window            */
    float *nsdf = scratch + n + nd;         /* curve, indexed by lag       */

    /* Centre and normalise ONCE. Removing the DC matters: the ES8311 does not
     * deliver it centred and an offset feeds the NSDF a constant correlation
     * that flattens the contrast between peaks. */
    float mean = 0.0f;
    for (int i = 0; i < n; i++) {
        mean += (float)x[i];
    }
    mean /= (float)n;
    for (int i = 0; i < n; i++) {
        xf[i] = ((float)x[i] - mean) * (1.0f / 32768.0f);
    }

    /* ---- high stage, at full rate ---- */
    int lag_alto_min = (int)((float)rate / AF_HZ_MAX);
    int lag_alto_max = (int)((float)rate / AF_HZ_CORTE_ALTO);
    if (lag_alto_min < 2) lag_alto_min = 2;
    if (lag_alto_max > n / 2) lag_alto_max = n / 2;

    float valor = 0.0f;
    int pico = nsdf_buscar(xf, n, lag_alto_min, lag_alto_max, nsdf, &valor);
    if (pico > 0) {
        out.hz      = (float)rate / interpolar(nsdf, pico);
        out.clarity = valor > 1.0f ? 1.0f : valor;
        if (out.hz < AF_HZ_MIN || out.hz > AF_HZ_MAX) {
            out.hz = 0.0f;
        }
        return out;
    }

    /* ---- low stage, over the decimated window ---- */
    /* Averaging AF_DEC at a time acts as a poor but sufficient anti-alias
     * filter: what is being looked for here is below 600 Hz and the high stage
     * has already dealt with the rest. */
    for (int i = 0; i < nd; i++) {
        float suma = 0.0f;
        for (int k = 0; k < AF_DEC; k++) {
            suma += xf[i * AF_DEC + k];
        }
        xd[i] = suma * (1.0f / (float)AF_DEC);
    }

    int lag_bajo_min = (int)((float)rated / AF_HZ_CORTE_BAJO);
    int lag_bajo_max = (int)((float)rated / AF_HZ_MIN);
    if (lag_bajo_min < 2)      lag_bajo_min = 2;
    if (lag_bajo_max > nd / 2) lag_bajo_max = nd / 2;

    int pico_d = nsdf_buscar(xd, nd, lag_bajo_min, lag_bajo_max, nsdf, &valor);
    if (pico_d <= 0) {
        return out;
    }

    /* ---- refine the lag at full rate ---- */
    int centro = pico_d * AF_DEC;
    int fino_min = centro - AF_DEC - 2;
    int fino_max = centro + AF_DEC + 2;
    if (fino_min < 2)         fino_min = 2;
    if (fino_max > n / 2 - 1) fino_max = n / 2 - 1;
    if (fino_min >= fino_max) {
        return out;
    }

    /* Here we DO look for the maximum and not the first peak: the octave
     * decision was already made by the coarse stage, this only locates the
     * crest precisely. */
    float ignorar = 0.0f;
    nsdf_buscar(xf, n, fino_min, fino_max, nsdf, &ignorar);
    int mejor_lag = fino_min + 1;
    for (int lag = fino_min + 1; lag < fino_max; lag++) {
        if (nsdf[lag] > nsdf[mejor_lag]) {
            mejor_lag = lag;
        }
    }
    if (mejor_lag <= fino_min || mejor_lag >= fino_max) {
        return out;
    }

    out.hz      = (float)rate / interpolar(nsdf, mejor_lag);
    out.clarity = nsdf[mejor_lag] > 1.0f ? 1.0f : nsdf[mejor_lag];
    if (out.hz < AF_HZ_MIN || out.hz > AF_HZ_MAX) {
        out.hz = 0.0f;
    }
    return out;
}

/* -------------------------------------------------------------------------- */
/* Nearest note                                                                */
/* -------------------------------------------------------------------------- */

static const char *NOMBRES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

float af_note_hz(int midi, float a4)
{
    return a4 * powf(2.0f, (float)(midi - 69) / 12.0f);
}

void af_note_from_hz(float hz, float a4, af_note_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->name = "--";
    if (hz <= 0.0f || a4 <= 0.0f) {
        return;
    }

    float exacto = 69.0f + 12.0f * log2f(hz / a4);
    int   midi   = (int)floorf(exacto + 0.5f);
    if (midi < 0)   midi = 0;
    if (midi > 127) midi = 127;

    out->midi   = midi;
    out->name   = NOMBRES[midi % 12];
    out->octave = midi / 12 - 1;
    out->ref_hz = af_note_hz(midi, a4);
    out->cents  = 1200.0f * log2f(hz / out->ref_hz);
}

/* -------------------------------------------------------------------------- */
/* A weighting                                                                 */
/* -------------------------------------------------------------------------- */

/* The standard's four poles. The numerator is s^4, split s^2 at a time between
 * the two double-pole sections. */
#define AF_F1   20.598997f
#define AF_F2   107.65265f
#define AF_F3   737.86223f
#define AF_F4   12194.217f

/* Bilinear transform of a second-order section given in s:
 *   N(s) = n2 s^2 + n1 s + n0     D(s) = d2 s^2 + d1 s + d0
 * with s -> K (1 - z^-1) / (1 + z^-1). Without prewarping: the poles that
 * matter are far below half the sample rate except the one at 12194 Hz, which
 * at 16 kHz stays within the microphone's own error anyway. */
static void bilinear(af_biquad_t *bq, float K,
                     float n2, float n1, float n0,
                     float d2, float d1, float d0)
{
    float KK = K * K;
    float b0 = n2 * KK + n1 * K + n0;
    float b1 = 2.0f * (n0 - n2 * KK);
    float b2 = n2 * KK - n1 * K + n0;
    float a0 = d2 * KK + d1 * K + d0;
    float a1 = 2.0f * (d0 - d2 * KK);
    float a2 = d2 * KK - d1 * K + d0;

    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
    bq->z1 = bq->z2 = 0.0f;
}

/* |H(e^jw)| of one section, for normalising the cascade at 1 kHz. */
static float biquad_mag(const af_biquad_t *bq, float w)
{
    float c1 = cosf(w),      s1 = sinf(w);
    float c2 = cosf(2.0f * w), s2 = sinf(2.0f * w);

    float nr = bq->b0 + bq->b1 * c1 + bq->b2 * c2;
    float ni =        -bq->b1 * s1 - bq->b2 * s2;
    float dr = 1.0f   + bq->a1 * c1 + bq->a2 * c2;
    float di =        -bq->a1 * s1 - bq->a2 * s2;

    float den = dr * dr + di * di;
    if (den <= 0.0f) {
        return 0.0f;
    }
    return sqrtf((nr * nr + ni * ni) / den);
}

void af_aweight_init(af_aweight_t *w, uint32_t rate)
{
    if (!w || rate == 0) {
        return;
    }
    memset(w, 0, sizeof(*w));
    w->rate = rate;

    const float K  = 2.0f * (float)rate;
    const float w1 = 2.0f * (float)M_PI * AF_F1;
    const float w2 = 2.0f * (float)M_PI * AF_F2;
    const float w3 = 2.0f * (float)M_PI * AF_F3;
    const float w4 = 2.0f * (float)M_PI * AF_F4;

    /* s^2 / (s + w1)^2 */
    bilinear(&w->s[0], K, 1.0f, 0.0f, 0.0f, 1.0f, 2.0f * w1, w1 * w1);
    /* s^2 / (s + w4)^2 */
    bilinear(&w->s[1], K, 1.0f, 0.0f, 0.0f, 1.0f, 2.0f * w4, w4 * w4);
    /* 1 / ((s + w2)(s + w3)) */
    bilinear(&w->s[2], K, 0.0f, 0.0f, 1.0f, 1.0f, w2 + w3, w2 * w3);

    float wd = 2.0f * (float)M_PI * 1000.0f / (float)rate;
    float mag = biquad_mag(&w->s[0], wd) *
                biquad_mag(&w->s[1], wd) *
                biquad_mag(&w->s[2], wd);
    w->gain = (mag > 0.0f) ? (1.0f / mag) : 1.0f;
}

void af_aweight_reset(af_aweight_t *w)
{
    if (!w) {
        return;
    }
    for (int i = 0; i < 3; i++) {
        w->s[i].z1 = w->s[i].z2 = 0.0f;
    }
}

/* Transposed direct form II: the one that stays tidiest in floating point */
static inline float biquad_run(af_biquad_t *bq, float x)
{
    float y = bq->b0 * x + bq->z1;
    bq->z1  = bq->b1 * x - bq->a1 * y + bq->z2;
    bq->z2  = bq->b2 * x - bq->a2 * y;
    return y;
}

float af_aweight_run(af_aweight_t *w, float x)
{
    if (!w) {
        return 0.0f;
    }
    float y = x;
    for (int i = 0; i < 3; i++) {
        y = biquad_run(&w->s[i], y);
    }
    return y * w->gain;
}

float af_aweight_block_dbfs(af_aweight_t *w, const int16_t *x, int n)
{
    if (!w || !x || n <= 0) {
        return -120.0f;
    }
    /* Accumulated in float and with the samples already normalised to full
     * scale. In double the sum would be more exact and would be of no use: the
     * ESP32-S3 has a single-precision FPU, so every double means calls into
     * the software emulation —and those routines are not even in the symbol
     * table the apps see—. With values near 1.0 the accumulated error over a
     * few thousand samples stays in the order of 0.001 dB. */
    float suma = 0.0f;
    for (int i = 0; i < n; i++) {
        float y = af_aweight_run(w, (float)x[i] / 32768.0f);
        suma += y * y;
    }
    float rms = sqrtf(suma / (float)n);
    if (rms < 3e-9f) {
        return -120.0f;
    }
    return 20.0f * log10f(rms);
}
