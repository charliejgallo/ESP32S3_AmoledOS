/*
 * IMA ADPCM, the plain one: 16-bit PCM in, 4 bits per sample out. The state
 * (predictor and step index) travels with every frame so a lost frame costs
 * its 29 ms and nothing after it. No LVGL, no HAL: it compiles on the Mac.
 */
#pragma once

#include <stdint.h>

typedef struct {
    int16_t pred;
    uint8_t index;
} wk_adpcm_t;

/* n samples in, n/2 bytes out (n even). The state is updated. */
void wk_adpcm_encode(wk_adpcm_t *st, const int16_t *pcm, int n, uint8_t *out);
/* n samples out of n/2 bytes. */
void wk_adpcm_decode(wk_adpcm_t *st, const uint8_t *in, int n, int16_t *pcm);
