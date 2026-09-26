/*
 * AmoledOS - Cameras: live view of the house's cameras on the watch.
 *
 * Two screens. The list is the cameras the portal's /camaras page saved
 * (cam_cfg.c); tapping one opens the viewer, which starts the app's worker
 * (cam_view_worker) on the network and the decoders and pushes each picture
 * it hands over straight to the panel, past LVGL, the way the Video app
 * does. Tap the picture to switch between the whole picture (fit, with
 * bands above and below where the name and the numbers live) and the whole
 * screen (fill, cropped at the sides). Swipe back or press the button to go
 * back to the list, which stops the worker and closes the connection.
 *
 * Two kinds of URL, one list:
 *
 *   rtsp://   the camera itself. H.264 constrained baseline decoded on the
 *             board (tinyh264, aos_hal_h264_*), or MJPEG over RTP.
 *   http://   an MJPEG stream from a transcoder (go2rtc), for whatever the
 *             board cannot decode itself.
 *
 * docs/CAMERAS.md has what the board can do, measured, and how to set a
 * camera up for it.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_theme.h"
#include "aos_ui.h"

#include "cam.h"
#include "cam_view.h"

#if !defined(AOS_SIM)
#include "esp_heap_caps.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK_MS          8
#define STATS_MS         1000
#define LOG_EVERY        10             /* stats periods per log line */
#define WORKER_STACK     12288
/* The worker decodes on core 0, with WiFi; LVGL, the conversion and the
 * panel's SPI live on core 1 (v0.4.4 pinned them there). On core 1 the
 * decoder shared the CPU with the conversion and averaged 140 ms a picture;
 * on core 0, 74-80 ms a P frame. */
#define WORKER_CORE      0
/* Below LVGL's 4, and that is not a detail. app_main's loop runs on core 0
 * at priority 1 and takes the LVGL lock for aos_ui_tick(); a worker on core 0
 * at 5 that decodes 90 % of the time (100 % while it skips to a keyframe)
 * starved it WITH the lock held, and LVGL, idle on core 1, waited up to
 * 2.7 s for it: the picture froze, the heartbeat came late and the portal
 * timed out (measured 2026-09-26, /api/pmu?tasks: core 1 67 % idle during
 * the freezes). Priority inheritance lifts the holder to LVGL's 4, which only
 * helps if the worker is below it. */
#define WORKER_PRIO      3
#define STRIP_ROWS       10             /* half the panel's transfer (AOS_DRAW_ROWS): 15 KB of internal RAM back */
#define KEY_MODE         "cam_mode"
#define FRAME_BYTES      ((size_t)AOS_SCREEN_W * AOS_SCREEN_H * 2)

/* A security camera on its arm: the body, the lens, the bracket. */
static const uint8_t CAM_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER,  18,  22, 10, 26, 3, AIC_C_TEXT, 255),            /* arm    */
    AIC_RECT(AIC_CENTER,  18,  35, 26,  8, 3, AIC_C_TEXT, 255),            /* foot   */
    AIC_RECT(AIC_CENTER,  -6,  -8, 66, 34, 10, AIC_C_TEXT, 255),           /* body   */
    AIC_INTO,
    AIC_RECT(AIC_LEFT_MID, 8, 0, 20, 20, AIC_CIRCLE, AIC_C_BG, 255),      /* lens   */
    AIC_INTO,
    AIC_RECT(AIC_CENTER, 0, 0, 9, 9, AIC_CIRCLE, AIC_C_TEAL, 255),
    AIC_OUT,
    AIC_RECT(AIC_CENTER, 18, -7, 6, 6, AIC_CIRCLE, AIC_C_RED, 255),        /* led    */
    AIC_OUT,
    AIC_END
};

typedef struct {
    /* the list */
    cam_t        cams[CAM_MAX];
    int          count;
    int32_t      gen;
    lv_obj_t    *page;
    lv_obj_t    *list_view;

    /* the viewer */
    lv_obj_t    *view_obj;
    lv_obj_t    *name_label;
    lv_obj_t    *status_label;
    lv_obj_t    *message;
    lv_timer_t  *timer;
#ifdef AOS_SIM
    lv_obj_t    *canvas;
    uint16_t    *sim_px;            /* the canvas's buffer, native RGB565 */
#endif
    bool         viewing;
    bool         leave;
    bool         shown;             /* a picture reached the panel */
    int          last_x, last_y, last_w, last_h;
    uint16_t    *strip[2];          /* internal, DMA-capable: AOS_SCREEN_W x STRIP_ROWS */
    uint32_t     last_state_gen;
    int          last_state;

    /* stats */
    uint64_t     stats_t0;
    uint32_t     shown_count;
    uint32_t     p_pictures, p_bytes, p_dec, p_dropped, p_errors, p_i, p_ims, p_skips, p_waits;
    uint32_t     render_ms, renders;
    uint64_t     last_tick;
    uint32_t     gap_max;           /* longest wait between two timer calls */
    uint32_t     blit_us;           /* time inside aos_hal_display_blit, summed */
    int          log_n;

    cam_view_t  *v;                 /* the worker's state and what it hands over */
} app_t;

