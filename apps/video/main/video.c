/*
 * AmoledOS - Video: plays MJPEG AVIs from the card, full screen, with sound.
 *
 * The shape of it:
 *
 *   - The file is an AVI of JPEG frames at exactly 368x448 (vd_avi.c reads
 *     it; tools/video_convert.sh writes it). The sound is a WAV beside it
 *     with the same name, and it goes to the firmware's player
 *     (aos_hal_player_play), which already drives the ES8311 from its own
 *     task. So the app never touches audio hardware: it only asks the
 *     player where it is.
 *
 *   - The AUDIO IS THE CLOCK. The player reports its position in whole
 *     seconds; the app keeps a millisecond clock of its own and re-anchors
 *     it every time that second ticks over. A frame is shown when its time
 *     comes, and when the app falls behind, the frames in between are
 *     skipped with a seek, never decoded. A dropped frame is invisible, a
 *     click in the sound is not, so the picture follows the sound and not
 *     the other way.
 *
 *   - Reading and decoding happen in a WORKER (aos_hal_worker_*), the one
 *     background task an app may have, on the other core. Measured first
 *     without it (2026-09-16): read 27 ms + decode 20 ms + LVGL's own
 *     render of a full screen, all in the LVGL task, gave 7.7 fps with the
 *     touch, the back swipe and the portal's capture starved behind it.
 *     The worker fills a ring of three decoded frames; the LVGL side only
 *     points the canvas at the next one and invalidates. Plain flags, one
 *     writer each: the worker owns a slot from FREE to READY, the UI from
 *     READY back to FREE.
 *
 *   - On the board the worker decodes with esp_new_jpeg straight into the
 *     slot's RGB565 buffer. In the simulator the worker only reads: the
 *     JPEG goes through LVGL's TJPGD at draw time, which has to happen in
 *     the LVGL thread. Same source, same screens.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_theme.h"
#include "aos_ui.h"

#include "vd_avi.h"

#ifndef AOS_SIM
#include "esp_jpeg_dec.h"
#endif

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VD_W            AOS_SCREEN_W
#define VD_H            AOS_SCREEN_H
#define VD_MAX_FILES    32
#define VD_NAME_LEN     64
#define VD_SLOTS        4
#define VD_JPEG_CAP     (192 * 1024)        /* one frame, compressed */
#define VD_FRAME_BYTES  (VD_W * VD_H * 2)   /* one frame, RGB565     */
#define VD_TICK_MS      4                   /* how often the timer looks at the clock */
#define VD_STATS_MS     500
#define VD_WORKER_STACK 8192

/* A strip of film: a rounded card with the perforations along both edges. */
static const uint8_t VIDEO_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER, 0, 0, 72, 56, 8, AIC_C_TEXT, 255),
    AIC_INTO,
    AIC_RECT(AIC_TOP_MID, -24,  5, 9, 7, 2, AIC_C_BG, 255),
    AIC_RECT(AIC_TOP_MID,  -8,  5, 9, 7, 2, AIC_C_BG, 255),
    AIC_RECT(AIC_TOP_MID,   8,  5, 9, 7, 2, AIC_C_BG, 255),
    AIC_RECT(AIC_TOP_MID,  24,  5, 9, 7, 2, AIC_C_BG, 255),
    AIC_RECT(AIC_BOTTOM_MID, -24, -5, 9, 7, 2, AIC_C_BG, 255),
    AIC_RECT(AIC_BOTTOM_MID,  -8, -5, 9, 7, 2, AIC_C_BG, 255),
    AIC_RECT(AIC_BOTTOM_MID,   8, -5, 9, 7, 2, AIC_C_BG, 255),
    AIC_RECT(AIC_BOTTOM_MID,  24, -5, 9, 7, 2, AIC_C_BG, 255),
    AIC_RECT(AIC_CENTER, 0, 0, 22, 22, AIC_CIRCLE, AIC_C_RED, 255),
    AIC_OUT,
    AIC_END
};

enum { SLOT_FREE = 0, SLOT_BUSY, SLOT_READY, SLOT_SHOWN };

