/*
 * CHATARRA - the music, and everything else that makes a noise
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE STOPPED BEING A SEQUENCER AND BECAME A SYNTHESISER
 * ---------------------------------------------------------------------------
 *
 * It used to be a one-voice sequencer over `aos_hal_beep()`, because that was
 * all there was: the board's ES8311 played one tone at a time and the HAL's
 * API was (frequency, duration). A melody was a list of notes and a counter.
 *
 * v0.4.3 added `aos_hal_spk_*` for the walkie-talkie: PCM in, sound out, a
 * ring of one second in PSRAM, a task feeding the codec 20 ms at a time. That
 * is a different instrument. This file now generates the samples itself -
 * three voices and a drum - and hands them over every frame.
 *
 * ---------------------------------------------------------------------------
 * AND THE CONSEQUENCE THAT DECIDED THE DESIGN
 * ---------------------------------------------------------------------------
 *
 * The tone task checks `s_spk_task` and stays quiet while the streaming
 * speaker holds the codec (aos_hal_esp32.c, in the tone task's guard). So the
 * moment this file opens the speaker for music, **every `aos_hal_beep()` in
 * the game goes silent**: the hits, the taps, the chest opening.
 *
 * That is not a problem to work around, it is the design telling you where it
 * wants to go. The effects became a FOURTH VOICE of the same mix. One audio
 * path, and along the way they got an envelope: a hit is no longer a square
 * wave switched on and off, which is what made them sound like a microwave.
 *
 * If the speaker cannot be opened -the microphone holds the codec, an older
 * firmware- everything falls back to `aos_hal_beep()` and the game is exactly
 * what it was. `ch_sfx()` is the one call the game makes either way.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS CHEAP AND WHAT IS NOT
 * ---------------------------------------------------------------------------
 *
 * At 16 kHz a frame of the game is 528 samples. Four voices of integer
 * arithmetic over 528 samples is nothing - no multiplies in the inner loop,
 * no floating point, no tables beyond the notes themselves. What would be
 * expensive is blocking: `aos_hal_spk_write()` never does, and the ring is
 * topped up towards a target instead of being filled on a schedule, so a slow
 * frame is absorbed by the buffer instead of becoming a gap.
 *
 * ---------------------------------------------------------------------------
 * WHERE IT PLAYS AND WHERE IT DOES NOT
 * ---------------------------------------------------------------------------
 *
 * Title, combats, and the short stings - victory, level up, defeat, the
 * ending. The map stays silent except for the effects, and that is deliberate:
 * a loop repeating while you walk for half an hour through 51 rooms is how you
 * teach somebody to turn the sound off. With the map quiet, the combat music
 * ARRIVES, which is what you want to happen when a rival appears.
 */
#include "chatarra.h"

#include <string.h>

/* Durations in frames of 33 ms. 4 = a quaver at ~110 beats per minute. */
#define C3   131
#define D3   147
#define E3   165
#define F3   175
#define G3   196
#define A3   220
#define B3   247
#define C4   262
#define D4   294
#define E4   330
#define F4   349
#define G4   392
#define A4   440
#define AS4  466
#define B4   494
#define C5   523
#define D5   587
#define DS5  622
#define E5   659
#define F5   698
#define G5   784
#define A5   880
#define C6  1047
#define E6  1319
#define G6  1568
#define SIL    0

/* And the octave below, for the bass line. */
#define C2    65
#define D2    73
#define E2    82
#define F2    87
#define G2    98
#define A2   110
#define AS2  117
#define B2   123

typedef struct { uint16_t hz; uint8_t dur; } nota_t;

/* --------------------------------------------------------------------------
 * The lead lines. These are the melodies this game has always had.
 * -------------------------------------------------------------------------- */

static const nota_t MEL_TITULO[] = {
    {C4,6},{E4,6},{G4,6},{C5,10},{G4,4},{E4,4},{F4,6},{A4,6},{C5,10},{SIL,4},
    {D4,6},{F4,6},{A4,6},{D5,10},{A4,4},{F4,4},{G4,6},{B4,6},{D5,12},{SIL,6},
    {C4,6},{E4,6},{G4,6},{C5,10},{E5,8},{D5,8},{C5,14},{SIL,10},
};

