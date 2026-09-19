/*
 * AmoledOS - Link: the seam between the common layer and the radio.
 *
 * aos_link.c is the same file on the board and in the simulator: beacons,
 * neighbours, the bump, the partner, the test protocol, the reliable and
 * the fast channel. Underneath it, one raw layer per platform:
 * aos_link_esp32.c (ESP-NOW, encrypted peers, a task) and the UDP version
 * in sim/hal_sim.c (one port per simulator on 127.0.0.1). This header is
 * what the two agree on. Nothing here is for apps.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- the raw layer, implemented per platform ----------------------------- */

bool aos_link_raw_start(uint8_t own_mac[6], uint32_t *version);
void aos_link_raw_stop(void);
/* One frame out, to 'mac' or to everyone (NULL). Returns false if the radio
 * refused it (queue full, no peer). The completion comes back through
 * aos_link_on_sent(). */
bool aos_link_raw_send(const uint8_t mac[6], const void *data, size_t len);
/* The partner's key: frames to and from that MAC go encrypted from now on.
 * NULL key forgets it. */
bool aos_link_raw_set_partner(const uint8_t mac[6], const uint8_t lmk[16]);
uint32_t aos_link_now_ms(void);
/* The common layer's state is touched by the radio's delivery and by the
 * apps: the raw layer provides the lock that keeps them apart (a critical
 * section on the board, nothing in the single-threaded simulator). */
void aos_link_lock(void);
void aos_link_unlock(void);

/* ---- what the raw layer calls into the common layer ---------------------- */

/* A frame arrived. Called from whatever context the raw layer runs in; the
 * common layer copies and returns. */
void aos_link_on_frame(const uint8_t mac[6], int8_t rssi, const uint8_t *data, size_t len);
/* The last raw send finished (acknowledged at the MAC level or not). */
void aos_link_on_sent(bool ok);
/* The common layer's clockwork: beacons, timeouts, the test sender, the
 * reliable channel's retransmits. Call it every ~10 ms from the platform's
 * link task or loop. */
void aos_link_poll(void);
/* A knock, from the IMU or from a key. */
void aos_link_bump_hint(void);
