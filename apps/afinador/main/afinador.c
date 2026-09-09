/*
 * AmoledOS - Tuner (#48) and noise meter (#12)
 *
 * Two screens over the same microphone capture: a chromatic tuner and a sound
 * level meter with A weighting. The maths lives in af_dsp.c, without a line of
 * LVGL, and is tested without the board with tools/af_harness.c.
 *
 * Four things worth writing down before touching this:
 *
 *   - THE TWO SCREENS OPEN THE MICROPHONE AT DIFFERENT RATES, and it is not a
 *     whim. The A curve's top pole is at 12194 Hz: at a 16 kHz sample rate that
 *     lands above Nyquist and the curve folds downwards (measured: -5.74 dB of
 *     error at 6.3 kHz, against -0.58 at 32 kHz). The meter asks for 32 kHz;
 *     the tuner stays at 16, where a window of 2048 samples is 128 ms and is
 *     enough for five periods of a low E.
 *   - THE RATE THE APP ASKS FOR IS NOT ALWAYS THE ONE IT GETS. If the recorder
 *     already has the capture open, its rate rules. Everything is computed with
 *     status.sample_rate, never with the requested one, because pitch detection
 *     with the wrong rate gives a clean, convincing and wrong result.
 *   - THE REFERENCE TONE AND LISTENING ARE EXCLUSIVE. The speaker and the
 *     microphone are the same codec: to give the A the capture has to be
 *     closed, the tone played, and the capture reopened. It is a four-step
 *     state machine and not a loose aos_hal_beep().
 *   - THE PEAK IS WATCHED. If the PGA saturates, the dB figure lies and the
 *     detection starts to go wrong; that is why there is a saturation warning
 *     and not just a level.
 */
#include "aos_app.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include "af_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */

#define TICK_MS         100
#define ANALISIS_MS     200         /* how often the NSDF is run */
/* How often it is REPAINTED, which is not the same as how often it is
 * computed.
 *
 * Measured on the board: with the analysis already cheap (20 ms), the frame
 * was still 77 ms of drawing because five labels and the graph were being
 * rewritten ten times a second. It is the same lesson the Game of Life left
 * behind —a label that changes adds its invalid area with its background and
 * its glyphs— and there dropping the repaint to three a second took 12 ms off
 * the frame. */
#define PINTAR_MS       250

#define RATE_AFINADOR   16000
#define RATE_RUIDO      32000
#define VENTANA         2048        /* samples of the pitch analysis */
#define SCRATCH_FLOATS  AF_SCRATCH_FLOATS(VENTANA, RATE_RUIDO)
#define LOTE            1024        /* how much is drained at once */

#define ABRIR_TIMEOUT   2000        /* ms for the codec to open */
#define CERRAR_TIMEOUT  600         /* ms waiting for the previous capture to die */

#define CENTS_RANGO     50.0f       /* full scale of the needle */
#define AFINADO_CENTS   3.0f
#define CASI_CENTS      15.0f

#define CHART_PUNTOS    60          /* ~6 s of history */

/* dB(A) = dBFS + cal.
 *
 * The starting value comes from the microphone's datasheet, not from a
 * measurement: a typical MEMS gives -26 dBFS at 94 dB SPL, and with the PGA at
 * 30 dB that puts the scale's zero near 90. It is a plausible starting point
 * and NOTHING more: until it is compared against a sound level meter, what is
 * read is relative dB. That is why the number can be corrected from the screen
 * and is stored. */
#define CAL_DEF         90
#define CAL_MIN         40
#define CAL_MAX         140

#define A4_DEF          440
#define A4_MIN          430
#define A4_MAX          450

#define TONO_SOLTAR_MS  250         /* wait for the capture to release the codec */
#define TONO_LARGO_MS   900

typedef enum { MODO_AFINAR = 0, MODO_RUIDO = 1 } modo_t;

typedef enum {
    MIC_CERRADO = 0,
    MIC_CERRANDO,       /* waiting for the previous capture to really die */
    MIC_ABRIENDO,
    MIC_ANDANDO,
    MIC_FALLO,
    TONO_SOLTANDO,      /* the capture was closed, waiting for the codec */
    TONO_SONANDO,
} mic_estado_t;

