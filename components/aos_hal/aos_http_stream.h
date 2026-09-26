/*
 * AmoledOS - aos_http.c's transport, for a connection that stays open.
 *
 * Internal to the HAL (aos_radio.c): apps have aos_hal_http_* for requests
 * and aos_hal_tcp_* for their own sockets. Plain or TLS behind the same four
 * calls, with the trust store and the session cache of aos_http.c.
 */
#pragma once

#include <stdbool.h>

typedef struct aos_http_stream aos_http_stream_t;

/* Creates aos_http.c's lock if nobody has yet. Call it from the thread that
 * starts things (the UI, or the portal), not from the reader. */
void aos_http_stream_init(void);

/* 0, or one of the AOS_HTTP_ERR_* of aos_hal.h. */
int  aos_http_stream_open(aos_http_stream_t **out, const char *host, int port, bool tls);
bool aos_http_stream_send(aos_http_stream_t *s, const char *buf, int len);
/* >0 bytes, 0 closed by the other side, -2 nothing within a second, -1 broken */
int  aos_http_stream_recv(aos_http_stream_t *s, void *buf, int max);
void aos_http_stream_close(aos_http_stream_t *s);
