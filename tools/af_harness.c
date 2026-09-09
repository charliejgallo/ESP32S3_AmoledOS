/* Test bench for the tuner's maths (#48). No screen and no board.
 *
 *   cc -std=c11 -O2 -I apps/afinador/main -o /tmp/af_harness \
 *      tools/af_harness.c apps/afinador/main/af_dsp.c -lm
 *   /tmp/af_harness
 *
 * It tests the two things that can be wrong without it showing on the screen:
 * that the pitch detection does not get the octave wrong, and that the A
 * weighting is the standard's and not a curve that resembles it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "af_dsp.h"

#define RATE    16000
#define N       2048

static int fallos;

static void check(const char *que, int ok)
{
    printf("%-56s %s\n", que, ok ? "ok" : "FAIL");
    if (!ok) fallos++;
}

/* An instrument note: a fundamental with harmonics and a little noise. A pure
 * sine would be easier than what the app will really see. */
static void sintetizar(int16_t *x, int n, float hz, float amp, unsigned semilla)
{
    srand(semilla);
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)RATE;
        float v = 1.00f * sinf(2.0f * (float)M_PI * hz * t)
                + 0.50f * sinf(4.0f * (float)M_PI * hz * t + 0.7f)
                + 0.25f * sinf(6.0f * (float)M_PI * hz * t + 1.9f)
                + 0.12f * sinf(8.0f * (float)M_PI * hz * t);
        v += ((float)(rand() % 1000) - 500.0f) / 5000.0f;
        int s = (int)(v * amp);
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        x[i] = (int16_t)s;
    }
}

static void probar_tono(void)
{
    static int16_t x[N];
    static float scratch[AF_SCRATCH_FLOATS(N, RATE)];

    /* The guitar's six open strings, the bass's four and two high ones */
    const float casos[] = {
        41.20f, 55.00f, 82.41f, 110.00f, 146.83f, 196.00f,
        246.94f, 329.63f, 440.00f, 587.33f, 880.00f, 1046.50f
    };
    printf("\n-- pitch detection (window from %d to %d Hz) --\n", N, RATE);
    float peor = 0.0f;
    for (unsigned i = 0; i < sizeof(casos) / sizeof(casos[0]); i++) {
        sintetizar(x, N, casos[i], 8000.0f, 1234 + i);
        af_pitch_t p = af_pitch(x, N, RATE, scratch, (int)(sizeof(scratch) / sizeof(scratch[0])));
        float cents = p.hz > 0 ? 1200.0f * log2f(p.hz / casos[i]) : 9999.0f;
        if (fabsf(cents) > peor && fabsf(cents) < 1000.0f) peor = fabsf(cents);
        printf("   %8.2f Hz -> %8.2f Hz  (%+6.2f cents, claridad %.2f)\n",
               casos[i], p.hz, cents, p.clarity);
        char nombre[64];
        snprintf(nombre, sizeof(nombre), "gets %.2f Hz right within 5 cents", casos[i]);
        check(nombre, fabsf(cents) < 5.0f);
    }
    printf("   peor error: %.2f cents\n", peor);

    /* Silence and noise must not invent a note */
    memset(x, 0, sizeof(x));
    af_pitch_t p = af_pitch(x, N, RATE, scratch, (int)(sizeof(scratch) / sizeof(scratch[0])));
    check("silence gives no note", p.hz == 0.0f);

    srand(7);
    for (int i = 0; i < N; i++) x[i] = (int16_t)((rand() % 16000) - 8000);
    p = af_pitch(x, N, RATE, scratch, (int)(sizeof(scratch) / sizeof(scratch[0])));
    printf("   ruido blanco -> %.1f Hz (claridad %.2f)\n", p.hz, p.clarity);
    check("white noise gives no clear note", p.clarity < 0.7f);
}

static void probar_nota(void)
{
    af_note_t n;
    printf("\n-- nearest note (A4 = 440) --\n");

    af_note_from_hz(440.0f, 440.0f, &n);
    printf("   440,00 Hz -> %s%d %+.1f cents\n", n.name, n.octave, n.cents);
    check("440 Hz is A4 exactly", !strcmp(n.name, "A") && n.octave == 4 &&
                                fabsf(n.cents) < 0.01f);

    af_note_from_hz(82.41f, 440.0f, &n);
    printf("   82,41 Hz -> %s%d %+.1f cents\n", n.name, n.octave, n.cents);
    check("82.41 Hz is E2 (the open sixth string)", !strcmp(n.name, "E") && n.octave == 2);

    af_note_from_hz(261.63f, 440.0f, &n);
    check("261.63 Hz is C4", !strcmp(n.name, "C") && n.octave == 4);

    af_note_from_hz(440.0f * powf(2.0f, 25.0f / 1200.0f), 440.0f, &n);
    printf("   A4 +25 cents -> %s%d %+.1f cents\n", n.name, n.octave, n.cents);
    check("25 cents up is still A4", !strcmp(n.name, "A") &&
                                             fabsf(n.cents - 25.0f) < 0.1f);

    af_note_from_hz(440.0f * powf(2.0f, 60.0f / 1200.0f), 440.0f, &n);
    printf("   A4 +60 cents -> %s%d %+.1f cents\n", n.name, n.octave, n.cents);
    check("60 cents up is already A#4 from below", !strcmp(n.name, "A#") &&
                                                  n.cents < 0.0f);

    /* A configurable A4, which is what an orchestra at 442 asks for */
    af_note_from_hz(442.0f, 442.0f, &n);
    check("with A4=442, 442 Hz lands exactly", !strcmp(n.name, "A") &&
                                         fabsf(n.cents) < 0.01f);
}