typedef struct {
    lv_obj_t   *root;
    lv_timer_t *timer;

    lv_obj_t *tab[2];
    lv_obj_t *pag[2];
    modo_t    modo;

    /* tuner */
    lv_obj_t *lbl_nota;
    lv_obj_t *lbl_octava;
    lv_obj_t *pista;
    lv_obj_t *centro;
    lv_obj_t *marca;
    lv_obj_t *lbl_cents;
    lv_obj_t *lbl_hz;
    lv_obj_t *lbl_estado;
    lv_obj_t *lbl_a4;
    lv_obj_t *lbl_tono;

    /* noise */
    lv_obj_t *lbl_db;
    lv_obj_t *lbl_max;
    lv_obj_t *chart;
    lv_chart_series_t *serie;
    lv_obj_t *lbl_aviso;
    lv_obj_t *lbl_cal;

    /* capture */
    mic_estado_t estado;
    uint32_t     rate_pedida;
    uint32_t     rate_real;
    uint32_t     estado_desde;
    bool         reintento_16k;

    int16_t *ventana;           /* PSRAM */
    int      ventana_uso;
    float   *scratch;           /* PSRAM: NSDF curve + window in float */
    int16_t *lote;              /* PSRAM */

    af_aweight_t aweight;
    bool         aweight_lista;

    float    hz;
    float    cents;
    float    clarity;
    int      midi_tono;
    uint32_t ultimo_analisis;

    float    db;
    float    db_max;
    bool     saturado;

    int      a4;
    int      cal;

    uint32_t analisis_ms_peor;
    uint32_t ultimo_log;

    /* The last thing WRITTEN into each object. LVGL does not compare: writing
     * the same text to a label, or the same colour to a style, invalidates it
     * all the same. */
    uint32_t ultimo_pintado;
    char     txt_nota[8], txt_octava[8], txt_cents[24], txt_hz[64], txt_estado[48];
    char     txt_db[12], txt_max[48], txt_cal[16], txt_aviso[64];
    uint32_t color_nota, color_marca;
    int      x_marca;
} af_t;

static af_t s_af;

/* Writes only if it changed. It is the difference between invalidating one
 * area per frame and invalidating none. */
static void set_txt(lv_obj_t *obj, char *cache, size_t cap, const char *txt)
{
    if (!obj || !txt || strncmp(cache, txt, cap - 1) == 0) {
        return;
    }
    snprintf(cache, cap, "%s", txt);
    lv_label_set_text(obj, cache);
}

static void set_color_texto(lv_obj_t *obj, uint32_t *cache, lv_color_t color)
{
    uint32_t v = lv_color_to_u32(color);
    if (!obj || *cache == v) {
        return;
    }
    *cache = v;
    lv_obj_set_style_text_color(obj, color, 0);
}

static uint32_t rate_del_modo(void)
{
    return s_af.modo == MODO_RUIDO ? RATE_RUIDO : RATE_AFINADOR;
}

/* -------------------------------------------------------------------------- */
/* Capture                                                                     */
/* -------------------------------------------------------------------------- */

/* Asking for a different rate means closing and reopening, BUT NOT BACK TO
 * BACK.
 *
 * Measured on the board: aos_hal_mic_close() only removes the user, and the
 * capture task takes up to one block (~50 ms) to notice and die. If
 * mic_open() is called on the very next line, the task is still alive and
 * whoever opens hangs off the rate that was already in force: we asked for
 * 32 kHz and the log said "capture at 16000 Hz (asked for 32000)" without the
 * codec even having found out we were asking for something else. That is why
 * there is a waiting state.
 *
 * If after the deadline the capture is still open, somebody else has it (the
 * recorder): it is opened anyway and whatever rate there is gets used, which
 * is what the status reports it for and what the screen shows. */
static void mic_pedir(uint32_t rate)
{
    aos_hal_mic_close();
    s_af.rate_pedida   = rate;
    s_af.rate_real     = rate;
    s_af.estado        = MIC_CERRANDO;
    s_af.estado_desde  = (uint32_t)aos_hal_uptime_ms();
    s_af.ventana_uso   = 0;
    s_af.aweight_lista = false;
    s_af.hz            = 0.0f;
}

static void mic_abrir_ya(void)
{
    s_af.estado       = MIC_ABRIENDO;
    s_af.estado_desde = (uint32_t)aos_hal_uptime_ms();
    if (!aos_hal_mic_open(s_af.rate_pedida)) {
        s_af.estado = MIC_FALLO;
    }
}

static void mic_esperar_cierre(void)
{
    aos_mic_status_t st;
    bool abierta = aos_hal_mic_status(&st) && st.open;
    uint32_t ahora = (uint32_t)aos_hal_uptime_ms();
    if (!abierta || ahora - s_af.estado_desde > CERRAR_TIMEOUT) {
        if (abierta) {
            aos_hal_log("afinador",
                        "the capture is still open (someone else has it): going with %u Hz",
                        (unsigned)st.sample_rate);
        }
        mic_abrir_ya();
    }
}

static void mic_confirmar(void)
{
    aos_mic_status_t st;
    if (!aos_hal_mic_status(&st) || !st.open) {
        uint32_t ahora = (uint32_t)aos_hal_uptime_ms();
        if (ahora - s_af.estado_desde > ABRIR_TIMEOUT) {
            /* The codec not accepting 32 kHz is possible and is not an error:
             * it falls back to 16, which is the one the recorder uses every
             * day. */
            if (!s_af.reintento_16k && s_af.rate_pedida != RATE_AFINADOR) {
                s_af.reintento_16k = true;
                aos_hal_log("afinador", "32 kHz did not work out, going to 16 kHz");
                mic_pedir(RATE_AFINADOR);
            } else {
                s_af.estado = MIC_FALLO;
            }
        }
        return;
    }

    /* The real one, not the requested one. */
    s_af.rate_real = st.sample_rate ? st.sample_rate : RATE_AFINADOR;
    af_aweight_init(&s_af.aweight, s_af.rate_real);
    s_af.aweight_lista = true;
    s_af.estado        = MIC_ANDANDO;
    aos_hal_log("afinador", "capture at %u Hz (asked for %u), PGA %d dB",
                (unsigned)s_af.rate_real, (unsigned)s_af.rate_pedida, st.gain_db);
}