typedef struct {
    volatile int state;
    uint8_t  *frame_raw;        /* malloc'd, PSRAM */
    uint8_t  *frame;            /* frame_raw rounded up to 16 bytes, for the decoder */
    uint8_t  *jpeg;             /* the compressed frame, PSRAM */
    int       jpeg_len;
    int32_t   index;            /* frame number in the file */
    uint32_t  lap;              /* how many times the file had been rewound */
    uint32_t  read_ms;
    uint32_t  decode_ms;
} vd_slot_t;

typedef struct {
    /* the list */
    char      names[VD_MAX_FILES][VD_NAME_LEN];
    int       count;
    lv_obj_t *list_view;

    /* the player screen */
    lv_obj_t *play_view;
    lv_obj_t *canvas;
    lv_obj_t *stats;
    lv_obj_t *bar;
    lv_obj_t *message;
    lv_timer_t *timer;
#ifdef AOS_SIM
    uint8_t  *sim_frame;        /* the canvas draws into this one on the desktop */
#endif

    vd_slot_t slots[VD_SLOTS];
    int       cur_slot;         /* the one on screen, -1 before the first */

    /* playback (UI side) */
    bool      playing;
    bool      paused;
    bool      has_audio;
    bool      audio_started;
    char      wav_path[256];
    uint64_t  t_anchor;         /* uptime at which the video's t = 0 */
    uint64_t  t_paused;
    uint32_t  last_pos_s;
    int32_t   shown;            /* index of the frame on screen, -1 before the first */
    bool      leave;

    /* the worker's side */
    vd_avi_t  avi;
    volatile int32_t skip_to;   /* UI -> worker: next frame to produce, when behind */
    volatile uint32_t skip_lap; /* ...and on which lap it was asked: a skip past the
                                   end must not carry over into the next lap */
    volatile uint32_t lap;      /* worker: laps so far */
    volatile uint32_t skipped;
    volatile uint32_t errors;
    volatile uint32_t wait_ms;      /* worker: time spent waiting for a free slot */
    volatile uint32_t late_ms;      /* UI: how late the frames were shown, summed */
    int       next_fill;
#ifndef AOS_SIM
    jpeg_dec_handle_t dec;
#endif

    /* stats */
    uint64_t  stats_t0;
    uint32_t  stats_frames;
    uint32_t  stats_decode_ms;
    uint32_t  stats_read_ms;
    uint32_t  log_every;
} vd_t;

static vd_t s_vd;

static const char *videos_dir(void)
{
    static char path[160];
    const char *root = aos_hal_path_sd_root();
    snprintf(path, sizeof(path), "%s/videos", root ? root : "");
    return path;
}

static bool has_ext(const char *name, const char *ext)
{
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot, ext) == 0;
}

static void scan(vd_t *v)
{
    v->count = 0;
    DIR *dir = opendir(videos_dir());
    if (!dir) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) && v->count < VD_MAX_FILES) {
        if (entry->d_name[0] == '.' || !has_ext(entry->d_name, ".avi")) {
            continue;
        }
        size_t len = strnlen(entry->d_name, VD_NAME_LEN - 1);
        memcpy(v->names[v->count], entry->d_name, len);
        v->names[v->count][len] = '\0';
        v->count++;
    }
    closedir(dir);
}

/* ---- the worker --------------------------------------------------------- */

#ifndef AOS_SIM
static bool decode_into(vd_t *v, vd_slot_t *slot)
{
    jpeg_dec_io_t io = {
        .inbuf        = slot->jpeg,
        .inbuf_len    = slot->jpeg_len,
        .inbuf_remain = slot->jpeg_len,
        .outbuf       = slot->frame,
    };
    jpeg_dec_header_info_t info = {0};
    if (jpeg_dec_parse_header(v->dec, &io, &info) != JPEG_ERR_OK) {
        return false;
    }
    if (info.width != VD_W || info.height != VD_H) {
        return false;           /* the converter never does this */
    }
    return jpeg_dec_process(v->dec, &io) == JPEG_ERR_OK;
}
#endif

