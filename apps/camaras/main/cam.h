/*
 * AmoledOS - Cameras: what the pieces of the app share.
 *
 *   cam_cfg.c    the list the portal writes (NVS) and URL parsing
 *   cam_rtsp.c   RTSP over TCP: Digest, SDP, interleaved RTP
 *   cam_http.c   MJPEG over HTTP (go2rtc and friends)
 *   cam_depay.c  RTP payloads back into NAL units (RFC 6184) and JPEG
 *                files (RFC 2435)
 *   cam_view.c   the worker's end: decode, drop when late, convert, hand
 *                the frame to the UI
 *   cam_conv.c   YUV 4:2:0 and RGB565 to the panel, scaled
 *   camaras.c    the screens and the app's life cycle
 *
 * Everything below the UI runs in the app's worker (aos_hal_worker_*) and
 * never touches LVGL. The two sides meet in cam_view_t: frame slots with one
 * writer each, and a status the worker writes and the UI reads.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAM_MAX          8
#define CAM_NAME_LEN     32
#define CAM_URL_LEN      200
#define CAM_CRED_LEN     64

/* One camera as the portal stored it. */
typedef struct {
    char name[CAM_NAME_LEN];
    char url[CAM_URL_LEN];          /* without credentials */
    char user[CAM_CRED_LEN];
    char pass[CAM_CRED_LEN];
} cam_t;

/* A URL taken apart. */
typedef struct {
    bool rtsp;                      /* rtsp:// ; otherwise http:// */
    char host[96];
    int  port;
    char path[CAM_URL_LEN];         /* from the first '/', query included */
} cam_url_t;

int  cam_cfg_load(cam_t *cams, int max);    /* how many */
int32_t cam_cfg_gen(void);
bool cam_url_parse(const char *url, cam_url_t *out);

/* ---- what the network side hands to the decoding side ------------------ */

typedef enum {
    CAM_CODEC_NONE = 0,
    CAM_CODEC_H264,
    CAM_CODEC_JPEG,
} cam_codec_t;

typedef struct cam_view cam_view_t;

/* One H.264 NAL unit, start code included, and the RTP time of its picture
 * in milliseconds (monotonic, extended from the 90 kHz clock). */
void cam_view_nal(cam_view_t *v, const uint8_t *nal, int len, int64_t pts_ms);

/* One whole JPEG file. pts_ms < 0 when the source has no clock (HTTP). */
void cam_view_jpeg(cam_view_t *v, const uint8_t *jpeg, int len, int64_t pts_ms);

/* Network accounting and state, from the worker. */
void cam_view_bytes(cam_view_t *v, int n);
void cam_view_codec(cam_view_t *v, cam_codec_t codec);

typedef enum {
    CAM_ST_IDLE = 0,
    CAM_ST_CONNECTING,
    CAM_ST_NEGOTIATING,             /* RTSP handshake / HTTP headers */
    CAM_ST_WAITING,                 /* connected, no picture yet */
    CAM_ST_LIVE,
    CAM_ST_ERROR,                   /* detail says why; the worker retries */
} cam_state_t;

void cam_view_state(cam_view_t *v, cam_state_t st, const char *detail);

/* ---- sessions (run until stop or failure; return false with a reason) --- */

bool cam_rtsp_run(cam_view_t *v, const cam_t *cam, const cam_url_t *url,
                  char *why, size_t why_len);
bool cam_http_run(cam_view_t *v, const cam_t *cam, const cam_url_t *url,
                  char *why, size_t why_len);

/* Base64 (RFC 4648) for sprop-parameter-sets and HTTP Basic. */
int  cam_b64_decode(const char *in, int in_len, uint8_t *out, int out_max);
int  cam_b64_encode(const uint8_t *in, int in_len, char *out, int out_max);
