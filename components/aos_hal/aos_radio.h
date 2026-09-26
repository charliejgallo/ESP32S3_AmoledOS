/*
 * AmoledOS - an internet radio as a source of MP3 bytes.
 *
 * Internal to the HAL, shared between the board and the simulator like
 * aos_http.c. One station at a time: a reader (a task on the board, a
 * thread on the Mac) keeps the connection, follows redirects and playlists,
 * takes the ICY metadata out of the stream and leaves the audio in a ring
 * of bytes in PSRAM; the player's decoder drinks from the ring through
 * aos_radio_read(), which never blocks. A dropped connection is retried by
 * the reader on its own, and the decoder only notices a pause.
 *
 * What the app sees of all this is aos_hal_radio_*() in aos_hal.h.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "aos_hal.h"

/* Starts the reader on 'url' (http:// or https://). Stops the one before,
 * if any. False only without memory or with a URL that is not one. */
bool aos_radio_start(const char *url);

/* Stops the reader and waits for it (a second at most: every receive times
 * out after one). The status stays readable, with the last error. */
void aos_radio_stop(void);

/* 1 when enough has arrived to start decoding, 0 not yet, -1 given up (the
 * reason is in the status). */
int  aos_radio_ready(void);

/* Up to 'max' bytes of audio. >0 bytes, 0 none right now, -1 the reader gave
 * up and the ring is empty. Never blocks. Matches aos_audio_src_fn. */
int  aos_radio_read(void *ctx, void *buf, int max);

/* The generation of the title that applies to the audio read so far, and
 * the title (UTF-8). The generation goes up with every new StreamTitle. */
uint32_t aos_radio_title_at_read(char *title, size_t len);

/* Everything known about the connection, for aos_hal_radio_status(). Fills
 * the network half; the HAL adds the station and the heard title. */
void aos_radio_fill_status(aos_radio_status_t *out);

/* Bytes of audio waiting in the ring. */
uint32_t aos_radio_buffered(void);

/* Bytes of the reader's stack never touched, 0 with no reader (board only;
 * for /api/player). */
uint32_t aos_radio_stack_free(void);