/* Fills free slots in order, forever, until told to stop. Never touches
 * LVGL. At the end of the file it starts over; the UI notices the frame
 * number going backwards and restarts its clock and the sound. */
static void worker(void *arg)
{
    vd_t *v = arg;
    while (!aos_hal_worker_should_stop()) {
        vd_slot_t *slot = &v->slots[v->next_fill];
        if (slot->state != SLOT_FREE) {
            uint64_t w0 = aos_hal_uptime_ms();
            aos_hal_worker_sleep(2);
            v->wait_ms += (uint32_t)(aos_hal_uptime_ms() - w0);
            continue;
        }
        slot->state = SLOT_BUSY;

        /* Behind? Skip up to the frame the UI asked for: a seek per frame,
         * nothing decoded. */
        while (v->skip_lap == v->lap && (int32_t)v->avi.next_index < v->skip_to) {
            if (vd_avi_next(&v->avi, NULL, 0) <= 0) {
                break;
            }
            v->skipped++;
        }

        uint64_t t0 = aos_hal_uptime_ms();
        int n = vd_avi_next(&v->avi, slot->jpeg, VD_JPEG_CAP);
        if (n == 0) {
            vd_avi_rewind(&v->avi);
            v->lap++;
            slot->state = SLOT_FREE;
            continue;
        }
        uint64_t t1 = aos_hal_uptime_ms();
        slot->read_ms  = (uint32_t)(t1 - t0);
        slot->jpeg_len = n;
        slot->index    = (int32_t)v->avi.next_index - 1;
        slot->lap      = v->lap;
        bool ok = n > 0;
#ifndef AOS_SIM
        if (ok) {
            ok = decode_into(v, slot);
        }
#endif
        slot->decode_ms = (uint32_t)(aos_hal_uptime_ms() - t1);
        if (!ok) {
            v->errors++;
            slot->state = SLOT_FREE;
            continue;
        }
        __sync_synchronize();
        slot->state = SLOT_READY;
        v->next_fill = (v->next_fill + 1) % VD_SLOTS;
    }
}

/* ---- the clock ---------------------------------------------------------- */

/* Milliseconds into the video, or -1 while the sound is still opening. */
static int64_t clock_ms(vd_t *v)
{
    uint64_t now = aos_hal_uptime_ms();
    if (v->has_audio) {
        aos_player_status_t st;
        if (aos_hal_player_status(&st)) {
            if (st.state == AOS_PLAYER_PLAYING) {
                if (!v->audio_started) {
                    v->audio_started = true;
                    v->t_anchor   = now;
                    v->last_pos_s = 0;
                } else if (st.position_s != v->last_pos_s) {
                    /* The second just ticked over: that is the one moment
                     * the player's position is exact to the millisecond. */
                    v->last_pos_s = st.position_s;
                    v->t_anchor   = now - (uint64_t)st.position_s * 1000u;
                }
            }
            /* STOPPED after playing: the sound ran out a fraction of a
             * second before the frames do; the frame clock finishes. */
        }
        if (!v->audio_started) {
            return -1;
        }
    }
    return (int64_t)(now - v->t_anchor);
}

/* ---- the UI side of playback -------------------------------------------- */

static void show_message(vd_t *v, const char *text)
{
    lv_label_set_text(v->message, text);
    lv_obj_remove_flag(v->message, LV_OBJ_FLAG_HIDDEN);
}

