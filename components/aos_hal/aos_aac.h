/*
 * AmoledOS - one AAC frame in, PCM out (branch aac).
 *
 * The same three calls on both platforms: on the board, Espressif's
 * esp_audio_codec (AAC-LC, HE-AAC and HE-AACv2, a prebuilt library under
 * its modified MIT licence, for Espressif chips); in the simulator,
 * libavcodec (sim/sim_aac.c). aos_audio.c finds the ADTS frames and hands
 * them over one at a time; nothing above it knows which decoder it got.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct aos_aac aos_aac_t;

/* The most a frame decodes to: 2048 samples per channel (HE-AAC doubles
 * the 1024 of AAC-LC), two channels. */
#define AOS_AAC_MAX_SAMPLES (2048 * 2)

aos_aac_t *aos_aac_open(void);

/* One whole ADTS frame (header included). Returns samples per channel
 * written to 'pcm' (interleaved, 'channels' of them at 'rate'), 0 for a
 * frame that gave nothing yet, -1 for one it could not decode. */
int  aos_aac_decode(aos_aac_t *d, const uint8_t *frame, int len, int16_t *pcm,
                    uint32_t *rate, uint8_t *channels);

void aos_aac_close(aos_aac_t *d);
