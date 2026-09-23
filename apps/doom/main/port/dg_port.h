/*
 * AmoledOS - Doom: what the port's files tell each other.
 */
#ifndef DG_PORT_H
#define DG_PORT_H

#include <stdbool.h>
#include <stdint.h>

bool dg_video_alloc(bool swap_bytes);   /* the frame slots, before the worker */
void dg_video_release(void);            /* after it */
void dg_input_reset(void);
void dg_sound_shutdown(void);
void dg_opl_mix(int32_t *acc, int n);   /* adds n samples of music */

#endif
