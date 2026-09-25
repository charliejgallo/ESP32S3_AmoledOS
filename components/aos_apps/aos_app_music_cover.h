/*
 * AmoledOS - the Music app's cover (aos_app_music_cover.c).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define COVER_PX    112         /* the art square; a multiple of 8 for the scaler */

/* Asks for the cover of 'track': the embedded picture if cover_size is not 0,
 * else a cover.jpg/folder.jpg/front.jpg beside it. Returns at once; the
 * result comes through music_cover_take(). */
void music_cover_request(const char *track, uint32_t cover_offset, uint32_t cover_size);

/* True once the answer to the last request is in: *px is the COVER_PX square
 * RGB565 (or NULL: no cover, draw the note), owned by the module. *old, when
 * not NULL, is the previous one: free() it after the screen stops using it. */
bool music_cover_take(const uint16_t **px, uint16_t **old);
