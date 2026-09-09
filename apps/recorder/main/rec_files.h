/*
 * Recorder - what lives on the card.
 *
 * Scanning the recordings folder, reading the WAV header and the envelope
 * drawn in the detail view.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define REC_MAX_FILES   64
#define REC_NAME_LEN    64
#define REC_PATH_LEN    224

typedef struct {
    char     name[REC_NAME_LEN];    /* name on disk, with its extension */
    uint32_t bytes;                 /* size of the file                 */
    uint32_t seconds;               /* duration according to the header */
    uint32_t sample_rate;
    time_t   mtime;                 /* 0 if the file system does not know it */
} rec_file_t;

/* Fills 'out' with whatever is in the folder, newest to oldest. Returns how
 * many it found. Files that are not WAVs are ignored. */
int rec_files_scan(rec_file_t *out, int max);

/* Full path of a recording. */
void rec_files_path(char *out, size_t out_len, const char *name);

/* Next free name, "grabacion_0007.wav". It looks at the ones already there so
 * as not to overwrite any, even if there are gaps in the numbering. */
void rec_files_next_name(char *out, size_t out_len);

/* "Grabacion 0007" from "grabacion_0007.wav": the name without the extension
 * and with the first letter capitalised. */
void rec_files_pretty(char *out, size_t out_len, const char *name);

bool rec_files_delete(const char *name);

/* Envelope of the whole file, 'bars' values of 0..100.
 *
 * It does not read the whole WAV: it jumps to the start of each stretch and
 * looks at a short window. A minute of audio is ~2 MB, and reading all of it
 * from the microSD to draw 40 little bars would leave the screen frozen for a
 * long second. */
bool rec_files_envelope(const char *name, uint8_t *out, int bars);

/* "hoy 14:03", "ayer 09:12" or "12 ene 14:03". */
void rec_files_when(char *out, size_t out_len, time_t when);

/* "1,4 MB" / "812 KB" */
void rec_files_size(char *out, size_t out_len, uint32_t bytes);
