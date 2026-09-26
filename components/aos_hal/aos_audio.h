/*
 * AmoledOS - audio files in, PCM out.
 *
 * The part of the player that knows file formats, kept apart from the part
 * that knows the codec so the simulator can build it too (and so it can be
 * checked on the Mac against ffmpeg, sample for sample). Two formats: 16-bit
 * PCM WAV, which needs no decoding, and MP3 through minimp3 (CC0, vendored
 * in minimp3/ with one local change: the 16 KB scratch comes from the heap).
 *
 * Every buffer is on the heap and, on the board, in PSRAM: what the decoder
 * costs in internal RAM is the stack of whoever calls aos_audio_read().
 *
 * Not thread-safe: one decoder belongs to one task.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AOS_AUDIO_WAV = 1,
    AOS_AUDIO_MP3,
} aos_audio_format_t;

typedef struct {
    aos_audio_format_t format;
    uint32_t sample_rate;
    uint8_t  channels;          /* the file's; aos_audio_read() interleaves them */
    uint16_t kbps;              /* MP3: of the first frame (VBR: nominal); WAV: PCM */
    bool     vbr;               /* a Xing header said so */
    uint32_t duration_ms;       /* 0 when it cannot be known without decoding */
    char     title[96];         /* ID3 tags, UTF-8, empty when absent */
    char     artist[96];
    char     album[64];
    uint32_t cover_offset;      /* the embedded picture (APIC), 0 = none */
    uint32_t cover_size;
    char     cover_mime[16];
} aos_audio_info_t;

typedef struct aos_audio aos_audio_t;

/* Opens the file and reads what can be read without decoding. NULL if it is
 * not something we play; 'info' is filled either way as far as it got. */
aos_audio_t *aos_audio_open(const char *path, aos_audio_info_t *info);

/* Up to max_frames frames (one sample per channel each), interleaved. Returns
 * how many, 0 at the end of the file, -1 on a read error. */
int aos_audio_read(aos_audio_t *a, int16_t *pcm, int max_frames);

/* Jumps to 'ms' from the start; returns where it really landed (MP3 lands on a
 * frame, 26 ms apart at 44.1 kHz). */
uint32_t aos_audio_seek(aos_audio_t *a, uint32_t ms);

void aos_audio_close(aos_audio_t *a);

/* MP3 from something that is not a file: an internet radio (aos_radio.c).
 * 'fn' gives up to 'max' bytes: >0 bytes, 0 none right now, -1 no more ever.
 * No tags, no length, no seeking. aos_audio_read() on it returns 0 both when
 * the source is momentarily dry and when it has ended: aos_audio_ended()
 * tells them apart. */
typedef int (*aos_audio_src_fn)(void *ctx, void *buf, int max);
aos_audio_t *aos_audio_open_src(aos_audio_src_fn fn, void *ctx, aos_audio_info_t *info);
bool aos_audio_ended(const aos_audio_t *a);

/* True for the extensions the player takes (.wav, .mp3). */
bool aos_audio_is_playable(const char *name);

/* PCM frames that came out of the MP3 decoder and the microseconds it took
 * (reading the card not included), since open. 0 for WAV. */
void aos_audio_cost(const aos_audio_t *a, uint32_t *frames, uint64_t *us);

/* --------------------------------------------------------------------------
 * A folder as a playlist
 *
 * The playable files of one folder, sorted by name (the card's order is the
 * order they were copied in). Names live in one PSRAM block.
 * -------------------------------------------------------------------------- */

typedef struct {
    char     dir[160];
    int      count;
    char    *names;             /* count names, each ending in '\0' */
    uint32_t *offset;           /* where each one starts in 'names' */
} aos_audio_list_t;

/* Fills the list with the folder's playable files. False if the folder cannot
 * be opened; an empty folder is true with count 0. */
bool aos_audio_list_scan(aos_audio_list_t *list, const char *dir, int max);
void aos_audio_list_free(aos_audio_list_t *list);
const char *aos_audio_list_name(const aos_audio_list_t *list, int index);
int  aos_audio_list_find(const aos_audio_list_t *list, const char *name);

/* The order the list uses: case ignored, digit runs compared as numbers
 * ("2 - x" before "10 - y"). For whoever lists the folders beside it. */
int  aos_audio_name_cmp(const char *a, const char *b);
