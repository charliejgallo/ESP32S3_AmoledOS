/*
 * AmoledOS - Video: a sequential reader of MJPEG-in-AVI files.
 *
 * An AVI is a RIFF file: a 'hdrl' list with the main header (frame period,
 * frame count, size) and a 'movi' list with one chunk per frame, '00dc'.
 * Nothing here needs the index at the end: the frames are read in order,
 * and skipping one is a seek past its chunk, so catching up after a slow
 * frame costs nothing.
 *
 * tools/video_convert.sh writes exactly this shape (video only; the audio
 * goes to a .wav beside it, see video.c).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE    *file;
    uint32_t us_per_frame;      /* avih.dwMicroSecPerFrame */
    uint32_t total_frames;      /* avih.dwTotalFrames      */
    uint16_t width;
    uint16_t height;
    long     movi_start;        /* first chunk of the 'movi' list */
    long     movi_end;
    long     pos;               /* next chunk header to read */
    long     file_pos;          /* where the OS file position really is */
    uint32_t next_index;        /* frames handed out so far */
} vd_avi_t;

/* Opens and parses the headers. false if it is not an AVI with a 'movi'. */
bool vd_avi_open(vd_avi_t *avi, const char *path);
void vd_avi_close(vd_avi_t *avi);

/* Next video frame. Copies it into 'buf' (up to 'cap' bytes) and returns its
 * size; with buf == NULL it only skips the frame. 0 at the end, -1 on a read
 * error or a frame bigger than 'cap' (which is also skipped). */
int  vd_avi_next(vd_avi_t *avi, uint8_t *buf, int cap);

void vd_avi_rewind(vd_avi_t *avi);
