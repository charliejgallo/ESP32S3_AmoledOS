/*
 * Recorder - AmoledOS
 *
 * Records from the microphone to WAV on the microSD, shows the time and the
 * waveform live, and lets you listen to and delete what was recorded.
 *
 * The capture does not live here: the HAL does it in its own task
 * (aos_hal_rec_*) and it goes on running even if you leave the app. This app
 * is the face: it asks to start, draws what the HAL tells it and manages the
 * files.
 *
 * Three views:
 *   main      record + the last three recordings
 *   list      every recording
 *   detail    one recording: whole waveform, play, delete
 *
 * It builds two ways from the same source:
 *   .so for the board      cd apps/recorder && idf.py -G 'Unix Makefiles' set-target esp32s3 && idf.py so
 *   simulator app          the simulator builds it (AOS_SIM_BUILTIN)
 */
#include "aos_app.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"
#include "aos_theme.h"

#include "rec_files.h"
#include "rec_wave.h"

#include <stdio.h>
#include <string.h>

#define WAVE_BARS       40
#define RECENT_ROWS     3
#define ROW_W           (AOS_SCREEN_W - 48)
#define ROW_H           32
#define DELETE_ARM_MS   3000    /* how long the "are you sure?" waits before giving up */

typedef enum {
    VIEW_MAIN = 0,
    VIEW_LIST,
    VIEW_DETAIL,
} view_t;

typedef struct {
    lv_obj_t *row;
    lv_obj_t *mark;
    lv_obj_t *name;
    lv_obj_t *meta;
} row_t;

typedef struct {
    view_t    view;
    view_t    came_from;        /* the detail view is reached from two places */

    lv_obj_t *main_view;
    lv_obj_t *pill;
    lv_obj_t *pill_dot;
    lv_obj_t *pill_text;
    lv_obj_t *time_label;
    lv_obj_t *status_label;
    rec_wave_t *live_wave;
    lv_obj_t *rec_btn;
    lv_obj_t *stop_btn;
    lv_obj_t *pause_btn;
    lv_obj_t *pause_icon;
    lv_obj_t *all_btn;
    lv_obj_t *empty_hint;
    row_t     recent[RECENT_ROWS];

    lv_obj_t *list_view;
    lv_obj_t *list_body;
    lv_obj_t *list_subtitle;

    lv_obj_t *detail_view;
    lv_obj_t *detail_title;
    lv_obj_t *detail_meta;
    lv_obj_t *detail_play_icon;
    lv_obj_t *detail_position;
    lv_obj_t *detail_del_text;
    rec_wave_t *detail_wave;
    int       detail_index;
    bool      delete_armed;
    uint32_t  delete_armed_ms;
    bool      playing;

    rec_file_t files[REC_MAX_FILES];
    int        file_count;

    lv_timer_t *timer;
    uint32_t   last_shown_s;
    uint32_t   blink_ms;
    bool       blink_on;
    bool       button_toggle;   /* the physical button asks to change state */
    bool       ui_recording;    /* what the screen is showing */
} rec_ctx_t;

static rec_ctx_t *s_ctx;

/* -------------------------------------------------------------------------- */

static void format_time(char *out, size_t len, uint32_t seconds)
{
    if (seconds >= 3600) {
        snprintf(out, len, "%u:%02u:%02u", (unsigned)(seconds / 3600),
                 (unsigned)(seconds / 60 % 60), (unsigned)(seconds % 60));
    } else {
        snprintf(out, len, "%02u:%02u", (unsigned)(seconds / 60),
                 (unsigned)(seconds % 60));
    }
}

static bool is_recording(void)
{
    aos_rec_status_t status;
    return aos_hal_rec_status(&status) && status.state != AOS_REC_IDLE;
}

/* The copy the runtime keeps in its table, not a duplicate: that is why
 * desc.flags can be touched live. The same trick remoto.c uses. */
static aos_app_t *s_self;

/* AOS_APP_FLAG_BACKGROUND only while really recording.
 *
 * The runtime does NOT destroy a background app on exit, and without destroy
 * its .so is not unloaded either: the code stays in executable memory until a
 * restart. Measured on the board: the Recorder held 38 KB, and after passing
 * through it the large apps no longer fitted. By carrying the flag only while
 * a recording is under way, leaving it idle lets the runtime destroy it and
 * the .so is unloaded like any other app's. */