static const nota_t MEL_COMBATE[] = {
    {A3,3},{A3,3},{C4,3},{E4,3},{A4,6},{E4,3},{C4,3},
    {A3,3},{A3,3},{C4,3},{E4,3},{G4,6},{E4,3},{C4,3},
    {F3,3},{F3,3},{A3,3},{C4,3},{F4,6},{C4,3},{A3,3},
    {G3,3},{G3,3},{B3,3},{D4,3},{G4,6},{D4,3},{B3,3},
    {A4,4},{C5,4},{E5,4},{C5,4},{A4,4},{G4,4},{F4,4},{E4,8},
    {SIL,4},
};

static const nota_t MEL_JEFE[] = {
    {C3,2},{C3,2},{DS5,2},{C3,2},{C3,2},{D5,2},{C3,2},{C3,2},
    {C3,2},{C3,2},{DS5,2},{C3,2},{C3,2},{C5,2},{C3,2},{C3,2},
    {D3,2},{D3,2},{F5,2},{D3,2},{D3,2},{DS5,2},{D3,2},{D3,2},
    {C3,2},{C3,2},{DS5,2},{C3,2},{C3,2},{D5,2},{C3,2},{C3,2},
    {G5,4},{F5,4},{DS5,4},{D5,4},{C5,8},{SIL,4},
};

static const nota_t MEL_VICTORIA[] = {
    {C5,3},{C5,3},{C5,3},{C5,9},{G4,9},{A4,9},{C5,6},{A4,3},{C5,18},{SIL,6},
};
static const nota_t MEL_NIVEL[] = {
    {G4,3},{C5,3},{E5,3},{G5,9},{E5,3},{G5,15},{SIL,4},
};
static const nota_t MEL_DERROTA[] = {
    {C5,6},{B4,6},{AS4,6},{A4,18},{F4,6},{E4,6},{D4,6},{C4,24},{SIL,8},
};
static const nota_t MEL_FINAL[] = {
    {C5,6},{E5,6},{G5,6},{C6,12},{SIL,3},{G5,6},{C6,6},{E6,12},{SIL,3},
    {A5,6},{G5,6},{F5,6},{G5,12},{C6,20},{SIL,10},
};

/* --------------------------------------------------------------------------
 * The bass lines, and the drums
 *
 * A bass line LOOPS ON ITS OWN, independently of the lead: it is a handful of
 * roots, not a second melody written note against note. That is both what
 * chiptune does and what keeps this table small enough to be worth having -
 * three lines of data buy the difference between a tune and a piece of music.
 *
 * The drums are a string, one character per step of four frames (132 ms, the
 * quaver these melodies are written in): 'K' kick, 'S' snare, 'h' hat,
 * '.' nothing. It loops too.
 * -------------------------------------------------------------------------- */

static const nota_t BAJO_TITULO[] = {
    {C2,12},{G2,12},{F2,12},{G2,12},
    {D2,12},{A2,12},{G2,12},{G2,12},
};
static const nota_t BAJO_COMBATE[] = {
    {A2,6},{A2,6},{A2,6},{E2,6},
    {A2,6},{A2,6},{C3,6},{E2,6},
    {F2,6},{F2,6},{F2,6},{C3,6},
    {G2,6},{G2,6},{D3,6},{B2,6},
};
static const nota_t BAJO_JEFE[] = {
    {C2,4},{C2,4},{C2,4},{C2,4},
    {C2,4},{C2,4},{AS2,4},{AS2,4},
    {D2,4},{D2,4},{D2,4},{D2,4},
    {C2,4},{C2,4},{G2,4},{G2,4},
};
static const nota_t BAJO_FINAL[] = {
    {C2,12},{G2,12},{A2,12},{F2,12},
};

#define RIT_TITULO   "h...h...h...h..."
#define RIT_COMBATE  "K.h.S.h.K.h.S.h."
#define RIT_JEFE     "K.K.S.h.K.K.S.S."
#define RIT_FINAL    "K.h.S.h.K.S.S.S."

typedef struct {
    const nota_t *lead;  uint8_t n_lead;
    const nota_t *bajo;  uint8_t n_bajo;    /* NULL: lead alone              */
    const char   *ritmo;                    /* NULL: no drums                */
    uint8_t       bucle;                    /* does it repeat?               */
} cancion_t;

#define N(a)    (a), (uint8_t)(sizeof(a) / sizeof((a)[0]))

