/*
 * CHATARRA - music
 *
 * A one-voice sequencer over `aos_hal_beep()`, which is all there is: the
 * board's ES8311 plays one tone at a time and the HAL's API enqueues and plays
 * from its own task. So a melody is a list of (frequency, duration) and a
 * counter advancing with the game's clock.
 *
 * ---------------------------------------------------------------------------
 * WHERE IT PLAYS AND WHERE IT DOES NOT, AND WHY
 * ---------------------------------------------------------------------------
 *
 * The music plays on the TITLE screen, in COMBAT and in the short stingers
 * -victory, level up, defeat-. On the map NOTHING plays but the effects.
 *
 * It is not that a town melody is missing: it is that a one-voice buzzer
 * repeating a twenty-second loop while you walk for half an hour through 51
 * rooms becomes unbearable, and the user ends up switching the sound off
 * entirely. Leaving the map silent, the combat's music COMES IN, which is
 * precisely what you want to happen when an opponent appears.
 *
 * ---------------------------------------------------------------------------
 * THE SETTING DOES NOT GO IN THE SAVE FILE
 * ---------------------------------------------------------------------------
 *
 * It goes in an NVS preference and not in `ch_save_t`. Two reasons: it is a
 * preference of the device and not of the progress, and -the one that rules-
 * adding a field to the saved structure changes its size, and the loader
 * compares that size and REJECTS old saves. Adding an option cannot cost
 * anybody their game.
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

typedef struct { uint16_t hz; uint8_t dur; } nota_t;

/* --------------------------------------------------------------------------
 * The title: calm, in a major key, it loops.
 * -------------------------------------------------------------------------- */
static const nota_t MEL_TITULO[] = {
    {C4,6},{E4,6},{G4,6},{C5,10},{G4,4},{E4,4},{F4,6},{A4,6},{C5,10},{SIL,4},
    {D4,6},{F4,6},{A4,6},{D5,10},{A4,4},{F4,4},{G4,6},{B4,6},{D5,12},{SIL,6},
    {C4,6},{E4,6},{G4,6},{C5,10},{E5,8},{D5,8},{C5,14},{SIL,10},
};

/* --------------------------------------------------------------------------
 * Combat: a short march with drive, in a minor key.
 * -------------------------------------------------------------------------- */
static const nota_t MEL_COMBATE[] = {
    {A3,3},{A3,3},{C4,3},{E4,3},{A4,6},{E4,3},{C4,3},
    {A3,3},{A3,3},{C4,3},{E4,3},{G4,6},{E4,3},{C4,3},
    {F3,3},{F3,3},{A3,3},{C4,3},{F4,6},{C4,3},{A3,3},
    {G3,3},{G3,3},{B3,3},{D4,3},{G4,6},{D4,3},{B3,3},
    {A4,4},{C5,4},{E5,4},{C5,4},{A4,4},{G4,4},{F4,4},{E4,8},
    {SIL,4},
};

/* --------------------------------------------------------------------------
 * The sub-boss: lower, tighter, with a semitone that bites.
 * -------------------------------------------------------------------------- */
static const nota_t MEL_JEFE[] = {
    {C3,2},{C3,2},{DS5,2},{C3,2},{C3,2},{D5,2},{C3,2},{C3,2},
    {C3,2},{C3,2},{DS5,2},{C3,2},{C3,2},{C5,2},{C3,2},{C3,2},
    {D3,2},{D3,2},{F5,2},{D3,2},{D3,2},{DS5,2},{D3,2},{D3,2},
    {C3,2},{C3,2},{DS5,2},{C3,2},{C3,2},{D5,2},{C3,2},{C3,2},
    {G5,4},{F5,4},{DS5,4},{D5,4},{C5,8},{SIL,4},
};

/* --------------------------------------------------------------------------
 * Stingers. They play once and fall silent.
 * -------------------------------------------------------------------------- */
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

#define MEL(a)  { (a), (uint8_t)(sizeof(a) / sizeof((a)[0])) }

static const struct { const nota_t *n; uint8_t largo; } MELODIAS[] = {
    { NULL, 0 },                    /* CH_MEL_NADA                           */
    MEL(MEL_TITULO),
    MEL(MEL_COMBATE),
    MEL(MEL_JEFE),
    MEL(MEL_VICTORIA),
    MEL(MEL_NIVEL),
    MEL(MEL_DERROTA),
    MEL(MEL_FINAL),
};

#define NMELODIAS ((int)(sizeof(MELODIAS) / sizeof(MELODIAS[0])))

/* Which ones loop and which play once. */
static bool en_bucle(int id)
{
    return id == CH_MEL_TITULO || id == CH_MEL_COMBATE || id == CH_MEL_JEFE;
}

void ch_snd_melodia(ch_t *g, int id)
{
    if (id < 0 || id >= NMELODIAS) id = CH_MEL_NADA;
    if (g->mel_id == id) return;            /* that one is already playing   */
    g->mel_id = (uint8_t)id;
    g->mel_i = 0;
    g->mel_t = 0;
}

void ch_snd_tick(ch_t *g)
{
    const nota_t *n;

    if (ch_sonido_get() < 2 || !g->mel_id || g->mel_id >= NMELODIAS) return;
    if (!MELODIAS[g->mel_id].n) return;

    if (g->mel_t) { g->mel_t--; return; }

    if (g->mel_i >= MELODIAS[g->mel_id].largo) {
        if (!en_bucle(g->mel_id)) { g->mel_id = CH_MEL_NADA; return; }
        g->mel_i = 0;
    }
    n = &MELODIAS[g->mel_id].n[g->mel_i++];
    g->mel_t = n->dur;

    /* Silence is a note that does not sound, not a gap in the table: that way
     * the duration is counted all the same and the bar does not shift. */
    if (n->hz) {
        /* 30 ms less than the duration so the next note's attack can be heard.
         * Without that gap two identical notes in a row sound like a single
         * long one. */
        int ms = n->dur * 33 - 30;
        ch_tono(n->hz, ms < 30 ? 30 : ms);
    }
}