static void update_stats(vd_t *v, uint64_t now)
{
    uint64_t dt = now - v->stats_t0;
    if (dt < VD_STATS_MS) {
        return;
    }
    uint32_t fps_x10   = (uint32_t)(v->stats_frames * 10000ull / dt);
    uint32_t read_avg  = v->stats_frames ? v->stats_read_ms / v->stats_frames : 0;
    uint32_t dec_avg   = v->stats_frames ? v->stats_decode_ms / v->stats_frames : 0;
    uint32_t wait_avg  = v->stats_frames ? v->wait_ms / v->stats_frames : 0;
    uint32_t late_avg  = v->stats_frames ? v->late_ms / v->stats_frames : 0;
    v->stats_t0        = now;
    v->stats_frames    = 0;
    v->stats_decode_ms = 0;
    v->stats_read_ms   = 0;
    v->wait_ms         = 0;
    v->late_ms         = 0;

    char buf[96];
    snprintf(buf, sizeof(buf), "%u.%u fps  r%u d%u w%u l%u ms  -%u  %u/%u",
             (unsigned)(fps_x10 / 10), (unsigned)(fps_x10 % 10),
             (unsigned)read_avg, (unsigned)dec_avg, (unsigned)wait_avg, (unsigned)late_avg,
             (unsigned)v->skipped,
             (unsigned)(v->shown < 0 ? 0 : v->shown + 1),
             (unsigned)v->avi.total_frames);
    lv_label_set_text(v->stats, buf);
    /* The same line to the log every 2 s: on the board /api/log is where
     * the numbers get read from. */
    if (++v->log_every % 4 == 0) {
        aos_hal_log("video", "%s  errors %u", buf, (unsigned)v->errors);
    }
    if (v->avi.total_frames) {
        uint32_t frame = v->shown < 0 ? 0 : (uint32_t)v->shown;
        lv_obj_set_width(v->bar, (int32_t)((uint64_t)VD_W * frame / v->avi.total_frames));
    }
}

static void present(vd_t *v, int idx)
{
    vd_slot_t *slot = &v->slots[idx];
#ifndef AOS_SIM
    /* Straight to the panel, past LVGL's render (aos_hal.h says why: 95 ms a
     * frame through the canvas, 16.5 ms this way). The label and the bar
     * are LVGL's, so they are invalidated to be drawn back on top. */
    aos_hal_display_blit(0, 0, VD_W, VD_H, slot->frame);
    lv_obj_invalidate(v->stats);
    lv_obj_invalidate(v->bar);
#else
    /* The desktop draws the JPEG through LVGL, the way the photo viewer
     * does: a RAW image whose data is the compressed frame. */
    lv_image_dsc_t dsc = {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf    = LV_COLOR_FORMAT_RAW,
            .w     = VD_W,
            .h     = VD_H,
        },
        .data_size = (uint32_t)slot->jpeg_len,
        .data      = slot->jpeg,
    };
    lv_layer_t layer;
    lv_canvas_init_layer(v->canvas, &layer);
    lv_draw_image_dsc_t draw;
    lv_draw_image_dsc_init(&draw);
    draw.src = &dsc;
    lv_area_t area = { 0, 0, VD_W - 1, VD_H - 1 };
    lv_draw_image(&layer, &draw, &area);
    lv_canvas_finish_layer(v->canvas, &layer);
    lv_obj_invalidate(v->canvas);
#endif

    /* The one on screen stays SHOWN (the canvas reads it) until the next
     * takes its place; only then does it go back to the worker. */
    if (v->cur_slot >= 0 && v->cur_slot != idx) {
        v->slots[v->cur_slot].state = SLOT_FREE;
    }
    slot->state = SLOT_SHOWN;
    v->cur_slot = idx;
    v->shown    = slot->index;
    v->stats_frames++;
    v->stats_read_ms   += slot->read_ms;
    v->stats_decode_ms += slot->decode_ms;
}

static void restart_clock(vd_t *v)
{
    v->shown         = -1;
    v->audio_started = false;
    v->t_anchor      = aos_hal_uptime_ms();
    v->stats_t0      = v->t_anchor;
    v->stats_frames  = 0;
    v->stats_decode_ms = 0;
    v->stats_read_ms   = 0;
    if (v->has_audio) {
        aos_hal_player_stop();
        v->has_audio = aos_hal_player_play(v->wav_path);
    }
}