/* -------------------------------------------------------------------------- */
/* Tuner                                                                       */
/* -------------------------------------------------------------------------- */

/* Leaves the last VENTANA samples in 'ventana'. If the ring gathered more than
 * one window —because the frame was slow— the old data is discarded: what
 * matters is the sound of now, not catching up with half a second ago. */
static void drenar_ventana(void)
{
    int hay = aos_hal_mic_available();
    while (hay > VENTANA) {
        int sobra = hay - VENTANA;
        if (sobra > LOTE) {
            sobra = LOTE;
        }
        int tirado = aos_hal_mic_read(s_af.lote, sobra);
        if (tirado <= 0) {
            break;
        }
        hay -= tirado;
    }

    int n;
    while ((n = aos_hal_mic_read(s_af.lote, LOTE)) > 0) {
        if (n >= VENTANA) {
            memcpy(s_af.ventana, s_af.lote + n - VENTANA,
                   (size_t)VENTANA * sizeof(int16_t));
            s_af.ventana_uso = VENTANA;
        } else {
            int queda = s_af.ventana_uso;
            if (queda > VENTANA - n) {
                queda = VENTANA - n;
            }
            memmove(s_af.ventana,
                    s_af.ventana + (s_af.ventana_uso - queda),
                    (size_t)queda * sizeof(int16_t));
            memcpy(s_af.ventana + queda, s_af.lote,
                   (size_t)n * sizeof(int16_t));
            s_af.ventana_uso = queda + n;
        }
        if (n < LOTE) {
            break;
        }
    }
}

static void afinar_paso(void)
{
    drenar_ventana();
    if (s_af.ventana_uso < VENTANA) {
        return;
    }

    uint32_t ahora = (uint32_t)aos_hal_uptime_ms();
    if (ahora - s_af.ultimo_analisis < ANALISIS_MS) {
        return;
    }
    s_af.ultimo_analisis = ahora;

    uint32_t t0 = (uint32_t)aos_hal_uptime_ms();
    af_pitch_t p = af_pitch(s_af.ventana, VENTANA, s_af.rate_real,
                            s_af.scratch, SCRATCH_FLOATS);
    uint32_t costo = (uint32_t)aos_hal_uptime_ms() - t0;
    if (costo > s_af.analisis_ms_peor) {
        s_af.analisis_ms_peor = costo;
    }

    s_af.clarity = p.clarity;
    if (p.hz > 0.0f) {
        /* Short smoothing: the NSDF is stable already, this only takes the
         * flicker off the last digit so the number can be read. */
        s_af.hz = (s_af.hz > 0.0f) ? (0.6f * s_af.hz + 0.4f * p.hz) : p.hz;
    } else {
        s_af.hz = 0.0f;
    }

    /* One line every 5 s with what the analysis costs ON THE BOARD: it is the
     * number the simulator cannot give. */
    if (ahora - s_af.ultimo_log > 5000) {
        s_af.ultimo_log = ahora;
        aos_hal_log("afinador", "NSDF worst %u ms (%u lags, %d samples, %u Hz)",
                    (unsigned)s_af.analisis_ms_peor,
                    (unsigned)(s_af.rate_real / 30 - s_af.rate_real / 1300),
                    VENTANA, (unsigned)s_af.rate_real);
        s_af.analisis_ms_peor = 0;
    }
}

static void aguja(float cents, lv_color_t color)
{
    int mitad = (AOS_SCREEN_W - 68) / 2;        /* half a track */
    float c = cents;
    if (c >  CENTS_RANGO) c =  CENTS_RANGO;
    if (c < -CENTS_RANGO) c = -CENTS_RANGO;

    int x = 34 + mitad + (int)(c / CENTS_RANGO * (float)mitad) - 3;
    if (x != s_af.x_marca) {
        s_af.x_marca = x;
        lv_obj_set_x(s_af.marca, x);
    }
    uint32_t v = lv_color_to_u32(color);
    if (v != s_af.color_marca) {
        s_af.color_marca = v;
        lv_obj_set_style_bg_color(s_af.marca, color, 0);
    }
}

