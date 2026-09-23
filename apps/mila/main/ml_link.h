/*
 * MILA - the other watch over the ESP-NOW link (docs: HANDOFF-LINK)
 *
 * Two things to do together (DESIGN.md): a VISIT (the friend's Mila, in her
 * outfit, comes to your casita and both play) and a RACE (both solve the same
 * level; each sees how far the other is). The host (the lower MAC) picks.
 */
#pragma once

#include <stdbool.h>

typedef struct app app_t;

enum { LINK_VISIT = 1, LINK_RACE = 2 };

void ml_link_begin(app_t *a);           /* the casita's friend button        */
void ml_link_choose(app_t *a, int what);/* the host's pick in the lobby      */
void ml_link_end(app_t *a);
void ml_link_tick(app_t *a);            /* the LVGL timer                    */
bool ml_link_available(void);