static const cancion_t CANCIONES[] = {
    { NULL, 0, NULL, 0, NULL, 0 },                      /* CH_MEL_NADA       */
    { N(MEL_TITULO),   N(BAJO_TITULO),  RIT_TITULO,  1 },
    { N(MEL_COMBATE),  N(BAJO_COMBATE), RIT_COMBATE, 1 },
    { N(MEL_JEFE),     N(BAJO_JEFE),    RIT_JEFE,    1 },
    /* The stings play once and stop. The victory one gets a drum because a
     * fanfare without one sounds like a mistake; the defeat one gets neither
     * bass nor drum, because it is supposed to feel like the power going. */
    { N(MEL_VICTORIA), NULL, 0,         "K...S...",  0 },
    { N(MEL_NIVEL),    NULL, 0,         NULL,        0 },
    { N(MEL_DERROTA),  NULL, 0,         NULL,        0 },
    { N(MEL_FINAL),    N(BAJO_FINAL),   RIT_FINAL,   0 },
};

#define NCANCIONES ((int)(sizeof(CANCIONES) / sizeof(CANCIONES[0])))

/* --------------------------------------------------------------------------
 * The synthesiser
 *
 * Square waves by phase accumulator, noise by a 15-bit shift register, and an
 * envelope that is two straight lines. No multiplies in the inner loop and no
 * floating point anywhere: the frequency becomes a phase STEP once, when the
 * note starts.
 * -------------------------------------------------------------------------- */

#define SND_HZ      16000
#define SND_TROZO   256                 /* samples generated at a time       */
#define SND_META    2400                /* ring target: 150 ms of cushion    */

/* Duty cycles, out of 256. A lead at 1/4 and a bass at 1/2 is the oldest
 * trick there is for telling two square waves apart. */
#define DUTY_LEAD   64
#define DUTY_BAJO   128
#define DUTY_SFX    96

enum { V_LEAD = 0, V_BAJO, V_RUIDO, V_SFX, NVOCES };

typedef struct {
    uint16_t fase;
    uint16_t paso;              /* phase per sample                          */
    uint8_t  duty;
    int16_t  vol;               /* where the envelope is now                 */
    int16_t  pico;              /* where it is heading, 0 = releasing        */
    int16_t  ataque, caida;     /* how fast, per sample                      */
    int32_t  restan;            /* samples of note left                      */
} voz_t;

static struct {
    bool     activo;            /* the synthesiser has the speaker           */
    bool     intentado;         /* we already tried to open it               */
    voz_t    v[NVOCES];
    uint32_t lfsr;
    uint16_t ruido_paso, ruido_cuenta;

    /* where each line is */
    uint8_t  cancion;
    uint8_t  i_lead,  i_bajo,  i_rit;
    int32_t  t_lead,  t_bajo,  t_rit;   /* samples until the next step       */
} S;

static void voz_nota(voz_t *v, int hz, int ms, int pico, int duty)
{
    v->paso = hz > 0 ? (uint16_t)(((uint32_t)hz << 16) / SND_HZ) : 0;
    v->duty = (uint8_t)duty;
    v->restan = (int32_t)ms * SND_HZ / 1000;
    v->pico = hz > 0 ? (int16_t)pico : 0;
    /* A note that starts at full volume clicks. Two milliseconds of ramp is
     * inaudible as a ramp and removes the click completely. */
    v->ataque = (int16_t)(pico / (SND_HZ / 500) + 1);
    v->caida  = (int16_t)(pico / (SND_HZ / 250) + 1);
    if (!hz) v->restan = (int32_t)ms * SND_HZ / 1000;
}

static void voz_callar(voz_t *v)
{
    v->pico = 0;
    v->restan = 0;
}

/* One sample of one voice, envelope included. */
static inline int voz_muestra(voz_t *v, bool ruido)
{
    int s;

    if (v->restan > 0) {
        v->restan--;
        if (v->vol < v->pico) {
            v->vol = (int16_t)(v->vol + v->ataque);
            if (v->vol > v->pico) v->vol = v->pico;
        }
    } else if (v->vol > 0) {
        v->vol = (int16_t)(v->vol - v->caida);
        if (v->vol < 0) v->vol = 0;
    }
    if (v->vol <= 0) return 0;

    if (ruido) {
        if (++S.ruido_cuenta >= S.ruido_paso) {
            S.ruido_cuenta = 0;
            /* 15-bit maximal-length register: the classic noise of these
             * machines, and one shift and one xor per step. */
            S.lfsr = (S.lfsr >> 1) ^ (uint32_t)((-(int32_t)(S.lfsr & 1)) & 0x6000u);
        }
        s = (S.lfsr & 1) ? v->vol : -v->vol;
    } else {
        v->fase = (uint16_t)(v->fase + v->paso);
        s = ((v->fase >> 8) < v->duty) ? v->vol : -v->vol;
    }
    return s;
}