static void afinar_pintar(void)
{
    char buf[48];

    /* Reference tone mode: the screen says what is about to sound. */
    if (s_af.estado == TONO_SOLTANDO || s_af.estado == TONO_SONANDO) {
        af_note_t n;
        af_note_from_hz(af_note_hz(s_af.midi_tono, (float)s_af.a4),
                        (float)s_af.a4, &n);
        set_txt(s_af.lbl_nota, s_af.txt_nota, sizeof(s_af.txt_nota), n.name);
        snprintf(buf, sizeof(buf), "%d", n.octave);
        set_txt(s_af.lbl_octava, s_af.txt_octava, sizeof(s_af.txt_octava), buf);
        set_txt(s_af.lbl_cents, s_af.txt_cents, sizeof(s_af.txt_cents), _("referencia"));
        snprintf(buf, sizeof(buf), "%.2f Hz", (double)n.ref_hz);
        set_txt(s_af.lbl_hz, s_af.txt_hz, sizeof(s_af.txt_hz), buf);
        set_txt(s_af.lbl_estado, s_af.txt_estado, sizeof(s_af.txt_estado),
                s_af.estado == TONO_SONANDO ? _("sonando") : _("soltando el codec"));
        aguja(0.0f, AOS_C_ACCENT);
        return;
    }

    if (s_af.estado != MIC_ANDANDO) {
        set_txt(s_af.lbl_nota, s_af.txt_nota, sizeof(s_af.txt_nota), "--");
        set_txt(s_af.lbl_octava, s_af.txt_octava, sizeof(s_af.txt_octava), "");
        set_txt(s_af.lbl_cents, s_af.txt_cents, sizeof(s_af.txt_cents), "");
        set_txt(s_af.lbl_hz, s_af.txt_hz, sizeof(s_af.txt_hz), "");
        set_txt(s_af.lbl_estado, s_af.txt_estado, sizeof(s_af.txt_estado),
                s_af.estado == MIC_FALLO ? _("no abrio el microfono")
                                         : _("abriendo el microfono"));
        aguja(0.0f, AOS_C_DIM);
        return;
    }

    if (s_af.hz <= 0.0f) {
        set_txt(s_af.lbl_nota, s_af.txt_nota, sizeof(s_af.txt_nota), "--");
        set_txt(s_af.lbl_octava, s_af.txt_octava, sizeof(s_af.txt_octava), "");
        set_txt(s_af.lbl_cents, s_af.txt_cents, sizeof(s_af.txt_cents), "");
        set_txt(s_af.lbl_hz, s_af.txt_hz, sizeof(s_af.txt_hz), "");
        set_txt(s_af.lbl_estado, s_af.txt_estado, sizeof(s_af.txt_estado),
                s_af.saturado ? _("satura: alejate del microfono")
                              : _("toca una cuerda"));
        aguja(0.0f, AOS_C_DIM);
        return;
    }

    af_note_t n;
    af_note_from_hz(s_af.hz, (float)s_af.a4, &n);
    s_af.cents     = n.cents;
    s_af.midi_tono = n.midi;

    set_txt(s_af.lbl_nota, s_af.txt_nota, sizeof(s_af.txt_nota), n.name);
    snprintf(buf, sizeof(buf), "%d", n.octave);
        set_txt(s_af.lbl_octava, s_af.txt_octava, sizeof(s_af.txt_octava), buf);

    lv_color_t color = AOS_C_RED;
    const char *estado = _("bajala");
    if (fabsf(n.cents) <= AFINADO_CENTS) {
        color  = AOS_C_GREEN;
        estado = _("afinada");
    } else if (fabsf(n.cents) <= CASI_CENTS) {
        color  = AOS_C_ORANGE;
        estado = n.cents < 0 ? _("casi: subila un poco") : _("casi: bajala un poco");
    } else {
        estado = n.cents < 0 ? _("subila") : _("bajala");
    }
    set_color_texto(s_af.lbl_nota, &s_af.color_nota, color);

    snprintf(buf, sizeof(buf), "%+.1f cents", (double)n.cents);
    set_txt(s_af.lbl_cents, s_af.txt_cents, sizeof(s_af.txt_cents), buf);
    snprintf(buf, sizeof(buf), "%.1f Hz   (%s%d = %.1f)",
             (double)s_af.hz, n.name, n.octave, (double)n.ref_hz);
    set_txt(s_af.lbl_hz, s_af.txt_hz, sizeof(s_af.txt_hz), buf);
    set_txt(s_af.lbl_estado, s_af.txt_estado, sizeof(s_af.txt_estado), s_af.saturado ? _("satura") : estado);

    aguja(n.cents, color);
}

/* -------------------------------------------------------------------------- */
/* Noise meter                                                                 */
/* -------------------------------------------------------------------------- */

static void ruido_paso(void)
{
    if (!s_af.aweight_lista) {
        return;
    }

    /* All in float and with the sample normalised: the S3 has no
     * double-precision FPU, so a double here means calls into the software
     * emulation in the app's hottest loop. */
    float suma  = 0.0f;
    int   total = 0;
    int   n;
    while ((n = aos_hal_mic_read(s_af.lote, LOTE)) > 0) {
        for (int i = 0; i < n; i++) {
            float y = af_aweight_run(&s_af.aweight,
                                     (float)s_af.lote[i] / 32768.0f);
            suma += y * y;
        }
        total += n;
        if (n < LOTE) {
            break;
        }
    }
    if (total <= 0) {
        return;
    }

    float rms  = sqrtf(suma / (float)total);
    float dbfs = (rms < 3e-9f) ? -120.0f : 20.0f * log10f(rms);
    float db   = dbfs + (float)s_af.cal;

    s_af.db = (s_af.db > 0.0f) ? (0.6f * s_af.db + 0.4f * db) : db;
    if (s_af.db > s_af.db_max) {
        s_af.db_max = s_af.db;
    }

}