static void aplicar_background(void)
{
    if (!s_self) {
        return;
    }
    if (is_recording()) {
        s_self->desc.flags |= AOS_APP_FLAG_BACKGROUND;
    } else {
        s_self->desc.flags &= ~(uint32_t)AOS_APP_FLAG_BACKGROUND;
    }
}

/* -------------------------------------------------------------------------- */
/* List rows                                                                   */
/* -------------------------------------------------------------------------- */

static void open_detail(int index);

static void row_click_cb(lv_event_t *event)
{
    open_detail((int)(intptr_t)lv_event_get_user_data(event));
}

static void row_build(row_t *row, lv_obj_t *parent, int index, bool flow)
{
    row->row = lv_obj_create(parent);
    lv_obj_remove_style_all(row->row);
    lv_obj_set_size(row->row, ROW_W, ROW_H);
    lv_obj_set_style_bg_color(row->row, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(row->row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row->row, 10, 0);
    lv_obj_add_flag(row->row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row->row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row->row, row_click_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)index);
    if (!flow) {
        lv_obj_set_pos(row->row, 0, 0);
    }

    row->mark = lv_obj_create(row->row);
    lv_obj_remove_style_all(row->mark);
    lv_obj_set_size(row->mark, 4, ROW_H - 14);
    lv_obj_set_style_bg_color(row->mark, AOS_C_GREEN, 0);
    lv_obj_set_style_bg_opa(row->mark, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row->mark, 2, 0);
    lv_obj_align(row->mark, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_remove_flag(row->mark, LV_OBJ_FLAG_CLICKABLE);

    row->name = aos_label(row->row, "", aos_font_small, AOS_C_TEXT);
    lv_label_set_long_mode(row->name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(row->name, ROW_W - 110);
    lv_obj_align(row->name, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_remove_flag(row->name, LV_OBJ_FLAG_CLICKABLE);

    row->meta = aos_label(row->row, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(row->meta, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_remove_flag(row->meta, LV_OBJ_FLAG_CLICKABLE);
}

static void row_fill(row_t *row, const rec_file_t *file, bool newest)
{
    char pretty[REC_NAME_LEN];
    rec_files_pretty(pretty, sizeof(pretty), file->name);
    lv_label_set_text(row->name, pretty);

    char duration[16];
    format_time(duration, sizeof(duration), file->seconds);
    lv_label_set_text(row->meta, duration);

    lv_obj_set_style_bg_opa(row->mark, newest ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(row->row, newest ? AOS_C_CARD2 : AOS_C_CARD, 0);
}

/* -------------------------------------------------------------------------- */
/* Data                                                                        */
/* -------------------------------------------------------------------------- */

static void rescan(void)
{
    s_ctx->file_count = rec_files_scan(s_ctx->files, REC_MAX_FILES);
}

static void refresh_recent(void)
{
    for (int i = 0; i < RECENT_ROWS; i++) {
        row_t *row = &s_ctx->recent[i];
        if (i < s_ctx->file_count) {
            lv_obj_remove_flag(row->row, LV_OBJ_FLAG_HIDDEN);
            row_fill(row, &s_ctx->files[i], i == 0);
        } else {
            lv_obj_add_flag(row->row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_ctx->file_count == 0) {
        lv_obj_remove_flag(s_ctx->empty_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ctx->all_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ctx->empty_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_ctx->all_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

/* -------------------------------------------------------------------------- */
/* Navigation                                                                  */
/* -------------------------------------------------------------------------- */

static void stop_playback(void)
{
    if (s_ctx->playing) {
        aos_hal_player_stop();
        s_ctx->playing = false;
    }
}

static void show_view(view_t view)
{
    s_ctx->view = view;
    lv_obj_add_flag(s_ctx->main_view,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ctx->list_view,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ctx->detail_view, LV_OBJ_FLAG_HIDDEN);

    switch (view) {
    case VIEW_MAIN:   lv_obj_remove_flag(s_ctx->main_view,   LV_OBJ_FLAG_HIDDEN); break;
    case VIEW_LIST:   lv_obj_remove_flag(s_ctx->list_view,   LV_OBJ_FLAG_HIDDEN); break;
    case VIEW_DETAIL: lv_obj_remove_flag(s_ctx->detail_view, LV_OBJ_FLAG_HIDDEN); break;
    }
}

static void build_list_rows(void)
{
    lv_obj_clean(s_ctx->list_body);

    char subtitle[48];
    snprintf(subtitle, sizeof(subtitle), "%d %s", s_ctx->file_count,
             s_ctx->file_count == 1 ? _("grabacion") : _("grabaciones"));
    lv_label_set_text(s_ctx->list_subtitle, subtitle);

    for (int i = 0; i < s_ctx->file_count; i++) {
        row_t row;
        row_build(&row, s_ctx->list_body, i, true);
        lv_obj_set_height(row.row, ROW_H + 14);
        lv_obj_align(row.name, LV_ALIGN_LEFT_MID, 20, -8);

        char pretty[REC_NAME_LEN];
        rec_files_pretty(pretty, sizeof(pretty), s_ctx->files[i].name);
        lv_label_set_text(row.name, pretty);

        char duration[16];
        format_time(duration, sizeof(duration), s_ctx->files[i].seconds);
        lv_label_set_text(row.meta, duration);
        lv_obj_set_style_bg_opa(row.mark, i == 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

        char when[32], size[16], detail[64];
        rec_files_when(when, sizeof(when), s_ctx->files[i].mtime);
        rec_files_size(size, sizeof(size), s_ctx->files[i].bytes);
        snprintf(detail, sizeof(detail), "%s  -  %s", when, size);

        lv_obj_t *sub = aos_label(row.row, detail, aos_font_small, AOS_C_DIM);
        lv_obj_set_style_text_opa(sub, LV_OPA_70, 0);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 20, 11);
        lv_obj_remove_flag(sub, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void refresh_detail(void)
{
    const rec_file_t *file = &s_ctx->files[s_ctx->detail_index];

    char pretty[REC_NAME_LEN];
    rec_files_pretty(pretty, sizeof(pretty), file->name);
    lv_label_set_text(s_ctx->detail_title, pretty);

    char when[32], size[16], duration[16], meta[96];
    rec_files_when(when, sizeof(when), file->mtime);
    rec_files_size(size, sizeof(size), file->bytes);
    format_time(duration, sizeof(duration), file->seconds);
    snprintf(meta, sizeof(meta), "%s  -  %s  -  %s  -  %u kHz", when, duration, size,
             (unsigned)(file->sample_rate / 1000));
    lv_label_set_text(s_ctx->detail_meta, meta);

    uint8_t envelope[WAVE_BARS];
    if (rec_files_envelope(file->name, envelope, WAVE_BARS)) {
        rec_wave_set(s_ctx->detail_wave, envelope, WAVE_BARS);
    } else {
        rec_wave_clear(s_ctx->detail_wave);
    }
    rec_wave_set_progress(s_ctx->detail_wave, -1);

    lv_label_set_text(s_ctx->detail_play_icon, LV_SYMBOL_PLAY);
    lv_label_set_text(s_ctx->detail_position, "");
    lv_label_set_text(s_ctx->detail_del_text, _("Borrar"));
    s_ctx->delete_armed = false;
}

static void open_detail(int index)
{
    if (index < 0 || index >= s_ctx->file_count) {
        return;
    }
    stop_playback();
    s_ctx->detail_index = index;
    s_ctx->came_from    = s_ctx->view;
    refresh_detail();
    show_view(VIEW_DETAIL);
}

/* -------------------------------------------------------------------------- */
/* Recording                                                                   */
/* -------------------------------------------------------------------------- */

static void set_recording_ui(bool recording, bool paused)
{
    s_ctx->ui_recording = recording;

    if (recording) {
        lv_obj_add_flag(s_ctx->rec_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_ctx->stop_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_ctx->pause_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_ctx->pill, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_ctx->pause_icon, paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
        lv_label_set_text(s_ctx->status_label, paused ? _("en pausa") : _("grabando"));
        lv_label_set_text(s_ctx->pill_text, paused ? _("PAUSA") : "REC");
        lv_obj_set_style_bg_color(s_ctx->pill, paused ? AOS_C_ORANGE : AOS_C_RED, 0);
        rec_wave_set_color(s_ctx->live_wave, paused ? AOS_C_ORANGE : AOS_C_RED);
    } else {
        lv_obj_remove_flag(s_ctx->rec_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ctx->stop_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ctx->pause_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ctx->pill, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_ctx->status_label, _("tocar para grabar"));
        rec_wave_set_color(s_ctx->live_wave, AOS_C_CARD2);
    }
}

static void start_recording(void)
{
    if (!aos_hal_sd_present()) {
        aos_ui_toast(_("No hay tarjeta"), 1600);
        return;
    }

    char name[REC_NAME_LEN];
    char path[REC_PATH_LEN];
    rec_files_next_name(name, sizeof(name));
    rec_files_path(path, sizeof(path), name);

    if (!aos_hal_rec_start(path, 0)) {
        aos_ui_toast(_("No se pudo grabar"), 1800);
        return;
    }

    rec_wave_clear(s_ctx->live_wave);
    s_ctx->last_shown_s = 0;
    lv_label_set_text(s_ctx->time_label, "00:00");
    set_recording_ui(true, false);
    aplicar_background();

    /* No beep on starting, on purpose: the speaker and the microphone are the
     * same codec, so the note would tear the capture's input channel away (and
     * it would also end up in the file). On stopping, yes, because by then the
     * microphone has been closed. */
}

static void stop_recording(void)
{
    bool saved = aos_hal_rec_stop();
    aos_hal_beep(700, 60);
    set_recording_ui(false, false);
    aplicar_background();

    rescan();
    refresh_recent();

    if (saved && s_ctx->file_count > 0) {
        char pretty[REC_NAME_LEN], message[96];
        rec_files_pretty(pretty, sizeof(pretty), s_ctx->files[0].name);
        snprintf(message, sizeof(message), "Guardado\n%s", pretty);
        aos_ui_toast(message, 1500);
    }
}

static void toggle_recording(void)
{
    if (is_recording()) {
        stop_recording();
    } else {
        start_recording();
    }
}

/* -------------------------------------------------------------------------- */
/* Events                                                                      */
/* -------------------------------------------------------------------------- */

static void rec_cb(lv_event_t *event)
{
    (void)event;
    start_recording();
}

static void stop_cb(lv_event_t *event)
{
    (void)event;
    stop_recording();
}

static void pause_cb(lv_event_t *event)
{
    (void)event;
    aos_rec_status_t status;
    if (!aos_hal_rec_status(&status)) {
        return;
    }
    if (status.state == AOS_REC_PAUSED) {
        aos_hal_rec_resume();
        set_recording_ui(true, false);
    } else if (status.state == AOS_REC_RECORDING) {
        aos_hal_rec_pause();
        set_recording_ui(true, true);
    }
}

static void all_cb(lv_event_t *event)
{
    (void)event;
    rescan();
    build_list_rows();
    show_view(VIEW_LIST);
}

static void play_cb(lv_event_t *event)
{
    (void)event;
    if (s_ctx->playing) {
        stop_playback();
        lv_label_set_text(s_ctx->detail_play_icon, LV_SYMBOL_PLAY);
        rec_wave_set_progress(s_ctx->detail_wave, -1);
        return;
    }

    char path[REC_PATH_LEN];
    rec_files_path(path, sizeof(path), s_ctx->files[s_ctx->detail_index].name);
    if (!aos_hal_player_play(path)) {
        aos_ui_toast(_("No se pudo reproducir"), 1600);
        return;
    }
    s_ctx->playing = true;
    lv_label_set_text(s_ctx->detail_play_icon, LV_SYMBOL_STOP);
}

/* Deleting asks for confirmation on the same button: the first tap arms it and
 * the second deletes. A dialogue covers the whole screen in 368 px of width,
 * and on a screen that small the accidental tap is the rule, not the
 * exception. */
static void delete_cb(lv_event_t *event)
{
    (void)event;

    if (!s_ctx->delete_armed) {
        s_ctx->delete_armed = true;
        s_ctx->delete_armed_ms = (uint32_t)aos_hal_uptime_ms();
        lv_label_set_text(s_ctx->detail_del_text, _("Seguro?"));
        return;
    }

    stop_playback();
    if (!rec_files_delete(s_ctx->files[s_ctx->detail_index].name)) {
        aos_ui_toast(_("No se pudo borrar"), 1600);
        return;
    }
    aos_ui_toast(_("Borrada"), 1200);

    rescan();
    refresh_recent();

    if (s_ctx->came_from == VIEW_LIST && s_ctx->file_count > 0) {
        build_list_rows();
        show_view(VIEW_LIST);
    } else {
        show_view(VIEW_MAIN);
    }
}

/* -------------------------------------------------------------------------- */
/* Refresh                                                                     */
/* -------------------------------------------------------------------------- */

static void tick_cb(lv_timer_t *timer)
{
    /* If the recording ends by itself (disk full, duration cap) nobody would
     * call stop_recording(): the flag is kept up to date from here. */
    aplicar_background();

    (void)timer;

    if (s_ctx->button_toggle) {
        s_ctx->button_toggle = false;
        toggle_recording();
    }

    aos_rec_status_t status;
    bool active = aos_hal_rec_status(&status) && status.state != AOS_REC_IDLE;

    /* The capture can end by itself (the microphone is cut off, the card fills
     * up). If the screen was still showing "recording", say so instead of
     * leaving the stopwatch stuck. */
    if (!active && s_ctx->ui_recording) {
        s_ctx->ui_recording = false;
        set_recording_ui(false, false);
        rescan();
        refresh_recent();
        aos_ui_toast(_("Se corto la grabacion"), 2000);
    }

    if (active) {
        /* The envelope is drained whole even if there is more than one sample:
         * if the frame fell behind, the waveform advances faster but nothing
         * is skipped. */
        uint8_t peaks[24];
        int got = aos_hal_rec_peaks(peaks, (int)sizeof(peaks));
        if (got > 0) {
            rec_wave_push_many(s_ctx->live_wave, peaks, got);
        }

        uint32_t seconds = status.elapsed_ms / 1000;
        if (seconds != s_ctx->last_shown_s) {
            s_ctx->last_shown_s = seconds;
            char buf[16];
            format_time(buf, sizeof(buf), seconds);
            lv_label_set_text(s_ctx->time_label, buf);
        }

        /* The REC dot pulses once a second, like the tally light on a real
         * recorder. */
        uint32_t now = (uint32_t)aos_hal_uptime_ms();
        if (now - s_ctx->blink_ms >= 500) {
            s_ctx->blink_ms = now;
            s_ctx->blink_on = !s_ctx->blink_on;
            lv_obj_set_style_bg_opa(s_ctx->pill_dot,
                                    status.state == AOS_REC_PAUSED ? LV_OPA_COVER
                                    : (s_ctx->blink_on ? LV_OPA_COVER : LV_OPA_20), 0);
        }
    }

    if (s_ctx->view == VIEW_DETAIL) {
        if (s_ctx->delete_armed &&
            (uint32_t)aos_hal_uptime_ms() - s_ctx->delete_armed_ms > DELETE_ARM_MS) {
            s_ctx->delete_armed = false;
            lv_label_set_text(s_ctx->detail_del_text, _("Borrar"));
        }

        if (s_ctx->playing) {
            aos_player_status_t player;
            uint32_t total = s_ctx->files[s_ctx->detail_index].seconds;
            if (aos_hal_player_status(&player)) {
                if (player.state == AOS_PLAYER_STOPPED || (total && player.position_s >= total)) {
                    stop_playback();
                    lv_label_set_text(s_ctx->detail_play_icon, LV_SYMBOL_PLAY);
                    lv_label_set_text(s_ctx->detail_position, "");
                    rec_wave_set_progress(s_ctx->detail_wave, -1);
                } else if (total) {
                    int bars = (int)(player.position_s * WAVE_BARS / total);
                    rec_wave_set_progress(s_ctx->detail_wave, bars);

                    char position[40], elapsed[16], duration[16];
                    format_time(elapsed, sizeof(elapsed), player.position_s);
                    format_time(duration, sizeof(duration), total);
                    snprintf(position, sizeof(position), "%s / %s", elapsed, duration);
                    lv_label_set_text(s_ctx->detail_position, position);
                }
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Building the views                                                          */
/* -------------------------------------------------------------------------- */

static lv_obj_t *round_button(lv_obj_t *parent, int32_t size, lv_color_t color,
                              lv_event_cb_t cb)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, size, size);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    return button;
}

static void build_main(lv_obj_t *parent)
{
    s_ctx->main_view = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ctx->main_view);
    lv_obj_set_size(s_ctx->main_view, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_ctx->main_view, LV_OBJ_FLAG_SCROLLABLE);

    /* REC pill */
    s_ctx->pill = lv_obj_create(s_ctx->main_view);
    lv_obj_remove_style_all(s_ctx->pill);
    lv_obj_set_size(s_ctx->pill, 104, 30);
    lv_obj_set_style_radius(s_ctx->pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ctx->pill, AOS_C_RED, 0);
    lv_obj_set_style_bg_opa(s_ctx->pill, LV_OPA_COVER, 0);
    lv_obj_align(s_ctx->pill, LV_ALIGN_TOP_MID, 0, 4);

    s_ctx->pill_dot = lv_obj_create(s_ctx->pill);
    lv_obj_remove_style_all(s_ctx->pill_dot);
    lv_obj_set_size(s_ctx->pill_dot, 12, 12);
    lv_obj_set_style_radius(s_ctx->pill_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_ctx->pill_dot, AOS_C_TEXT, 0);
    lv_obj_set_style_bg_opa(s_ctx->pill_dot, LV_OPA_COVER, 0);
    lv_obj_align(s_ctx->pill_dot, LV_ALIGN_LEFT_MID, 14, 0);

    s_ctx->pill_text = aos_label(s_ctx->pill, "REC", aos_font_small, AOS_C_TEXT);
    lv_obj_align(s_ctx->pill_text, LV_ALIGN_LEFT_MID, 34, 0);
    aos_make_decorative(s_ctx->pill);

    /* time */
    s_ctx->time_label = aos_label_boxed(s_ctx->main_view, "00:00", aos_font_huge,
                                        AOS_C_TEXT, AOS_SCREEN_W, 56);
    lv_obj_align(s_ctx->time_label, LV_ALIGN_TOP_MID, 0, 38);

    s_ctx->status_label = aos_label_boxed(s_ctx->main_view, _("tocar para grabar"),
                                          aos_font_small, AOS_C_DIM,
                                          AOS_SCREEN_W, 20);
    lv_obj_align(s_ctx->status_label, LV_ALIGN_TOP_MID, 0, 96);

    /* live waveform */
    s_ctx->live_wave = rec_wave_create(s_ctx->main_view, 320, 66, WAVE_BARS,
                                       AOS_C_CARD2);
    lv_obj_align(rec_wave_obj(s_ctx->live_wave), LV_ALIGN_TOP_MID, 0, 118);

    /* buttons */
    s_ctx->rec_btn = round_button(s_ctx->main_view, 76, AOS_C_RED, rec_cb);
    lv_obj_align(s_ctx->rec_btn, LV_ALIGN_TOP_MID, 0, 192);
    lv_obj_set_style_border_width(s_ctx->rec_btn, 3, 0);
    lv_obj_set_style_border_color(s_ctx->rec_btn, lv_color_hex(0x7A1512), 0);

    lv_obj_t *rec_glyph = aos_label(s_ctx->rec_btn, "REC", aos_font_small, AOS_C_TEXT);
    lv_obj_center(rec_glyph);
    lv_obj_remove_flag(rec_glyph, LV_OBJ_FLAG_CLICKABLE);

    s_ctx->stop_btn = round_button(s_ctx->main_view, 76, AOS_C_RED, stop_cb);
    lv_obj_align(s_ctx->stop_btn, LV_ALIGN_TOP_MID, 0, 192);

    lv_obj_t *square = lv_obj_create(s_ctx->stop_btn);
    lv_obj_remove_style_all(square);
    lv_obj_set_size(square, 26, 26);
    lv_obj_set_style_radius(square, 5, 0);
    lv_obj_set_style_bg_color(square, AOS_C_TEXT, 0);
    lv_obj_set_style_bg_opa(square, LV_OPA_COVER, 0);
    lv_obj_center(square);
    lv_obj_remove_flag(square, LV_OBJ_FLAG_CLICKABLE);

    s_ctx->pause_btn = round_button(s_ctx->main_view, 56, AOS_C_CARD2, pause_cb);
    lv_obj_align(s_ctx->pause_btn, LV_ALIGN_TOP_MID, -88, 202);
    s_ctx->pause_icon = aos_label(s_ctx->pause_btn, LV_SYMBOL_PAUSE,
                                  aos_font_body, AOS_C_TEXT);
    lv_obj_center(s_ctx->pause_icon);
    lv_obj_remove_flag(s_ctx->pause_icon, LV_OBJ_FLAG_CLICKABLE);

    /* latest recordings */
    lv_obj_t *header = aos_label(s_ctx->main_view, _("GRABACIONES"), aos_font_small,
                                 AOS_C_DIM);
    lv_obj_set_style_text_opa(header, LV_OPA_60, 0);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 24, 280);

    s_ctx->all_btn = lv_obj_create(s_ctx->main_view);
    lv_obj_remove_style_all(s_ctx->all_btn);
    lv_obj_set_size(s_ctx->all_btn, 92, 26);
    lv_obj_align(s_ctx->all_btn, LV_ALIGN_TOP_RIGHT, -20, 276);
    lv_obj_add_flag(s_ctx->all_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ctx->all_btn, all_cb, LV_EVENT_CLICKED, NULL);

    /* The icon is not glued to the literal: glued, the key aos_tr() looks for
     * carries its bytes at the end and the catalogue does not have it. */
    char all_txt[32];
    snprintf(all_txt, sizeof(all_txt), "%s " LV_SYMBOL_RIGHT, _("todas"));
    lv_obj_t *all_text = aos_label(s_ctx->all_btn, all_txt,
                                   aos_font_small, AOS_C_ACCENT);
    lv_obj_align(all_text, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_remove_flag(all_text, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < RECENT_ROWS; i++) {
        row_build(&s_ctx->recent[i], s_ctx->main_view, i, false);
        lv_obj_align(s_ctx->recent[i].row, LV_ALIGN_TOP_MID, 0,
                     304 + i * (ROW_H + 4));
    }

    s_ctx->empty_hint = aos_label_boxed(s_ctx->main_view,
                                        _("todavia no grabaste nada"),
                                        aos_font_small, AOS_C_DIM,
                                        AOS_SCREEN_W, 22);
    lv_obj_align(s_ctx->empty_hint, LV_ALIGN_TOP_MID, 0, 330);
}

static void build_list(lv_obj_t *parent)
{
    s_ctx->list_view = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ctx->list_view);
    lv_obj_set_size(s_ctx->list_view, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_ctx->list_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ctx->list_view, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = aos_label(s_ctx->list_view, _("Grabaciones"), aos_font_title,
                                AOS_C_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    s_ctx->list_subtitle = aos_label_boxed(s_ctx->list_view, "", aos_font_small,
                                           AOS_C_DIM, AOS_SCREEN_W, 20);
    lv_obj_align(s_ctx->list_subtitle, LV_ALIGN_TOP_MID, 0, 40);

    s_ctx->list_body = lv_obj_create(s_ctx->list_view);
    lv_obj_remove_style_all(s_ctx->list_body);
    lv_obj_set_size(s_ctx->list_body, AOS_SCREEN_W, AOS_SCREEN_H - 130);
    lv_obj_align(s_ctx->list_body, LV_ALIGN_TOP_MID, 0, 68);
    lv_obj_set_flex_flow(s_ctx->list_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ctx->list_body, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_ctx->list_body, 6, 0);
    lv_obj_set_scroll_dir(s_ctx->list_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_ctx->list_body, LV_SCROLLBAR_MODE_OFF);
}

static void build_detail(lv_obj_t *parent)
{
    s_ctx->detail_view = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ctx->detail_view);
    lv_obj_set_size(s_ctx->detail_view, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_ctx->detail_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ctx->detail_view, LV_OBJ_FLAG_HIDDEN);

    s_ctx->detail_title = aos_label_boxed(s_ctx->detail_view, "", aos_font_title,
                                          AOS_C_TEXT, AOS_SCREEN_W - 24, 34);
    lv_label_set_long_mode(s_ctx->detail_title, LV_LABEL_LONG_DOT);
    lv_obj_align(s_ctx->detail_title, LV_ALIGN_TOP_MID, 0, 20);

    s_ctx->detail_meta = aos_label_boxed(s_ctx->detail_view, "", aos_font_small,
                                         AOS_C_DIM, AOS_SCREEN_W - 16, 20);
    lv_obj_align(s_ctx->detail_meta, LV_ALIGN_TOP_MID, 0, 60);

    s_ctx->detail_wave = rec_wave_create(s_ctx->detail_view, 320, 110, WAVE_BARS,
                                         AOS_C_TEAL);
    lv_obj_align(rec_wave_obj(s_ctx->detail_wave), LV_ALIGN_TOP_MID, 0, 104);

    s_ctx->detail_position = aos_label_boxed(s_ctx->detail_view, "", aos_font_small,
                                             AOS_C_DIM, AOS_SCREEN_W, 20);
    lv_obj_align(s_ctx->detail_position, LV_ALIGN_TOP_MID, 0, 220);

    lv_obj_t *play = round_button(s_ctx->detail_view, 84, AOS_C_TEAL, play_cb);
    lv_obj_align(play, LV_ALIGN_TOP_MID, 0, 248);
    s_ctx->detail_play_icon = aos_label(play, LV_SYMBOL_PLAY, aos_font_title,
                                        AOS_C_BG);
    lv_obj_center(s_ctx->detail_play_icon);
    lv_obj_remove_flag(s_ctx->detail_play_icon, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *trash = lv_obj_create(s_ctx->detail_view);
    lv_obj_remove_style_all(trash);
    lv_obj_set_size(trash, 150, 44);
    lv_obj_set_style_radius(trash, 22, 0);
    lv_obj_set_style_bg_color(trash, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(trash, LV_OPA_COVER, 0);
    lv_obj_align(trash, LV_ALIGN_TOP_MID, 0, 348);
    lv_obj_add_flag(trash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(trash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(trash, delete_cb, LV_EVENT_CLICKED, NULL);

    s_ctx->detail_del_text = aos_label(trash, _("Borrar"), aos_font_body, AOS_C_RED);
    lv_obj_center(s_ctx->detail_del_text);
    lv_obj_remove_flag(s_ctx->detail_del_text, LV_OBJ_FLAG_CLICKABLE);
}

/* -------------------------------------------------------------------------- */
/* Life cycle                                                                  */
/* -------------------------------------------------------------------------- */

static void *rec_create(aos_app_t *self, lv_obj_t *root)
{
    s_self = self;
    aplicar_background();

    (void)self;

    rec_ctx_t *ctx = lv_malloc_zeroed(sizeof(rec_ctx_t));
    if (!ctx) {
        return NULL;
    }
    s_ctx = ctx;

    lv_obj_t *page = aos_page(root);

    build_main(page);
    build_list(page);
    build_detail(page);

    rescan();
    refresh_recent();

    /* If it is reopened with a recording under way (the HAL goes on recording
     * even when the app is not there), the screen starts by showing it. */
    aos_rec_status_t status = { 0 };
    bool recording = aos_hal_rec_status(&status) && status.state != AOS_REC_IDLE;
    set_recording_ui(recording, status.state == AOS_REC_PAUSED);
    if (recording) {
        char buf[16];
        format_time(buf, sizeof(buf), status.elapsed_ms / 1000);
        lv_label_set_text(ctx->time_label, buf);
    }

    show_view(VIEW_MAIN);
    ctx->timer = lv_timer_create(tick_cb, 50, NULL);
    return ctx;
}

static void rec_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    rec_ctx_t *ctx = (rec_ctx_t *)inst;

    if (ctx->timer) {
        lv_timer_delete(ctx->timer);
    }
    stop_playback();
    rec_wave_delete(ctx->live_wave);
    rec_wave_delete(ctx->detail_wave);

    /* The recording carries on: it lives in the HAL. On reopening the app, the
     * screen picks it up where it was. */
    lv_free(ctx);
    s_ctx = NULL;
}

static bool rec_back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;

    switch (s_ctx->view) {
    case VIEW_DETAIL:
        stop_playback();
        if (s_ctx->came_from == VIEW_LIST) {
            build_list_rows();
            show_view(VIEW_LIST);
        } else {
            show_view(VIEW_MAIN);
        }
        return true;
    case VIEW_LIST:
        show_view(VIEW_MAIN);
        return true;
    default:
        return false;
    }
}

/* The side button starts and stops the recording without looking at the
 * screen, which is what you want a physical button on a recorder for. It is
 * noted and acted on next frame: the callback arrives from the HAL's task. */
static bool rec_button(aos_app_t *self, void *inst, int action)
{
    (void)self; (void)inst;

    if (action != AOS_BUTTON_CLICK || s_ctx->view != VIEW_MAIN) {
        return false;
    }
    s_ctx->button_toggle = true;
    return true;
}

static bool rec_init(aos_app_t *app)
{
    app->desc.id       = "app.recorder";
    app->desc.name     = "Grabadora";
    app->desc.icon     = LV_SYMBOL_AUDIO;
    app->desc.icon_vec = AOS_ICON_MIC;
    app->desc.color_a  = 0xFF453A;
    app->desc.color_b  = 0x7A1512;
    app->desc.order    = 46;
    /* It starts WITHOUT background: it switches itself on when recording
     * begins. See aplicar_background(). */
    app->desc.flags    = AOS_APP_FLAG_NONE;

    app->create  = rec_create;
    app->destroy = rec_destroy;
    app->back    = rec_back;
    app->button  = rec_button;
    return true;
}

AOS_APP_ENTRY(rec_init);