/* --------------------------------------------------------------------------
 * The lines, advanced in samples rather than in frames
 *
 * The old sequencer counted frames because a frame was the only clock it had.
 * Here the clock is the sample, so a note lasts exactly as long as it says
 * even if a frame of the game runs late - which is the whole reason the music
 * stops wobbling when the board is busy.
 * -------------------------------------------------------------------------- */

#define DUR_MUESTRAS(d) ((int32_t)(d) * SND_HZ * 33 / 1000)

static const cancion_t *cancion(void)
{
    if (!S.cancion || S.cancion >= NCANCIONES) return NULL;
    return &CANCIONES[S.cancion];
}

static void paso_lead(void)
{
    const cancion_t *c = cancion();
    const nota_t *n;

    if (!c || !c->lead) return;
    if (S.i_lead >= c->n_lead) {
        if (!c->bucle) {
            S.cancion = CH_MEL_NADA;
            voz_callar(&S.v[V_LEAD]);
            voz_callar(&S.v[V_BAJO]);
            return;
        }
        S.i_lead = 0;
    }
    n = &c->lead[S.i_lead++];
    S.t_lead = DUR_MUESTRAS(n->dur);
    /* Thirty milliseconds short, so two equal notes in a row are heard as two
     * and not as one long one. That gap was in the old sequencer too; here it
     * is the release of the envelope instead of a hole in the schedule. */
    voz_nota(&S.v[V_LEAD], n->hz, (int)(n->dur * 33 - 30), 5200, DUTY_LEAD);
}

static void paso_bajo(void)
{
    const cancion_t *c = cancion();
    const nota_t *n;

    if (!c || !c->bajo) { voz_callar(&S.v[V_BAJO]); S.t_bajo = SND_HZ; return; }
    if (S.i_bajo >= c->n_bajo) S.i_bajo = 0;
    n = &c->bajo[S.i_bajo++];
    S.t_bajo = DUR_MUESTRAS(n->dur);
    voz_nota(&S.v[V_BAJO], n->hz, (int)(n->dur * 33 - 20), 4200, DUTY_BAJO);
}