static void ruido_pintar(void)
{
    if (s_af.estado != MIC_ANDANDO) {
        set_txt(s_af.lbl_db, s_af.txt_db, sizeof(s_af.txt_db), "--");
        set_txt(s_af.lbl_max, s_af.txt_max, sizeof(s_af.txt_max), s_af.estado == MIC_FALLO
                          ? _("no abrio el microfono") : _("abriendo..."));
        return;
    }

    char linea[64];
    snprintf(linea, sizeof(linea), "%d", (int)(s_af.db + 0.5f));
    set_txt(s_af.lbl_db, s_af.txt_db, sizeof(s_af.txt_db), linea);
    /* snprintf and not lv_label_set_text_fmt: LVGL's own formatter does not
     * understand %f and prints it as the letter "f". Measured right here: it
     * said "max 69   ahora f". */
    snprintf(linea, sizeof(linea), _("max %d   ahora %.1f"),
             (int)(s_af.db_max + 0.5f), (double)s_af.db);
    set_txt(s_af.lbl_max, s_af.txt_max, sizeof(s_af.txt_max), linea);
    lv_label_set_text_fmt(s_af.lbl_cal, "cal %d", s_af.cal);

    /* The two warnings that stop the number being read as if it came from an
     * instrument: the truncated curve and the saturation. */
    if (s_af.saturado) {
        set_txt(s_af.lbl_aviso, s_af.txt_aviso, sizeof(s_af.txt_aviso), _("SATURA: el numero no vale"));
        lv_obj_set_style_text_color(s_af.lbl_aviso, AOS_C_RED, 0);
    } else if (s_af.rate_real < RATE_RUIDO) {
        snprintf(linea, sizeof(linea), _("a %u Hz la curva A vale hasta 4 kHz"),
                 (unsigned)s_af.rate_real);
        set_txt(s_af.lbl_aviso, s_af.txt_aviso, sizeof(s_af.txt_aviso), linea);
        lv_obj_set_style_text_color(s_af.lbl_aviso, AOS_C_ORANGE, 0);
    } else {
        set_txt(s_af.lbl_aviso, s_af.txt_aviso, sizeof(s_af.txt_aviso), _("dB relativo hasta que lo calibres"));
        lv_obj_set_style_text_color(s_af.lbl_aviso, AOS_C_DIM, 0);
    }
}

/* -------------------------------------------------------------------------- */
/* Timer                                                                       */
/* -------------------------------------------------------------------------- */

static void tick(lv_timer_t *t)
{
    (void)t;

    aos_mic_status_t st;
    if (aos_hal_mic_status(&st)) {
        s_af.saturado = (st.peak >= 32000);
    }

    switch (s_af.estado) {
    case MIC_CERRANDO:
        mic_esperar_cierre();
        break;

    case MIC_ABRIENDO:
        mic_confirmar();
        break;

    case MIC_ANDANDO:
        if (s_af.modo == MODO_AFINAR) {
            afinar_paso();
        } else {
            ruido_paso();
        }
        break;

    case TONO_SOLTANDO:
        if ((uint32_t)aos_hal_uptime_ms() - s_af.estado_desde >= TONO_SOLTAR_MS) {
            float hz = af_note_hz(s_af.midi_tono, (float)s_af.a4);
            aos_hal_beep((int)(hz + 0.5f), TONO_LARGO_MS);
            s_af.estado       = TONO_SONANDO;
            s_af.estado_desde = (uint32_t)aos_hal_uptime_ms();
        }
        break;

    case TONO_SONANDO:
        if ((uint32_t)aos_hal_uptime_ms() - s_af.estado_desde >=
            TONO_LARGO_MS + 200) {
            s_af.reintento_16k = false;
            mic_pedir(rate_del_modo());
        }
        break;

    default:
        break;
    }

    /* Computing runs at 10 a second; PAINTING runs at 4. The analysis does not
     * give a new value more often than that, so repainting faster is
     * invalidating area to draw the same thing again. */
    uint32_t ahora = (uint32_t)aos_hal_uptime_ms();
    if (ahora - s_af.ultimo_pintado < PINTAR_MS) {
        return;
    }
    s_af.ultimo_pintado = ahora;

    if (s_af.modo == MODO_AFINAR) {
        afinar_pintar();
    } else {
        ruido_pintar();
        /* One point of the graph per repaint: 60 points is 15 s of history. */
        int v = (int)(s_af.db + 0.5f);
        if (v < 0)   v = 0;
        if (v > 130) v = 130;
        lv_chart_set_next_value(s_af.chart, s_af.serie, v);
    }
}