static app_t s_app;

/* ---- the viewer --------------------------------------------------------------- */

static void *alloc_dma(size_t n)
{
#if !defined(AOS_SIM)
    return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#else
    return malloc(n);
#endif
}

static void free_dma(void *p)
{
#if !defined(AOS_SIM)
    heap_caps_free(p);
#else
    free(p);
#endif
}

static void free_strips(app_t *a)
{
    for (int i = 0; i < 2; i++) {
        free_dma(a->strip[i]);
        a->strip[i] = NULL;
    }
    cam_conv_free();
}

static void set_message(app_t *a, const char *text)
{
    if (!text) {
        lv_obj_add_flag(a->message, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(a->message, text);
    lv_obj_remove_flag(a->message, LV_OBJ_FLAG_HIDDEN);
}

static void show_bands(app_t *a, bool on)
{
    if (on) {
        lv_obj_remove_flag(a->name_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(a->status_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(a->name_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(a->status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void stop(app_t *a)
{
    if (!a->viewing) {
        return;
    }
    a->viewing = false;
    aos_hal_worker_stop();              /* joins: nothing below runs under it */
    cam_view_release(a->v);
    free_strips(a);
    lv_obj_add_flag(a->view_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(a->list_view, LV_OBJ_FLAG_HIDDEN);
    /* LVGL never knew about the pictures: repaint all of it. */
    lv_obj_invalidate(lv_screen_active());
}

static void open_camera(app_t *a, int index)
{
    cam_view_t *v = a->v;
    memset(v, 0, sizeof(*v));
    v->cam = a->cams[index];
    int32_t mode = CAM_FIT;
    aos_hal_pref_get_i32(KEY_MODE, &mode);
    v->mode = mode == CAM_FILL ? CAM_FILL : CAM_FIT;

    lv_obj_add_flag(a->list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(a->view_obj, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(a->name_label, v->cam.name);
    lv_label_set_text(a->status_label, "");
    show_bands(a, true);
    a->shown = false;
    a->last_w = 0;
    a->last_h = 0;
    a->last_state = -1;
    a->last_state_gen = 0;
    a->stats_t0 = aos_hal_uptime_ms();
    a->shown_count = 0;
    a->p_pictures = a->p_bytes = a->p_dec = a->p_dropped = a->p_errors = 0;
    a->p_i = a->p_ims = a->p_skips = a->p_waits = 0;
    a->render_ms = a->renders = 0;
    a->log_n = 0;

    if (!cam_url_parse(v->cam.url, &v->url)) {
        set_message(a, _("La dirección de esta cámara\nno es rtsp:// ni http://"));
        return;
    }
    if (aos_hal_net_state() != AOS_NET_CONNECTED) {
        set_message(a, _("Sin WiFi"));
        return;
    }
    /* Two strips of internal RAM: the conversion writes one while the
     * panel's DMA reads the other, and neither touches PSRAM. */
    for (int i = 0; i < 2; i++) {
        a->strip[i] = alloc_dma((size_t)AOS_SCREEN_W * STRIP_ROWS * 2);
    }
    if (!a->strip[0] || !a->strip[1] || !cam_conv_init()) {
        free_strips(a);
        set_message(a, _("Sin memoria para la imagen"));
        return;
    }
    set_message(a, _("Conectando…"));
    v->state = CAM_ST_CONNECTING;
    a->viewing = true;
    if (!aos_hal_worker_start_on("aos_cam", cam_view_worker, v, WORKER_STACK, WORKER_CORE, WORKER_PRIO)) {
        a->viewing = false;
        free_strips(a);
        set_message(a, _("No hay lugar para la tarea de video"));
        return;
    }
    aos_hal_log("camaras", "open %s (%s)", v->cam.name, v->url.rtsp ? "rtsp" : "http");
}

/* Where each strip goes: the panel, or on the desktop the canvas. */
static void strip_out(void *ctx, int x, int y, int w, int h, const uint16_t *px)
{
    app_t *a = ctx;
#ifdef AOS_SIM
    if (a->sim_px) {
        for (int r = 0; r < h; r++) {
            const uint16_t *src = px + r * w;
            uint16_t *dst = a->sim_px + (y + r) * AOS_SCREEN_W + x;
            for (int c = 0; c < w; c++) {
                dst[c] = (uint16_t)((src[c] >> 8) | (src[c] << 8));
            }
        }
    }
#else
    uint64_t t0 = aos_hal_uptime_ms();
    aos_hal_display_blit(x, y, w, h, px);
    a->blit_us += (uint32_t)(aos_hal_uptime_ms() - t0);  /* ms, despite the name */
#endif
}

static bool present(app_t *a)
{
    int x, y, w, h;
    uint32_t ms;
    if (!cam_view_render(a->v, a->strip, STRIP_ROWS, strip_out, a, &x, &y, &w, &h, &ms)) {
        return false;
    }
    bool moved = x != a->last_x || y != a->last_y || w != a->last_w || h != a->last_h;
    if (moved && a->last_w) {
        /* Fit <-> fill: the bands come back, or go. LVGL repaints the whole
         * view once, black, and the next picture covers it again. */
        lv_obj_invalidate(a->view_obj);
#ifdef AOS_SIM
        if (a->sim_px) {
            memset(a->sim_px, 0, FRAME_BYTES);
            a->last_w = 0;              /* repaint the new shape whole next time */
        }
#endif
    }
    a->last_x = x;
    a->last_y = y;
    a->last_w = w;
    a->last_h = h;
#ifdef AOS_SIM
    if (a->canvas) {
        lv_obj_invalidate(a->canvas);
    }
#endif
    a->render_ms += ms;
    a->renders++;
    a->shown = true;
    a->shown_count++;
    return true;
}

static const char *state_text(const cam_view_t *v)
{
    switch (v->state) {
    case CAM_ST_CONNECTING:  return _("Conectando…");
    case CAM_ST_NEGOTIATING: return _("Abriendo el video…");
    case CAM_ST_WAITING:     return _("Esperando la imagen…");
    default:                 return NULL;
    }
}

static void update_stats(app_t *a, uint64_t now)
{
    cam_view_t *v = a->v;
    uint64_t dt = now - a->stats_t0;
    if (dt < STATS_MS) {
        return;
    }
    uint32_t pics = v->pictures - a->p_pictures;
    uint32_t bytes = v->bytes - a->p_bytes;
    uint32_t dec = v->dec_ms - a->p_dec;
    uint32_t dropped = v->dropped - a->p_dropped;
    uint32_t errors = v->errors - a->p_errors;
    a->p_pictures = v->pictures;
    a->p_bytes = v->bytes;
    a->p_dec = v->dec_ms;
    a->p_dropped = v->dropped;
    a->p_errors = v->errors;
    uint32_t ipics = v->i_pics - a->p_i, ims = v->i_ms - a->p_ims, skips = v->skips - a->p_skips;
    a->p_i = v->i_pics;
    a->p_ims = v->i_ms;
    a->p_skips = v->skips;
    uint32_t waits = v->waits_ms - a->p_waits;
    a->p_waits = v->waits_ms;
    uint32_t dmax = v->dec_ms_max;
    v->dec_ms_max = 0;
    uint32_t render_avg = a->renders ? a->render_ms / a->renders : 0;
    uint32_t blit_avg = a->renders ? a->blit_us / a->renders : 0;
    uint32_t gap = a->gap_max;
    a->render_ms = a->renders = 0;
    a->blit_us = 0;
    uint32_t fps10 = (uint32_t)(a->shown_count * 10000ull / dt);
    uint32_t kbps = (uint32_t)(bytes * 8ull / dt);
    a->shown_count = 0;
    a->stats_t0 = now;

    if (v->state == CAM_ST_LIVE) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%u.%u fps  ·  %dx%d  ·  %u kb/s",
                 (unsigned)(fps10 / 10), (unsigned)(fps10 % 10),
                 v->src_w, v->src_h, (unsigned)kbps);
        lv_label_set_text(a->status_label, buf);
    }
    if (++a->log_n % LOG_EVERY == 0) {
        a->gap_max = 0;
        uint32_t pp = pics - ipics;
        aos_hal_log("camaras", "%u.%u fps shown, %u decoded (I %u: %u ms, P/J %u: %u ms, max %u), "
                    "render %u ms (blit %u), gap %u ms, wait %u ms, lag %d ms, -%u dropped (%u skips), %u errors, "
                    "%u kb/s, %dx%d %s",
                    (unsigned)(fps10 / 10), (unsigned)(fps10 % 10), (unsigned)pics,
                    (unsigned)ipics, ipics ? (unsigned)(ims / ipics) : 0,
                    (unsigned)pp, pp ? (unsigned)((dec - ims) / pp) : 0, (unsigned)dmax,
                    (unsigned)render_avg, (unsigned)blit_avg, (unsigned)gap, (unsigned)waits,
                    (int)v->lag_ms, (unsigned)dropped, (unsigned)skips, (unsigned)errors, (unsigned)kbps,
                    v->src_w, v->src_h,
                    v->codec == CAM_CODEC_H264 ? "H264" : v->codec == CAM_CODEC_JPEG ? "JPEG" : "-");
    }
}

static void frame_cb(lv_timer_t *timer)
{
    app_t *a = lv_timer_get_user_data(timer);
    if (a->leave) {
        a->leave = false;
        stop(a);
        return;
    }
    if (!a->viewing) {
        return;
    }
    cam_view_t *v = a->v;
    uint64_t now = aos_hal_uptime_ms();
    if (a->last_tick && now - a->last_tick > a->gap_max) {
        a->gap_max = (uint32_t)(now - a->last_tick);
    }
    a->last_tick = now;

    /* The newest picture, if there is one the panel has not shown. */
    if (present(a) && !lv_obj_has_flag(a->message, LV_OBJ_FLAG_HIDDEN)) {
        set_message(a, NULL);       /* also after a reconnection */
    }

    /* The words: state changes and errors. */
    if (v->state != a->last_state || v->detail_gen != a->last_state_gen) {
        a->last_state = v->state;
        a->last_state_gen = v->detail_gen;
        if (v->undecodable) {
            set_message(a, _("Este video no se puede ver en el reloj.\n"
                             "Pasá la cámara a H.264 Baseline\no a MJPEG."));
        } else if (v->state == CAM_ST_ERROR) {
            char msg[200];
            snprintf(msg, sizeof(msg), "%s\n\n%s", v->detail, _("Reintentando…"));
            set_message(a, msg);
            lv_label_set_text(a->status_label, "");
        } else if (state_text(v) && !a->shown) {
            set_message(a, state_text(v));
        }
    }
    if (v->undecodable && lv_obj_has_flag(a->message, LV_OBJ_FLAG_HIDDEN)) {
        a->last_state = -1;
    }
    update_stats(a, aos_hal_uptime_ms());
}

static void tap_cb(lv_event_t *event)
{
    app_t *a = lv_event_get_user_data(event);
    if (!a->viewing) {
        return;
    }
    cam_view_t *v = a->v;
    v->mode = v->mode == CAM_FIT ? CAM_FILL : CAM_FIT;
    aos_hal_pref_set_i32(KEY_MODE, v->mode);
    show_bands(a, v->mode == CAM_FIT);
}

/* ---- the list ------------------------------------------------------------------- */

static void open_cb(lv_event_t *event)
{
    open_camera(&s_app, (int)(intptr_t)lv_event_get_user_data(event));
}

static void build_list(app_t *a)
{
    lv_obj_clean(a->list_view);
    a->count = cam_cfg_load(a->cams, CAM_MAX);
    a->gen = cam_cfg_gen();

    lv_obj_t *title = aos_label(a->list_view, _("Cámaras"), aos_font_title, AOS_C_TEXT);
    lv_obj_set_style_pad_bottom(title, 8, 0);

    if (a->count == 0) {
        char msg[200];
        const char *ip = aos_hal_net_ip();
        snprintf(msg, sizeof(msg), _("Agregá cámaras desde\nel portal del reloj:\n\nhttp://%s/camaras"),
                 ip && ip[0] ? ip : "…");
        lv_obj_t *empty = aos_label(a->list_view, msg, aos_font_body, AOS_C_DIM);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }
    for (int i = 0; i < a->count; i++) {
        lv_obj_t *row = lv_obj_create(a->list_view);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, AOS_SCREEN_W - 56, 72);
        lv_obj_set_style_bg_color(row, AOS_C_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = aos_label(row, a->cams[i].name, aos_font_body, AOS_C_TEXT);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, AOS_SCREEN_W - 90);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 16, 10);

        cam_url_t u;
        char sub[140];
        if (cam_url_parse(a->cams[i].url, &u)) {
            snprintf(sub, sizeof(sub), "%s  ·  %s", u.rtsp ? "RTSP" : "MJPEG", u.host);
        } else {
            snprintf(sub, sizeof(sub), "%s", _("dirección inválida"));
        }
        lv_obj_t *sl = aos_label(row, sub, aos_font_small, AOS_C_DIM);
        lv_label_set_long_mode(sl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(sl, AOS_SCREEN_W - 90);
        lv_obj_align(sl, LV_ALIGN_BOTTOM_LEFT, 16, -10);
    }
}

/* ---- life cycle ----------------------------------------------------------------- */

static bool back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = inst;
    if (a->viewing) {
        a->leave = true;                /* on the next timer tick, not in the event */
        return true;
    }
    if (!lv_obj_has_flag(a->view_obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(a->view_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(a->list_view, LV_OBJ_FLAG_HIDDEN);
        return true;
    }
    return false;
}

static void tick(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = inst;
    /* The portal saved: rebuild the list, only while it is the one showing. */
    if (!a->viewing && lv_obj_has_flag(a->view_obj, LV_OBJ_FLAG_HIDDEN) &&
        cam_cfg_gen() != a->gen) {
        build_list(a);
    }
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    app_t *a = &s_app;
    memset(a, 0, sizeof(*a));
    a->v = calloc(1, sizeof(cam_view_t));
    if (!a->v) {
        return NULL;
    }
    a->page = aos_page(root);

    a->list_view = lv_obj_create(a->page);
    lv_obj_remove_style_all(a->list_view);
    lv_obj_set_size(a->list_view, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(a->list_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(a->list_view, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(a->list_view, 8, 0);
    lv_obj_set_style_pad_ver(a->list_view, 40, 0);
    lv_obj_set_scroll_dir(a->list_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(a->list_view, LV_SCROLLBAR_MODE_OFF);

    a->view_obj = lv_obj_create(a->page);
    lv_obj_remove_style_all(a->view_obj);
    lv_obj_set_size(a->view_obj, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(a->view_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(a->view_obj, LV_OPA_COVER, 0);
    lv_obj_add_flag(a->view_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(a->view_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(a->view_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(a->view_obj, tap_cb, LV_EVENT_CLICKED, a);

#ifdef AOS_SIM
    a->sim_px = calloc(1, FRAME_BYTES);
    if (a->sim_px) {
        a->canvas = lv_canvas_create(a->view_obj);
        lv_canvas_set_buffer(a->canvas, a->sim_px, AOS_SCREEN_W, AOS_SCREEN_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_align(a->canvas, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    }
#endif

    a->name_label = aos_label(a->view_obj, "", aos_font_body, AOS_C_TEXT);
    lv_label_set_long_mode(a->name_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(a->name_label, AOS_SCREEN_W - 80);
    lv_obj_set_style_text_align(a->name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(a->name_label, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_remove_flag(a->name_label, LV_OBJ_FLAG_CLICKABLE);

    a->status_label = aos_label(a->view_obj, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_style_text_align(a->status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(a->status_label, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_remove_flag(a->status_label, LV_OBJ_FLAG_CLICKABLE);

    a->message = aos_label(a->view_obj, "", aos_font_body, AOS_C_DIM);
    lv_obj_set_style_text_align(a->message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(a->message, AOS_SCREEN_W - 40);
    lv_obj_center(a->message);
    lv_obj_add_flag(a->message, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(a->message, LV_OBJ_FLAG_CLICKABLE);

    a->shown = false;
    build_list(a);
    a->timer = lv_timer_create(frame_cb, TICK_MS, a);

    /* Development: CAM_OPEN=<n> opens the n-th camera straight away. */
    const char *open = getenv("CAM_OPEN");
    if (open && open[0] >= '0' && open[0] <= '9' && open[0] - '0' < a->count) {
        open_camera(a, open[0] - '0');
    }
    return a;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = inst;
    stop(a);                            /* joins the worker before anything is freed */
    if (a->timer) {
        lv_timer_delete(a->timer);
        a->timer = NULL;
    }
#ifdef AOS_SIM
    if (a->canvas) {
        lv_obj_delete(a->canvas);       /* before its buffer goes */
        a->canvas = NULL;
    }
    free(a->sim_px);
    a->sim_px = NULL;
#endif
    if (a->v) {
        cam_view_release(a->v);
        free(a->v);
        a->v = NULL;
    }
}

static bool camaras_init(aos_app_t *app)
{
    app->desc.id       = "aos.camaras";
    app->desc.name     = "Cámaras";
    app->desc.icon     = LV_SYMBOL_EYE_OPEN;
    app->desc.icon_vec = AOS_ICON_NONE;
    app->desc.color_a  = 0x1E6F8C;
    app->desc.color_b  = 0x0B2E3D;
    app->desc.order    = 125;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN;
    aos_icon_set_ops(app, CAM_ICON, sizeof CAM_ICON);
    app->create  = create;
    app->destroy = destroy;
    app->back    = back;
    app->tick    = tick;
    return true;
}

AOS_APP_ENTRY(camaras_init);
