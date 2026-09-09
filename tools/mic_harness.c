/* Test bench for the raw microphone, against the simulator's HAL.
 *
 * It draws nothing and does not need the board: it opens the capture, drains
 * the ring the way an app would from its tick and verifies that the sums add
 * up. With MIC_TONE it also estimates the frequency by NSDF, which is the
 * algorithm the tuner (#48) is going to use.
 *
 * Build (with the simulator already built, since that is where hal_sim.c.o
 * comes from):
 *
 *   cc -std=c11 -I components/aos_hal/include $(pkg-config --cflags sdl2) \
 *      -o /tmp/mic_harness tools/mic_harness.c \
 *      sim/build/CMakeFiles/amoledos_sim.dir/hal_sim.c.o \
 *      sim/build/lvgl/lib/liblvgl.a $(pkg-config --libs sdl2) -lm
 *
 *   /tmp/mic_harness              # the ring, the gain, recording and listening
 *   MIC_TONE=440 /tmp/mic_harness # and that the detection gets it right
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include "aos_hal.h"

static int fallos;

static void check(const char *que, int ok)
{
    printf("%-52s %s\n", que, ok ? "ok" : "FALLA");
    if (!ok) {
        fallos++;
    }
}

/* NSDF (McLeod): the autocorrelation normalised by the energy of the two
 * windows being compared.
 *
 * Measured with this very bench: the raw autocorrelation gets 82.41 Hz right
 * but at 440 Hz returns 146.8 (= 440/3) and at 1046.5 returns 149.5 (= /7).
 * The reason is that the ACF has equally high peaks at ALL multiples of the
 * period, so keeping the maximum is a coin toss. The normalisation flattens
 * them and, above all, the FIRST peak passing the threshold is chosen instead
 * of the highest: the true period is the shortest of the ones that work.
 *
 * With parabolic interpolation over the peak, the resolution is good enough
 * for cents.
 */
static float estimar_hz(const int16_t *x, int n, uint32_t rate)
{
    int lag_min = (int)(rate / 1300);
    int lag_max = (int)(rate / 30);
    if (lag_max > n / 2) lag_max = n / 2;

    static float nsdf[4096];
    if (lag_max >= (int)(sizeof(nsdf) / sizeof(nsdf[0]))) {
        lag_max = (int)(sizeof(nsdf) / sizeof(nsdf[0])) - 1;
    }

    float mean = 0;
    for (int i = 0; i < n; i++) mean += x[i];
    mean /= (float)n;

    float mejor = 0;
    for (int lag = lag_min; lag <= lag_max; lag++) {
        float r = 0, m = 0;
        for (int i = 0; i < n - lag; i++) {
            float a = (float)x[i] - mean;
            float b = (float)x[i + lag] - mean;
            r += a * b;
            m += a * a + b * b;
        }
        nsdf[lag] = m > 0 ? 2.0f * r / m : 0.0f;
        if (nsdf[lag] > mejor) mejor = nsdf[lag];
    }
    if (mejor < 0.3f) {
        return 0.0f;            /* there is no tone, it is noise */
    }

    /* the first peak passing the threshold, not the highest */
    const float umbral = 0.85f * mejor;
    int pico = 0;
    for (int lag = lag_min + 1; lag < lag_max; lag++) {
        if (nsdf[lag] > umbral &&
            nsdf[lag] >= nsdf[lag - 1] && nsdf[lag] >= nsdf[lag + 1]) {
            pico = lag;
            break;
        }
    }
    if (!pico) {
        return 0.0f;
    }

    /* parabolic interpolation: without this, at 1 kHz each sample of lag is
     * nearly 70 cents and the tuner is no use for tuning */
    float y0 = nsdf[pico - 1], y1 = nsdf[pico], y2 = nsdf[pico + 1];
    float denom = 2.0f * (2.0f * y1 - y0 - y2);
    float ajuste = denom != 0.0f ? (y2 - y0) / denom : 0.0f;
    return (float)rate / ((float)pico + ajuste);
}

/* The recorder alone, with no raw microphone open: it is the path that already
 * worked before the two shared one capture, and the one that has to be
 * verified as still working after the refactor. */
