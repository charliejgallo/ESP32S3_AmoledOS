/*
 * Tuner - the part that does the maths
 *
 * Not one line of LVGL, on purpose: that way it is tested without a screen
 * with tools/af_harness.c, just like truco's rules. What is here is what is
 * worth getting genuinely right, because a tuner that is out by an octave
 * looks just as convincing as one that works.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* -------------------------------------------------------------------------- */
/* Pitch detection                                                             */
/* -------------------------------------------------------------------------- */

typedef struct {
    float hz;           /* 0 if there is no clear tone */
    float clarity;      /* 0..1: the NSDF's peak, it doubles as a signal meter */
} af_pitch_t;

#define AF_HZ_MIN   30.0f
#define AF_HZ_MAX   1300.0f

/* Boundary between the search's two stages (see af_dsp.c). They overlap on
 * purpose so no note falls into the gap. */
#define AF_HZ_CORTE_ALTO   500.0f   /* from here up, at full rate */
#define AF_HZ_CORTE_BAJO   600.0f   /* from here down, decimated */
#define AF_DEC             4        /* decimation factor of the coarse stage */

/* Working memory for af_pitch(): the window in float, the decimated window and
 * the NSDF curve. */
#define AF_SCRATCH_FLOATS(n, rate) \
    ((n) + (n) / AF_DEC + (int)((rate) / (int)AF_HZ_MIN) + 8)

/* Estimates the fundamental frequency of 'x' by NSDF (McLeod).
 *
 * 'scratch' is the caller's working memory, AF_SCRATCH_FLOATS floats. It is
 * asked for from outside so nothing is allocated here.
 *
 * MIND WHERE IT LIVES: the inner loop walks it ~900 thousand times per
 * analysis. Measured on the board, that is not a detail. */
af_pitch_t af_pitch(const int16_t *x, int n, uint32_t rate,
                    float *scratch, int scratch_len);

/* -------------------------------------------------------------------------- */
/* Nearest note                                                                */
/* -------------------------------------------------------------------------- */

typedef struct {
    const char *name;   /* "C", "C#", ... */
    int   octave;       /* 4 for the A at 440 */
    int   midi;
    float ref_hz;       /* that note's exact frequency */
    float cents;        /* -50..+50 relative to ref_hz */
} af_note_t;

void af_note_from_hz(float hz, float a4, af_note_t *out);
float af_note_hz(int midi, float a4);

/* -------------------------------------------------------------------------- */
/* A weighting                                                                 */
/* -------------------------------------------------------------------------- */

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} af_biquad_t;

typedef struct {
    af_biquad_t s[3];
    float       gain;       /* normalises to 0 dB at 1 kHz */
    uint32_t    rate;
} af_aweight_t;

/* Builds the filter for that sample rate, by bilinear transform of the
 * standard's poles (20.6 / 107.7 / 737.9 / 12194 Hz). It is computed rather
 * than coming from a table because the capture rate is not always the one the
 * app asked for: it is fixed by whoever opens the microphone first. */
void  af_aweight_init(af_aweight_t *w, uint32_t rate);
void  af_aweight_reset(af_aweight_t *w);
float af_aweight_run(af_aweight_t *w, float x);

/* RMS of the already weighted block, in dBFS (0 dBFS = a full-scale square
 * wave). Returns -120 for absolute silence. */
float af_aweight_block_dbfs(af_aweight_t *w, const int16_t *x, int n);
