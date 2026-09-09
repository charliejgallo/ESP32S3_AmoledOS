/*
 * AmoledOS - Local music.
 *
 * Lists whatever is in the microSD's music folder and plays it with the
 * board's codec. Two views: the list and the player.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#define MAX_TRACKS      64
#define NAME_MAX_LEN    72

typedef struct {
    char names[MAX_TRACKS][NAME_MAX_LEN];
    int  count;
    int  current;

    lv_obj_t *list_view;
    lv_obj_t *player_view;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    lv_obj_t *progress;
    lv_obj_t *elapsed;
    lv_obj_t *total;
    lv_obj_t *play_btn;
    lv_obj_t *play_label;
    lv_timer_t *timer;
} music_t;

AOS_BSS_PSRAM static music_t s_music;

static bool has_audio_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return false;
    }
    return strcasecmp(dot, ".wav") == 0 || strcasecmp(dot, ".mp3") == 0;
}

static void scan(void)
{
    s_music.count = 0;

    DIR *dir = opendir(aos_hal_path_music());
    if (!dir) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_music.count < MAX_TRACKS) {
        if (entry->d_name[0] == '.' || !has_audio_ext(entry->d_name)) {
            continue;
        }
        size_t len = strnlen(entry->d_name, NAME_MAX_LEN - 1);
        memcpy(s_music.names[s_music.count], entry->d_name, len);
        s_music.names[s_music.count][len] = '\0';
        s_music.count++;
    }
    closedir(dir);
}

static void format_time(char *out, size_t len, uint32_t seconds)
{
    snprintf(out, len, "%u:%02u", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
}

static void show_player(bool playing_view)
{
    if (playing_view) {
        lv_obj_add_flag(s_music.list_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_music.player_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_music.list_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_music.player_view, LV_OBJ_FLAG_HIDDEN);
    }
}

static void play_index(int index)
{
    if (index < 0 || index >= s_music.count) {
        return;
    }
    s_music.current = index;

    char path[240];
    snprintf(path, sizeof(path), "%s/%s", aos_hal_path_music(), s_music.names[index]);

    if (!aos_hal_player_play(path)) {
        aos_ui_toast(_("No se pudo reproducir"), 1600);
        return;
    }
    show_player(true);
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;

    aos_player_status_t status;
    if (!aos_hal_player_status(&status)) {
        return;
    }

    lv_label_set_text(s_music.title, status.title[0] ? status.title : "-");

    char buf[96];
    snprintf(buf, sizeof(buf), "%u kHz  %s",
             (unsigned)(status.sample_rate / 1000),
             status.channels == 2 ? "estereo" : "mono");
    lv_label_set_text(s_music.subtitle, buf);

    char elapsed[12], total[12];
    format_time(elapsed, sizeof(elapsed), status.position_s);
    format_time(total, sizeof(total), status.duration_s);
    lv_label_set_text(s_music.elapsed, elapsed);
    lv_label_set_text(s_music.total, total);

    int32_t percent = status.duration_s
                    ? (int32_t)(status.position_s * 100 / status.duration_s)
                    : 0;
    lv_bar_set_value(s_music.progress, percent, LV_ANIM_OFF);

    lv_label_set_text(s_music.play_label,
                      status.state == AOS_PLAYER_PLAYING ? LV_SYMBOL_PAUSE
                                                         : LV_SYMBOL_PLAY);

    /* when a track ends the next one follows */
    if (status.state == AOS_PLAYER_STOPPED && status.position_s == 0 &&
        s_music.count > 1 && !lv_obj_has_flag(s_music.player_view, LV_OBJ_FLAG_HIDDEN)) {
        play_index((s_music.current + 1) % s_music.count);
    }
}

/* -------------------------------------------------------------------------- */

static void open_cb(lv_event_t *event)
{
    play_index((int)(intptr_t)lv_event_get_user_data(event));
}

static void play_cb(lv_event_t *event)
{
    (void)event;
    aos_player_status_t status;
    if (!aos_hal_player_status(&status)) {
        return;
    }
    if (status.state == AOS_PLAYER_PLAYING) {
        aos_hal_player_pause();
    } else if (status.state == AOS_PLAYER_PAUSED) {
        aos_hal_player_resume();
    } else {
        play_index(s_music.current);
    }
}

static void skip_cb(lv_event_t *event)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(event);
    if (s_music.count == 0) {
        return;
    }
    play_index((s_music.current + delta + s_music.count) % s_music.count);
}

static void volume_cb(lv_event_t *event)
{
    aos_hal_volume_set((int)lv_slider_get_value(lv_event_get_target(event)));
}