static void probar_grabadora_sola(void)
{
    aos_rec_status_t rst;

    check("graba sin el microfono crudo abierto",
          aos_hal_rec_start("/tmp/aos_rec_solo.wav", 0));
    usleep(600 * 1000);
    check("el nivel del microfono vive durante la grabacion",
          aos_hal_mic_level() > 0);
    check("la grabacion avanza sola", aos_hal_rec_status(&rst) && rst.bytes > 0);

    uint8_t picos[64];
    int n = aos_hal_rec_peaks(picos, 64);
    printf("   envolvente: %d muestras en ~600 ms (esperado ~12)\n", n);
    check("la envolvente sigue saliendo", n >= 8 && n <= 20);

    check("cierra y hay audio", aos_hal_rec_stop());

    FILE *f = fopen("/tmp/aos_rec_solo.wav", "rb");
    check("el wav quedo en disco", f != NULL);
    if (f) {
        unsigned char h[44];
        size_t leidos = fread(h, 1, sizeof(h), f);
        fseek(f, 0, SEEK_END);
        long tam = ftell(f);
        fclose(f);
        uint32_t data = (uint32_t)h[40] | ((uint32_t)h[41] << 8) |
                        ((uint32_t)h[42] << 16) | ((uint32_t)h[43] << 24);
        printf("   wav: %ld bytes, la cabecera declara %u de audio\n", tam, data);
        check("cabecera RIFF/WAVE", leidos == 44 && !memcmp(h, "RIFF", 4) &&
                                    !memcmp(h + 8, "WAVE", 4));
        check("la cabecera declara el audio que hay", data == (uint32_t)(tam - 44));
        check("entro cerca de medio segundo de audio", data > 12000 && data < 26000);
    }

    aos_mic_status_t st;
    check("tras parar, la captura se apago",
          aos_hal_mic_status(&st) && !st.open);
}

int main(void)
{
    aos_mic_status_t st;

    probar_grabadora_sola();

    check("cerrado: no hay muestras", aos_hal_mic_available() == 0);
    check("cerrado: read devuelve 0", aos_hal_mic_read((int16_t[8]){0}, 8) == 0);

    check("abre a 16 kHz", aos_hal_mic_open(16000));
    check("status dice abierto", aos_hal_mic_status(&st) && st.open);
    check("frecuencia real 16000", st.sample_rate == 16000);

    /* A quarter of a second of capture */
    usleep(250 * 1000);
    int avail = aos_hal_mic_available();
    printf("   esperando ~4000 muestras, hay %d\n", avail);
    check("junto entre 3 y 5 mil muestras", avail > 3000 && avail < 5000);

    static int16_t buf[8192];
    int got = aos_hal_mic_read(buf, 2048);
    check("drena de a 2048", got == 2048);
    check("despues de drenar quedan menos", aos_hal_mic_available() < avail);

    int no_cero = 0;
    for (int i = 0; i < got; i++) if (buf[i]) no_cero++;
    check("las muestras no son todas cero", no_cero > got / 2);

    /* Overflowing the ring on purpose: one second without draining */
    aos_hal_mic_status(&st);
    uint32_t antes = st.dropped;
    usleep(1400 * 1000);
    aos_hal_mic_read(buf, 8192);
    aos_hal_mic_status(&st);
    printf("   perdidas tras 1,4 s sin drenar: %u\n", (unsigned)st.dropped);
    check("el anillo cuenta lo que se perdio", st.dropped > antes);
    check("el anillo no entrega mas de un segundo",
          aos_hal_mic_available() <= 16000);

    /* Gain: steps of 6 dB */
    aos_hal_mic_gain_set(31);
    check("la ganancia se redondea al escalon de 6", aos_hal_mic_gain_get() == 30);
    aos_hal_mic_gain_set(99);
    check("la ganancia se limita a 42", aos_hal_mic_gain_get() == 42);

    /* Recording while listening: both at once */
    check("arranca a grabar con el microfono abierto",
          aos_hal_rec_start("/tmp/aos_mic_harness.wav", 0));
    usleep(300 * 1000);
    aos_rec_status_t rst;
    check("la grabacion avanza", aos_hal_rec_status(&rst) && rst.bytes > 0);
    check("la grabacion usa la frecuencia de la captura", rst.sample_rate == 16000);
    check("el microfono crudo sigue dando muestras", aos_hal_mic_available() > 0);
    aos_hal_rec_stop();
    check("tras parar, la captura sigue viva",
          aos_hal_mic_status(&st) && st.open);

    /* Pitch detection, if it was asked for */
    const char *tono = getenv("MIC_TONE");
    if (tono) {
        usleep(300 * 1000);
        int n = aos_hal_mic_read(buf, 4096);
        float hz = estimar_hz(buf, n, 16000);
        float esperado = (float)atof(tono);
        printf("   tono pedido %.1f Hz, estimado %.1f Hz (%d muestras)\n",
               esperado, hz, n);
        check("la autocorrelacion acierta el tono",
              fabsf(hz - esperado) < esperado * 0.03f);
    }

    aos_hal_mic_close();
    check("cerrado: status dice cerrado",
          aos_hal_mic_status(&st) && !st.open);

    printf("\n%s\n", fallos ? "HAY FALLAS" : "todo bien");
    return fallos ? 1 : 0;
}
