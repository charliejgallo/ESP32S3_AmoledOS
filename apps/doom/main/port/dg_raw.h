/*
 * AmoledOS - Doom: memory that is NOT the engine's.
 *
 * The frame slots and the palette outlive the engine's run (the app is
 * still blitting the last frame when the engine leaves and frees all of its
 * own blocks), so they come from here, off the list, and the app gives them
 * back from destroy().
 */
#ifndef DG_RAW_H
#define DG_RAW_H

#include <stdbool.h>
#include <stddef.h>

void *dg_raw_alloc(size_t n, bool internal);
void  dg_raw_free(void *p);

#endif