static void stop(vd_t *v)
{
    if (!v->playing) {
        return;
    }
    v->playing = false;
    v->paused  = false;
    aos_hal_worker_stop();
    /* LVGL never knew about the frames: make it repaint the whole screen. */
    lv_obj_invalidate(lv_screen_active());
    if (v->has_audio) {
        aos_hal_player_stop();
    }
#ifndef AOS_SIM
    if (v->dec) {
        jpeg_dec_close(v->dec);
        v->dec = NULL;
    }
#endif
    vd_avi_close(&v->avi);
    lv_obj_add_flag(v->play_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(v->list_view, LV_OBJ_FLAG_HIDDEN);
}

static bool alloc_slots(vd_t *v)
{
    for (int i = 0; i < VD_SLOTS; i++) {
        vd_slot_t *s = &v->slots[i];
        if (!s->jpeg) {
            s->jpeg = malloc(VD_JPEG_CAP);
        }
        if (!s->jpeg) {
            return false;
        }
#ifndef AOS_SIM
        if (!s->frame_raw) {
            s->frame_raw = malloc(VD_FRAME_BYTES + 16);
            if (!s->frame_raw) {
                return false;
            }
            s->frame = (uint8_t *)(((uintptr_t)s->frame_raw + 15) & ~(uintptr_t)15);
            memset(s->frame, 0, VD_FRAME_BYTES);
        }
#endif
    }
#ifdef AOS_SIM
    if (!v->sim_frame) {
        v->sim_frame = malloc(VD_FRAME_BYTES);
        if (!v->sim_frame) {
            return false;
        }
        memset(v->sim_frame, 0, VD_FRAME_BYTES);
        lv_canvas_set_buffer(v->canvas, v->sim_frame, VD_W, VD_H, LV_COLOR_FORMAT_RGB565);
    }
#endif
    return true;
}

static void play(vd_t *v, int index)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", videos_dir(), v->names[index]);

    lv_obj_add_flag(v->list_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(v->play_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(v->message, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(v->stats, "");
    lv_obj_set_width(v->bar, 0);

    if (!alloc_slots(v)) {
        show_message(v, _("Sin memoria para el cuadro"));
        return;
    }
    if (!vd_avi_open(&v->avi, path)) {
        char msg[200];
        snprintf(msg, sizeof(msg), _("%s\nno es un AVI de MJPEG"), v->names[index]);
        show_message(v, msg);
        return;
    }
    if (v->avi.width != VD_W || v->avi.height != VD_H) {
        char msg[240];
        snprintf(msg, sizeof(msg), _("%s\nes de %dx%d y no de %dx%d\n(tools/video_convert.sh)"),
                 v->names[index], v->avi.width, v->avi.height, VD_W, VD_H);
        vd_avi_close(&v->avi);
        show_message(v, msg);
        return;
    }
#ifndef AOS_SIM
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;      /* the panel's order, for the blit */
    if (jpeg_dec_open(&cfg, &v->dec) != JPEG_ERR_OK) {
        v->dec = NULL;
        vd_avi_close(&v->avi);
        show_message(v, _("El decodificador no abrió"));
        return;
    }
#endif

    /* The sound: same name, .wav, if it is there. */
    snprintf(v->wav_path, sizeof(v->wav_path), "%s/%s", videos_dir(), v->names[index]);
    char *dot = strrchr(v->wav_path, '.');
    if (dot) {
        strcpy(dot, ".wav");
    }
    FILE *probe = fopen(v->wav_path, "rb");
    v->has_audio = probe != NULL;
    if (probe) {
        fclose(probe);
    }

    for (int i = 0; i < VD_SLOTS; i++) {
        v->slots[i].state = SLOT_FREE;
    }
    v->cur_slot  = -1;
    v->next_fill = 0;
    v->skip_to   = 0;
    v->skip_lap  = 0;
    v->lap       = 0;
    v->skipped   = 0;
    v->errors    = 0;
    v->log_every = 0;
    v->playing   = true;
    v->paused    = false;
    restart_clock(v);

    if (!aos_hal_worker_start("aos_video", worker, v, VD_WORKER_STACK)) {
        v->playing = false;
        if (v->has_audio) {
            aos_hal_player_stop();
        }
#ifndef AOS_SIM
        jpeg_dec_close(v->dec);
        v->dec = NULL;
#endif
        vd_avi_close(&v->avi);
        show_message(v, _("No hay lugar para la tarea de video"));
    }
}

static void frame_cb(lv_timer_t *timer)
{
    vd_t *v = lv_timer_get_user_data(timer);
    if (v->leave) {
        v->leave = false;
        stop(v);
        return;
    }
    if (!v->playing || v->paused) {
        return;
    }

    /* The oldest ready frame, if any. */
    int idx = -1;
    for (int i = 0; i < VD_SLOTS; i++) {
        if (v->slots[i].state == SLOT_READY &&
            (idx < 0 || v->slots[i].index < v->slots[idx].index)) {
            idx = i;
        }
    }
    if (idx < 0) {
        return;
    }
    __sync_synchronize();
    vd_slot_t *slot = &v->slots[idx];

    if (slot->index < v->shown) {
        /* The worker went back to the start: so does everything else. */
        restart_clock(v);
    }
    int64_t t = clock_ms(v);
    if (t < 0) {
        return;                         /* the codec is still opening */
    }
    int32_t due = (int32_t)((uint64_t)t * 1000u / v->avi.us_per_frame);
    if (slot->index > due) {
        return;                         /* early: its time has not come */
    }
    present(v, idx);
    v->late_ms += (uint32_t)(t - (int64_t)slot->index * v->avi.us_per_frame / 1000);
    if (due > slot->index + 1) {
        /* Behind: whatever the worker reads next, let it be the frame that
         * is due by the time it is decoded, not the one after this. */
        v->skip_lap = slot->lap;
        v->skip_to  = due + 1;
    }
    update_stats(v, aos_hal_uptime_ms());
}

/* ---- events ------------------------------------------------------------- */

static void tap_cb(lv_event_t *event)
{
    vd_t *v = lv_event_get_user_data(event);
    if (!v->playing) {
        return;
    }
    v->paused = !v->paused;
    if (v->paused) {
        v->t_paused = aos_hal_uptime_ms();
        if (v->has_audio) {
            aos_hal_player_pause();
        }
    } else {
        v->t_anchor += aos_hal_uptime_ms() - v->t_paused;
        if (v->has_audio) {
            aos_hal_player_resume();
        }
    }
}

static void open_cb(lv_event_t *event)
{
    play(&s_vd, (int)(intptr_t)lv_event_get_user_data(event));
}

static bool back(aos_app_t *self, void *inst)
{
    (void)self;
    vd_t *v = inst;
    if (v->playing) {
        v->leave = true;            /* on the next timer tick, not in the event */
        return true;
    }
    if (!lv_obj_has_flag(v->play_view, LV_OBJ_FLAG_HIDDEN)) {
        /* the message screen after a file that would not open */
        lv_obj_add_flag(v->play_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(v->list_view, LV_OBJ_FLAG_HIDDEN);
        return true;
    }
    return false;
}

/* ---- lifecycle ---------------------------------------------------------- */

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    vd_t *v = &s_vd;
    memset(v, 0, sizeof *v);
    lv_obj_t *page = aos_page(root);
    scan(v);

    /* --- list --- */
    v->list_view = lv_obj_create(page);
    lv_obj_remove_style_all(v->list_view);
    lv_obj_set_size(v->list_view, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(v->list_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v->list_view, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(v->list_view, 8, 0);
    lv_obj_set_style_pad_ver(v->list_view, 40, 0);
    lv_obj_set_scroll_dir(v->list_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(v->list_view, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title = aos_label(v->list_view, _("Videos"), aos_font_title, AOS_C_TEXT);
    lv_obj_set_style_pad_bottom(title, 8, 0);

    if (v->count == 0) {
        char msg[300];
        snprintf(msg, sizeof(msg), _("No hay videos en\n%s\n\nConvertilos con\ntools/video_convert.sh"),
                 videos_dir());
        lv_obj_t *empty = aos_label(v->list_view, msg, aos_font_body, AOS_C_DIM);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    }
    for (int i = 0; i < v->count; i++) {
        lv_obj_t *row = lv_obj_create(v->list_view);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, AOS_SCREEN_W - 56, 56);
        lv_obj_set_style_bg_color(row, AOS_C_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        char shown[VD_NAME_LEN];
        snprintf(shown, sizeof(shown), "%s", v->names[i]);
        char *dot = strrchr(shown, '.');
        if (dot) {
            *dot = '\0';
        }
        lv_obj_t *label = aos_label(row, shown, aos_font_body, AOS_C_TEXT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, AOS_SCREEN_W - 100);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 16, 0);
    }

    /* --- player --- */
    v->play_view = lv_obj_create(page);
    lv_obj_remove_style_all(v->play_view);
    lv_obj_set_size(v->play_view, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(v->play_view, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(v->play_view, LV_OPA_COVER, 0);
    lv_obj_add_flag(v->play_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(v->play_view, LV_OBJ_FLAG_SCROLLABLE);

#ifdef AOS_SIM
    v->canvas = lv_canvas_create(v->play_view);
    lv_obj_set_size(v->canvas, VD_W, VD_H);
    lv_obj_align(v->canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_remove_flag(v->canvas, LV_OBJ_FLAG_CLICKABLE);
#endif
    lv_obj_add_flag(v->play_view, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(v->play_view, tap_cb, LV_EVENT_CLICKED, v);

    v->stats = aos_label(v->play_view, "", aos_font_small, AOS_C_TEXT);
    lv_obj_set_style_bg_color(v->stats, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(v->stats, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(v->stats, 6, 0);
    lv_obj_set_style_pad_ver(v->stats, 2, 0);
    lv_obj_set_style_radius(v->stats, 6, 0);
    lv_obj_align(v->stats, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_remove_flag(v->stats, LV_OBJ_FLAG_CLICKABLE);

    v->bar = lv_obj_create(v->play_view);
    lv_obj_remove_style_all(v->bar);
    lv_obj_set_size(v->bar, 0, 4);
    lv_obj_set_style_bg_color(v->bar, AOS_C_ACCENT, 0);
    lv_obj_set_style_bg_opa(v->bar, LV_OPA_COVER, 0);
    lv_obj_align(v->bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_remove_flag(v->bar, LV_OBJ_FLAG_CLICKABLE);

    v->message = aos_label(v->play_view, "", aos_font_body, AOS_C_DIM);
    lv_obj_set_style_text_align(v->message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(v->message, AOS_SCREEN_W - 40);
    lv_obj_center(v->message);
    lv_obj_add_flag(v->message, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(v->message, LV_OBJ_FLAG_CLICKABLE);

    v->cur_slot = -1;
    v->timer = lv_timer_create(frame_cb, VD_TICK_MS, v);
    return v;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self;
    vd_t *v = inst;
    stop(v);                        /* joins the worker before anything is freed */
    if (v->timer) {
        lv_timer_delete(v->timer);
        v->timer = NULL;
    }
    /* The canvas points at one of our buffers: it goes before they do. */
    if (v->canvas) {
        lv_obj_delete(v->canvas);
        v->canvas = NULL;
    }
    for (int i = 0; i < VD_SLOTS; i++) {
        free(v->slots[i].frame_raw);
        free(v->slots[i].jpeg);
        v->slots[i].frame_raw = NULL;
        v->slots[i].jpeg      = NULL;
    }
#ifdef AOS_SIM
    free(v->sim_frame);
    v->sim_frame = NULL;
#endif
}

static bool video_init(aos_app_t *app)
{
    app->desc.id       = "aos.video";
    app->desc.name     = "Video";
    app->desc.icon     = LV_SYMBOL_VIDEO;
    app->desc.icon_vec = AOS_ICON_NONE;
    app->desc.color_a  = 0xFF453A;
    app->desc.color_b  = 0x8E1A12;
    app->desc.order    = 126;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN;
    aos_icon_set_ops(app, VIDEO_ICON, sizeof VIDEO_ICON);
    app->create  = create;
    app->destroy = destroy;
    app->back    = back;
    return true;
}

AOS_APP_ENTRY(video_init);