/* -------------------------------------------------------------------------- */

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);
    scan();

    /* --- list --- */
    s_music.list_view = lv_obj_create(page);
    lv_obj_remove_style_all(s_music.list_view);
    lv_obj_set_size(s_music.list_view, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_music.list_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_music.list_view, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_music.list_view, 8, 0);
    lv_obj_set_style_pad_ver(s_music.list_view, 16, 0);
    lv_obj_set_scroll_dir(s_music.list_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_music.list_view, LV_SCROLLBAR_MODE_OFF);

    if (s_music.count == 0) {
        char msg[200];
        snprintf(msg, sizeof(msg), _("No hay musica en\n%s\n\nse aceptan .wav y .mp3"),
                 aos_hal_path_music());
        lv_obj_t *empty = aos_label(s_music.list_view, msg, aos_font_body, AOS_C_DIM);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    }

    for (int i = 0; i < s_music.count; i++) {
        lv_obj_t *row = lv_obj_create(s_music.list_view);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, AOS_SCREEN_W - 56, 58);
        lv_obj_set_style_bg_color(row, AOS_C_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *icon = aos_label(row, LV_SYMBOL_AUDIO, aos_font_body, AOS_C_PINK);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *name = aos_label(row, s_music.names[i], aos_font_body, AOS_C_TEXT);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, AOS_SCREEN_W - 130);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 52, 0);
        lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
    }

    /* --- player --- */
    s_music.player_view = lv_obj_create(page);
    lv_obj_remove_style_all(s_music.player_view);
    lv_obj_set_size(s_music.player_view, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(s_music.player_view, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *art = lv_obj_create(s_music.player_view);
    lv_obj_remove_style_all(art);
    lv_obj_set_size(art, 132, 132);
    lv_obj_set_style_radius(art, 30, 0);
    lv_obj_set_style_bg_color(art, lv_color_hex(0xFF375F), 0);
    lv_obj_set_style_bg_grad_color(art, lv_color_hex(0x7A2FA0), 0);
    lv_obj_set_style_bg_grad_dir(art, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    lv_obj_align(art, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *note = aos_label(art, LV_SYMBOL_AUDIO, aos_font_title, AOS_C_TEXT);
    lv_obj_center(note);

    s_music.title = aos_label_boxed(s_music.player_view, "-", aos_font_title,
                                    AOS_C_TEXT, AOS_SCREEN_W - 40, 32);
    lv_obj_align(s_music.title, LV_ALIGN_TOP_MID, 0, 164);
    lv_label_set_long_mode(s_music.title, LV_LABEL_LONG_DOT);

    s_music.subtitle = aos_label_boxed(s_music.player_view, "", aos_font_small,
                                       AOS_C_DIM, AOS_SCREEN_W - 40, 20);
    lv_obj_align(s_music.subtitle, LV_ALIGN_TOP_MID, 0, 198);

    s_music.progress = lv_bar_create(s_music.player_view);
    lv_obj_set_size(s_music.progress, AOS_SCREEN_W - 90, 6);
    lv_obj_align(s_music.progress, LV_ALIGN_TOP_MID, 0, 232);
    lv_obj_set_style_bg_color(s_music.progress, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_music.progress, AOS_C_PINK, LV_PART_INDICATOR);

    s_music.elapsed = aos_label(s_music.player_view, "0:00", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_music.elapsed, LV_ALIGN_TOP_LEFT, 45, 244);
    s_music.total = aos_label(s_music.player_view, "0:00", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_music.total, LV_ALIGN_TOP_RIGHT, -45, 244);

    lv_obj_t *prev = aos_button(s_music.player_view, LV_SYMBOL_PREV, AOS_C_CARD2,
                                skip_cb, (void *)(intptr_t)-1);
    lv_obj_set_size(prev, 74, 60);
    lv_obj_align(prev, LV_ALIGN_TOP_LEFT, 30, 280);

    s_music.play_btn = aos_button(s_music.player_view, LV_SYMBOL_PLAY, AOS_C_PINK,
                                  play_cb, NULL);
    lv_obj_set_size(s_music.play_btn, 92, 60);
    lv_obj_align(s_music.play_btn, LV_ALIGN_TOP_MID, 0, 280);
    s_music.play_label = lv_obj_get_child(s_music.play_btn, 0);

    lv_obj_t *next = aos_button(s_music.player_view, LV_SYMBOL_NEXT, AOS_C_CARD2,
                                skip_cb, (void *)(intptr_t)1);
    lv_obj_set_size(next, 74, 60);
    lv_obj_align(next, LV_ALIGN_TOP_RIGHT, -30, 280);

    lv_obj_t *volume = lv_slider_create(s_music.player_view);
    lv_obj_set_size(volume, AOS_SCREEN_W - 110, 12);
    lv_obj_align(volume, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_slider_set_range(volume, 0, 100);
    lv_slider_set_value(volume, aos_hal_volume_get(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(volume, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume, AOS_C_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume, AOS_C_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(volume, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

    show_player(false);
    s_music.timer = lv_timer_create(refresh, 250, NULL);
    refresh(NULL);
    return &s_music;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_music.timer) {
        lv_timer_delete(s_music.timer);
        s_music.timer = NULL;
    }
    s_music.list_view = NULL;
    s_music.player_view = NULL;
}

/* Back: from the player to the list, and from the list to the menu. The music
 * goes on playing: the player lives in the HAL, not in the app. */
static bool back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_music.player_view && !lv_obj_has_flag(s_music.player_view, LV_OBJ_FLAG_HIDDEN)) {
        show_player(false);
        return true;
    }
    return false;
}

void aos_app_music_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.music",
            .name     = "Musica",
            .icon     = LV_SYMBOL_AUDIO,
            .color_a  = 0xFF375F,
            .color_b  = 0x7A2FA0,
            .order    = 45,
        },
        .create  = create,
        .destroy = destroy,
        .back    = back,
    };
}