/* -------------------------------------------------------------------------- */
/* Events                                                                      */
/* -------------------------------------------------------------------------- */

static void poner_modo(modo_t modo)
{
    if (modo == s_af.modo) {
        return;
    }
    s_af.modo = modo;

    for (int i = 0; i < 2; i++) {
        bool activa = (i == (int)modo);
        lv_obj_set_style_bg_color(s_af.tab[i],
                                  activa ? AOS_C_ACCENT : AOS_C_CARD, 0);
        if (activa) {
            lv_obj_remove_flag(s_af.pag[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_af.pag[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Each screen wants its own rate, so the capture is reopened. */
    s_af.reintento_16k  = false;
    s_af.db             = 0.0f;
    s_af.ultimo_pintado = 0;        /* repaint on the next tick */
    mic_pedir(rate_del_modo());
}

static void ev_tab(lv_event_t *e)
{
    poner_modo((modo_t)(intptr_t)lv_event_get_user_data(e));
}

static void ev_a4(lv_event_t *e)
{
    int paso = (int)(intptr_t)lv_event_get_user_data(e);
    s_af.a4 += paso;
    if (s_af.a4 < A4_MIN) s_af.a4 = A4_MIN;
    if (s_af.a4 > A4_MAX) s_af.a4 = A4_MAX;
    aos_hal_pref_set_i32("af_a4", s_af.a4);
    lv_label_set_text_fmt(s_af.lbl_a4, "A4 %d", s_af.a4);
}

static void ev_cal(lv_event_t *e)
{
    int paso = (int)(intptr_t)lv_event_get_user_data(e);
    s_af.cal += paso;
    if (s_af.cal < CAL_MIN) s_af.cal = CAL_MIN;
    if (s_af.cal > CAL_MAX) s_af.cal = CAL_MAX;
    aos_hal_pref_set_i32("af_cal", s_af.cal);
    lv_label_set_text_fmt(s_af.lbl_cal, "cal %d", s_af.cal);
}

static void ev_max(lv_event_t *e)
{
    (void)e;
    s_af.db_max = 0.0f;
}

static void ev_tono(lv_event_t *e)
{
    (void)e;
    if (s_af.estado != MIC_ANDANDO) {
        return;             /* it is already sounding or has not opened yet */
    }
    if (s_af.midi_tono <= 0) {
        s_af.midi_tono = 69;            /* with no note detected, the A at 440 */
    }
    /* The capture has to release the codec before anything sounds: they are
     * the same ES8311 and opening the speaker tears its input channel away. */
    aos_hal_mic_close();
    s_af.estado       = TONO_SOLTANDO;
    s_af.estado_desde = (uint32_t)aos_hal_uptime_ms();
    lv_label_set_text(s_af.lbl_tono, _("TONO"));
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */
/* -------------------------------------------------------------------------- */

static lv_obj_t *chip(lv_obj_t *padre, const char *texto, int x, int y,
                      int w, int h, lv_event_cb_t cb, void *dato, lv_color_t bg)
{
    lv_obj_t *b = lv_obj_create(padre);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, dato);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, texto);
    lv_obj_set_style_text_font(l, aos_font_body, 0);
    lv_obj_set_style_text_color(l, AOS_C_TEXT, 0);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

/* Fixed-box label with centred text: lv_obj_align_to() does not recompute when
 * the text changes length, so everything that changes goes in a fixed box. */
static lv_obj_t *caja(lv_obj_t *padre, const char *texto, const lv_font_t *font,
                      lv_color_t color, int y, int h)
{
    lv_obj_t *l = lv_label_create(padre);
    lv_label_set_text(l, texto);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(l, AOS_SCREEN_W, h);
    lv_obj_set_pos(l, 0, y);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static void construir_afinador(lv_obj_t *p)
{
    s_af.lbl_nota = caja(p, "--", aos_font_huge, AOS_C_TEXT, 52, 70);
    /* A deliberately narrow box: what LVGL invalidates is the ALREADY
     * transformed area, so a 368 px label at scale 1.64 dirties 603 px of
     * width on every frame to draw two letters. */
    lv_obj_set_width(s_af.lbl_nota, 160);
    lv_obj_set_x(s_af.lbl_nota, (AOS_SCREEN_W - 160) / 2);
    lv_obj_set_style_transform_scale(s_af.lbl_nota, 420, 0);
    lv_obj_set_style_transform_pivot_x(s_af.lbl_nota, 80, 0);
    lv_obj_set_style_transform_pivot_y(s_af.lbl_nota, 35, 0);

    s_af.lbl_octava = lv_label_create(p);
    set_txt(s_af.lbl_octava, s_af.txt_octava, sizeof(s_af.txt_octava), "");
    lv_obj_set_style_text_font(s_af.lbl_octava, aos_font_title, 0);
    lv_obj_set_style_text_color(s_af.lbl_octava, AOS_C_DIM, 0);
    /* 36 and not 30: this font's line height is 35 px and the box was clipping
     * the descender. With no visible descenders it went unnoticed. */
    lv_obj_set_size(s_af.lbl_octava, 60, 36);
    lv_obj_set_style_text_align(s_af.lbl_octava, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_af.lbl_octava, AOS_SCREEN_W / 2 + 52, 66);

    /* the needle */
    int mitad = (AOS_SCREEN_W - 68) / 2;
    s_af.pista = lv_obj_create(p);
    lv_obj_remove_style_all(s_af.pista);
    lv_obj_set_size(s_af.pista, mitad * 2, 8);
    lv_obj_set_pos(s_af.pista, 34, 150);
    lv_obj_set_style_bg_color(s_af.pista, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_opa(s_af.pista, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_af.pista, 4, 0);

    s_af.centro = lv_obj_create(p);
    lv_obj_remove_style_all(s_af.centro);
    lv_obj_set_size(s_af.centro, 2, 26);
    lv_obj_set_pos(s_af.centro, AOS_SCREEN_W / 2 - 1, 141);
    lv_obj_set_style_bg_color(s_af.centro, AOS_C_DIM, 0);
    lv_obj_set_style_bg_opa(s_af.centro, LV_OPA_COVER, 0);

    s_af.marca = lv_obj_create(p);
    lv_obj_remove_style_all(s_af.marca);
    lv_obj_set_size(s_af.marca, 6, 30);
    lv_obj_set_pos(s_af.marca, AOS_SCREEN_W / 2 - 3, 139);
    lv_obj_set_style_bg_color(s_af.marca, AOS_C_DIM, 0);
    lv_obj_set_style_bg_opa(s_af.marca, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_af.marca, 3, 0);

    /* Everything below moved up ~30 px. The page is AOS_SCREEN_H-30-42 = 376
     * and this reached down to 426: the TONO button came out cut off by the
     * screen's edge and the text below was NEVER visible. tools/audit_layout.sh
     * found it, not the eye; it was like that in both languages. */
    s_af.lbl_cents  = caja(p, "", aos_font_title, AOS_C_TEXT, 170, 32);
    s_af.lbl_hz     = caja(p, "", aos_font_small, AOS_C_DIM, 204, 22);
    /* The status drops to y=324 (screen 396..422): it is text that is only
     * read and it is out of the way there, and that way both buttons move up
     * into the strip this board's touch panel reaches (see AOS_TOUCH_Y_MAX in
     * aos_hal.h). */
    s_af.lbl_estado = caja(p, _("abriendo el microfono"), aos_font_body,
                           AOS_C_DIM, 324, 26);

    /* A4 */
    chip(p, "-", 34, 228, 44, 40, ev_a4, (void *)(intptr_t)-1, AOS_C_CARD);
    chip(p, "+", 290, 228, 44, 40, ev_a4, (void *)(intptr_t)1, AOS_C_CARD);
    s_af.lbl_a4 = caja(p, "A4 440", aos_font_body, AOS_C_TEXT, 235, 26);

    /* reference tone */
    lv_obj_t *bt = chip(p, _("TONO"), 104, 274, 160, 44, ev_tono, NULL, AOS_C_ACCENT);
    s_af.lbl_tono = lv_obj_get_child(bt, 0);
    caja(p, _("escuchar y sonar no van juntos"), aos_font_small, AOS_C_DIM, 354, 20);
}

static void construir_ruido(lv_obj_t *p)
{
    s_af.lbl_db = caja(p, "--", aos_font_huge, AOS_C_TEXT, 50, 70);
    lv_obj_set_width(s_af.lbl_db, 200);
    lv_obj_set_x(s_af.lbl_db, (AOS_SCREEN_W - 200) / 2);
    lv_obj_set_style_transform_scale(s_af.lbl_db, 380, 0);
    lv_obj_set_style_transform_pivot_x(s_af.lbl_db, 100, 0);
    lv_obj_set_style_transform_pivot_y(s_af.lbl_db, 35, 0);

    caja(p, "dB(A)", aos_font_body, AOS_C_DIM, 128, 26);
    s_af.lbl_max = caja(p, "", aos_font_small, AOS_C_DIM, 156, 22);

    s_af.chart = lv_chart_create(p);
    lv_obj_set_size(s_af.chart, 300, 120);
    lv_obj_set_pos(s_af.chart, 34, 186);
    lv_obj_set_style_bg_color(s_af.chart, AOS_C_CARD, 0);
    lv_obj_set_style_border_width(s_af.chart, 0, 0);
    lv_obj_set_style_radius(s_af.chart, 12, 0);
    lv_obj_set_style_line_width(s_af.chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_af.chart, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_type(s_af.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_af.chart, CHART_PUNTOS);
    lv_chart_set_range(s_af.chart, LV_CHART_AXIS_PRIMARY_Y, 20, 110);
    lv_chart_set_div_line_count(s_af.chart, 4, 0);
    s_af.serie = lv_chart_add_series(s_af.chart, AOS_C_GREEN,
                                     LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_remove_flag(s_af.chart, LV_OBJ_FLAG_CLICKABLE);

    s_af.lbl_aviso = caja(p, "", aos_font_small, AOS_C_DIM, 314, 22);

    chip(p, "-", 34, 342, 44, 40, ev_cal, (void *)(intptr_t)-1, AOS_C_CARD);
    chip(p, "+", 290, 342, 44, 40, ev_cal, (void *)(intptr_t)1, AOS_C_CARD);
    s_af.lbl_cal = caja(p, "cal 90", aos_font_body, AOS_C_TEXT, 350, 26);

    chip(p, _("borrar maximo"), 84, 390, 200, 36, ev_max, NULL, AOS_C_CARD2);
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    memset(&s_af, 0, sizeof(s_af));
    s_af.root = root;

    int32_t v;
    s_af.a4  = aos_hal_pref_get_i32("af_a4", &v)  ? (int)v : A4_DEF;
    s_af.cal = aos_hal_pref_get_i32("af_cal", &v) ? (int)v : CAL_DEF;
    if (s_af.a4  < A4_MIN  || s_af.a4  > A4_MAX)  s_af.a4  = A4_DEF;
    if (s_af.cal < CAL_MIN || s_af.cal > CAL_MAX) s_af.cal = CAL_DEF;

    /* Everything heavy to PSRAM, of which there is plenty: what has to be
     * looked after is the .text. */
    s_af.ventana = malloc((size_t)VENTANA * sizeof(int16_t));
    s_af.lote    = malloc((size_t)LOTE * sizeof(int16_t));
    s_af.scratch = malloc((size_t)SCRATCH_FLOATS * sizeof(float));
    if (!s_af.ventana || !s_af.lote || !s_af.scratch) {
        aos_ui_toast(_("sin memoria"), 1500);
        return NULL;
    }

    lv_obj_set_style_bg_color(root, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    s_af.tab[0] = chip(root, _("AFINAR"), 24, 4, 152, 34, ev_tab,
                       (void *)(intptr_t)MODO_AFINAR, AOS_C_ACCENT);
    s_af.tab[1] = chip(root, _("RUIDO"), 192, 4, 152, 34, ev_tab,
                       (void *)(intptr_t)MODO_RUIDO, AOS_C_CARD);

    for (int i = 0; i < 2; i++) {
        s_af.pag[i] = lv_obj_create(root);
        lv_obj_remove_style_all(s_af.pag[i]);
        /* lv_obj_remove_style_all deletes the SIZE (in LVGL 9 the width and
         * the height are local style), so it has to be put back or the page is
         * drawn clipped in the top left with no error at all. */
        lv_obj_set_size(s_af.pag[i], AOS_SCREEN_W, AOS_SCREEN_H - 30 - 42);
        lv_obj_set_pos(s_af.pag[i], 0, 42);
        lv_obj_remove_flag(s_af.pag[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_add_flag(s_af.pag[1], LV_OBJ_FLAG_HIDDEN);

    construir_afinador(s_af.pag[0]);
    construir_ruido(s_af.pag[1]);
    lv_label_set_text_fmt(s_af.lbl_a4, "A4 %d", s_af.a4);
    lv_label_set_text_fmt(s_af.lbl_cal, "cal %d", s_af.cal);

    /* Development switch: AF_MODO=ruido starts on the meter, which otherwise
     * has to be found by tapping a tab on every test. */
    const char *modo_env = getenv("AF_MODO");
    s_af.modo      = (modo_env && modo_env[0] == 'r') ? MODO_RUIDO : MODO_AFINAR;
    s_af.midi_tono = 69;
    if (s_af.modo == MODO_RUIDO) {
        lv_obj_set_style_bg_color(s_af.tab[0], AOS_C_CARD, 0);
        lv_obj_set_style_bg_color(s_af.tab[1], AOS_C_ACCENT, 0);
        lv_obj_add_flag(s_af.pag[0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_af.pag[1], LV_OBJ_FLAG_HIDDEN);
    }
    mic_pedir(rate_del_modo());

    s_af.timer = lv_timer_create(tick, TICK_MS, NULL);
    return &s_af;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    if (s_af.timer) {
        lv_timer_delete(s_af.timer);
        s_af.timer = NULL;
    }
    /* ALWAYS release the codec, including if you leave with the tone sounding:
     * otherwise the capture is left held against the next app that wants it. */
    aos_hal_mic_close();

    free(s_af.ventana);
    free(s_af.lote);
    free(s_af.scratch);

    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    memset(&s_af, 0, sizeof(s_af));
}

static bool afinador_init(aos_app_t *app)
{
    app->desc.id       = "aos.tuner";
    app->desc.name     = "Afinador";
    app->desc.icon     = "af";
    app->desc.icon_vec = AOS_ICON_MIC;
    app->desc.color_a  = 0x40C8E0;
    app->desc.color_b  = 0x0A6070;
    app->desc.order    = 78;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE;

    app->create  = create;
    app->destroy = destroy;
    return true;
}

AOS_APP_ENTRY(afinador_init);