/* Measures the cascade's response at one frequency, against the unweighted
 * signal: what it returns is the curve's gain in dB. */
static float respuesta(uint32_t rate, float hz)
{
    static int16_t x[96000];
    af_aweight_t w;
    af_aweight_init(&w, rate);

    int n = (int)rate / 2;
    for (int k = 0; k < n; k++) {
        x[k] = (int16_t)(8000.0f * sinf(2.0f * (float)M_PI * hz *
                                        (float)k / (float)rate));
    }
    af_aweight_reset(&w);
    /* Warm the filter up WITH THE SAME SCALE af_aweight_block_dbfs uses (a
     * sample normalised to full scale). Feeding it the raw samples leaves the
     * high-pass sections loaded with a transient 90 dB larger, and since the
     * 20 Hz pole decays extremely slowly, that transient eats the whole
     * measurement: the curve comes out flat and ~56 dB too high. */
    int skip = (int)rate / 10;                  /* the filter's warm-up */
    for (int k = 0; k < skip; k++) af_aweight_run(&w, (float)x[k] / 32768.0f);
    float dbfs = af_aweight_block_dbfs(&w, x + skip, n - skip);

    double suma = 0;
    for (int k = skip; k < n; k++) suma += (double)x[k] * x[k];
    float plano = 20.0f * log10f(sqrtf((float)(suma / (n - skip))) / 32768.0f);
    return dbfs - plano;
}

/* IEC 61672-1's table for the A curve, in dB */
static const struct { float hz, db; } NORMA[] = {
    {   31.5f, -39.4f }, {   63.0f, -26.2f }, {  125.0f, -16.1f },
    {  250.0f,  -8.6f }, {  500.0f,  -3.2f }, { 1000.0f,   0.0f },
    { 2000.0f,   1.2f }, { 4000.0f,   1.0f }, { 6300.0f,  -0.1f },
    { 8000.0f,  -1.1f },
};
#define N_NORMA (int)(sizeof(NORMA) / sizeof(NORMA[0]))

static void probar_ponderacion(void)
{
    /* First of all the table that settled the noise meter's capture rate. The
     * A curve's top pole is at 12194 Hz: at a 16 kHz sample rate it lands
     * ABOVE Nyquist and cannot be represented, so the curve folds downwards
     * before reaching the end of the band. It is not a bug in the code: it is
     * what happens to any A weighting sampled that low. */
    const uint32_t rates[] = { 16000, 32000, 48000 };
    printf("\n-- A-weighting: error against the standard, by sample rate --\n");
    printf("%9s", "Hz");
    for (unsigned r = 0; r < 3; r++) printf(" %10u", rates[r]);
    printf("\n");
    for (int i = 0; i < N_NORMA; i++) {
        printf("%9.1f", NORMA[i].hz);
        for (unsigned r = 0; r < 3; r++) {
            if (NORMA[i].hz >= rates[r] / 2.0f) { printf(" %10s", "-"); continue; }
            printf(" %+10.2f", respuesta(rates[r], NORMA[i].hz) - NORMA[i].db);
        }
        printf("\n");
    }

    /* At 16 kHz the standard is required up to 4 kHz, which is as far as it
     * holds. Above that the app has to say the reading is approximate. */
    printf("\n   at 16 kHz (the tuner's):\n");
    for (int i = 0; i < N_NORMA && NORMA[i].hz <= 4000.0f; i++) {
        float err = respuesta(16000, NORMA[i].hz) - NORMA[i].db;
        char nombre[72];
        snprintf(nombre, sizeof(nombre), "16 kHz: %.1f Hz within 1 dB", NORMA[i].hz);
        check(nombre, fabsf(err) < 1.0f);
    }
    check("16 kHz: above 6 kHz the curve no longer holds (expected)",
          fabsf(respuesta(16000, 6300.0f) - (-0.1f)) > 2.0f);

    /* At 32 kHz the whole band is required, which is why the noise meter opens
     * the microphone there and not at 16. */
    printf("   at 32 kHz (the noise meter's):\n");
    for (int i = 0; i < N_NORMA; i++) {
        float err = respuesta(32000, NORMA[i].hz) - NORMA[i].db;
        char nombre[72];
        snprintf(nombre, sizeof(nombre), "32 kHz: %.1f Hz within 1.6 dB", NORMA[i].hz);
        check(nombre, fabsf(err) < 1.6f);
    }

    af_aweight_t w32;
    af_aweight_init(&w32, 32000);
    check("it builds at 32 kHz too", w32.rate == 32000 && w32.gain > 0.0f);
}

int main(void)
{
    probar_tono();
    probar_nota();
    probar_ponderacion();
    printf("\n%s\n", fallos ? "THERE ARE FAILURES" : "all good");
    return fallos ? 1 : 0;
}