static void paso_ritmo(void)
{
    const cancion_t *c = cancion();
    char k;

    S.t_rit = DUR_MUESTRAS(4);          /* one step = a quaver               */
    if (!c || !c->ritmo) { voz_callar(&S.v[V_RUIDO]); return; }
    if (!c->ritmo[S.i_rit]) S.i_rit = 0;
    k = c->ritmo[S.i_rit++];

    switch (k) {
    case 'K':   /* kick: low noise, gone in a blink                          */
        S.ruido_paso = 24;
        voz_nota(&S.v[V_RUIDO], 1, 70, 5000, 128);
        break;
    case 'S':   /* snare: mid noise, a touch longer                          */
        S.ruido_paso = 6;
        voz_nota(&S.v[V_RUIDO], 1, 110, 3800, 128);
        break;
    case 'h':   /* hat: high and short                                       */
        S.ruido_paso = 2;
        voz_nota(&S.v[V_RUIDO], 1, 35, 1800, 128);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * Generating
 * -------------------------------------------------------------------------- */

static void generar(int n)
{
    int16_t buf[SND_TROZO];
    bool musica = ch_sonido_get() >= 2;

    while (n > 0) {
        int m = n > SND_TROZO ? SND_TROZO : n;

        for (int i = 0; i < m; i++) {
            int s;
            /* The lines step between samples, never inside one. */
            if (--S.t_lead <= 0) paso_lead();
            if (--S.t_bajo <= 0) paso_bajo();
            if (--S.t_rit  <= 0) paso_ritmo();

            s = voz_muestra(&S.v[V_SFX], false);
            if (musica) {
                s += voz_muestra(&S.v[V_LEAD], false);
                s += voz_muestra(&S.v[V_BAJO], false);
                s += voz_muestra(&S.v[V_RUIDO], true);
            } else {
                /* With music off the lines still RUN -so that turning it back
                 * on does not restart the tune halfway through a bar- they
                 * simply are not mixed. */
                S.v[V_LEAD].vol = S.v[V_BAJO].vol = S.v[V_RUIDO].vol = 0;
            }
            if (s >  22000) s =  22000;
            if (s < -22000) s = -22000;
            buf[i] = (int16_t)s;
        }
        if (ch_audio_escribir(buf, m) < m) return;  /* the ring filled up    */
        n -= m;
    }
}

/* --------------------------------------------------------------------------
 * What the game calls
 * -------------------------------------------------------------------------- */

void ch_snd_init(void)
{
    memset(&S, 0, sizeof(S));
    S.lfsr = 0x7FFFu;
    S.ruido_paso = 8;
    S.t_lead = S.t_bajo = S.t_rit = 1;
    for (int i = 0; i < NVOCES; i++) S.v[i].caida = 40;
    /* The speaker is NOT opened here. `aos_hal_spk_open()` waits for the
     * microphone to let the codec go -up to 800 ms- and then the codec's own
     * open costs about 200. Doing that inside create() delays the app's first
     * frame; doing it on the first tick spends it on the title screen, where
     * there is nothing to miss. */
}

void ch_snd_fin(void)
{
    if (S.activo) ch_audio_cerrar();
    S.activo = false;
}

/* Called when the sound setting changes: off gives the codec back, on tries
 * again from scratch. */
void ch_snd_reabrir(void)
{
    S.intentado = false;
    if (ch_sonido_get() == 0) ch_snd_fin();
}

bool ch_snd_sintetiza(void)
{
    return S.activo;
}

void ch_snd_sfx(int freq_hz, int ms)
{
    if (!S.activo || freq_hz <= 0 || ms <= 0) return;
    /* An effect always wins its own voice: a hit landing on top of the
     * previous one is the hit you want to hear, not the one before it. */
    voz_nota(&S.v[V_SFX], freq_hz, ms, 6000, DUTY_SFX);
}

void ch_snd_melodia(ch_t *g, int id)
{
    if (id < 0 || id >= NCANCIONES) id = CH_MEL_NADA;
    if (g->mel_id == id) return;            /* already playing that one      */
    g->mel_id = (uint8_t)id;
    g->mel_i = 0;
    g->mel_t = 0;

    if (!S.activo) return;
    S.cancion = (uint8_t)id;
    S.i_lead = S.i_bajo = S.i_rit = 0;
    S.t_lead = S.t_bajo = S.t_rit = 1;      /* all three step next sample    */
    if (id == CH_MEL_NADA) {
        voz_callar(&S.v[V_LEAD]);
        voz_callar(&S.v[V_BAJO]);
        voz_callar(&S.v[V_RUIDO]);
    }
}

void ch_snd_tick(ch_t *g)
{
    /* One attempt, on the first tick with the sound on. */
    if (!S.activo && !S.intentado && ch_sonido_get() > 0) {
        S.intentado = true;
        S.activo = ch_audio_abrir(SND_HZ);
        if (S.activo) {
            S.cancion = g->mel_id;
            S.i_lead = S.i_bajo = S.i_rit = 0;
            S.t_lead = S.t_bajo = S.t_rit = 1;
        }
    }

    /* --- The synthesiser has the speaker ------------------------------- */
    if (S.activo) {
        int faltan;
        if (ch_sonido_get() == 0) {         /* muted: give the codec back    */
            ch_snd_fin();
            return;
        }
        if (!ch_audio_abierto()) { S.activo = false; return; }
        faltan = SND_META - ch_audio_pendiente();
        if (faltan > 0) generar(faltan);
        return;
    }

    /* --- The old one-voice sequencer, for when it is not --------------- */
    {
        const cancion_t *c;
        const nota_t *n;

        if (ch_sonido_get() < 2 || !g->mel_id || g->mel_id >= NCANCIONES) return;
        c = &CANCIONES[g->mel_id];
        if (!c->lead) return;
        if (g->mel_t) { g->mel_t--; return; }

        if (g->mel_i >= c->n_lead) {
            if (!c->bucle) { g->mel_id = CH_MEL_NADA; return; }
            g->mel_i = 0;
        }
        n = &c->lead[g->mel_i++];
        g->mel_t = n->dur;
        if (n->hz) {
            int ms = n->dur * 33 - 30;
            ch_tono(n->hz, ms < 30 ? 30 : ms);
        }
    }
}
