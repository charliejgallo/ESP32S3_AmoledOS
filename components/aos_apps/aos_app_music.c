/*
 * AmoledOS - Local music.
 *
 * Lists the microSD's music folder, subfolders included, and plays through
 * the HAL's player. Two views: the list and the player.
 *
 * The app is only the remote control. The queue, the next track and the
 * decoding live in the HAL (aos_hal_player_*), so the music goes on with the
 * app closed and a folder plays through to its end and round again. Opening
 * the app while something plays goes straight to the player, in the folder
 * of what is playing.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_text_safe.h"
#include "../aos_hal/aos_audio.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef AOS_SIM
#define entries_alloc(n)    malloc(n)
#else
#include "esp_heap_caps.h"
#define entries_alloc(n)    heap_caps_malloc(n, MALLOC_CAP_SPIRAM)
#endif

#define MAX_ENTRIES     256
#define NAME_LEN        256             /* FAT's long names go to 255 */

typedef struct {
    bool dir;
    char name[NAME_LEN];
} entry_t;

typedef struct {
    char     cwd[200];                  /* the folder on show */
    entry_t *entries;                   /* MAX_ENTRIES, PSRAM, only while open */
    int      count;

    lv_obj_t *list_view;
    lv_obj_t *player_view;
    lv_obj_t *now_row, *now_icon, *now_title, *now_artist;
    lv_obj_t *count_label, *shuffle_btn;
    lv_obj_t *title, *artist, *format;
    lv_obj_t *progress;
    lv_obj_t *elapsed, *total;
    lv_obj_t *play_label;
    lv_timer_t *timer;
    bool      seeking;                  /* the finger is on the progress bar */
    bool      has_last;                 /* a track to go on from, after a restart */
    char      last_title[96], last_artist[96];
    uint32_t  last_pos;
    char      shown[256];               /* the path the player view shows */
} music_t;

AOS_BSS_PSRAM static music_t s_music;

static void build_list(void);

/* ---- small things ---------------------------------------------------------- */

static int entry_cmp(const void *a, const void *b)
{
    const entry_t *x = a, *y = b;
    if (x->dir != y->dir) {
        return x->dir ? -1 : 1;         /* folders first */
    }
    return aos_audio_name_cmp(x->name, y->name);
}

static void scan(void)
{
    s_music.count = 0;
    DIR *dir = opendir(s_music.cwd);
    if (!dir) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL && s_music.count < MAX_ENTRIES) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= NAME_LEN) {
            continue;
        }
        bool is_dir = e->d_type == DT_DIR;
        if (e->d_type == DT_UNKNOWN) {
            char full[480];
            struct stat st;
            snprintf(full, sizeof(full), "%s/%s", s_music.cwd, e->d_name);
            is_dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        }
        if (!is_dir && !aos_audio_is_playable(e->d_name)) {
            continue;
        }
        entry_t *en = &s_music.entries[s_music.count++];
        en->dir = is_dir;
        snprintf(en->name, sizeof(en->name), "%s", e->d_name);
    }
    closedir(dir);
    qsort(s_music.entries, (size_t)s_music.count, sizeof(entry_t), entry_cmp);
}

static void format_time(char *out, size_t len, uint32_t ms)
{
    uint32_t s = ms / 1000;
    if (s >= 3600) {
        snprintf(out, len, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60),
                 (unsigned)(s % 60));
    } else {
        snprintf(out, len, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    }
}

/* Only when it changed: a label that is set is redrawn, and this runs four
 * times a second with the screen on. */
static void set_text(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void set_text_safe(lv_obj_t *label, const char *text)
{
    char buf[200];
    aos_text_safe(buf, sizeof(buf), text);
    set_text(label, buf);
}

/* "Artist - Title.mp3" into its two halves; no " - ", all of it is title. */
static void split_name(const char *path, char *title, size_t tlen, char *artist, size_t alen)
{
    const char *slash = strrchr(path, '/');
    char name[NAME_LEN];
    snprintf(name, sizeof(name), "%s", slash ? slash + 1 : path);
    char *dot = strrchr(name, '.');
    if (dot) {
        *dot = '\0';
    }
    /* long names are cut to the label's buffer, which is what shows anyway */
    char *sep = strstr(name, " - ");
    const char *t = name;
    artist[0] = '\0';
    if (sep) {
        *sep = '\0';
        t = sep + 3;
        while (*t == ' ') {
            t++;
        }
        if (snprintf(artist, alen, "%s", name) >= (int)alen) {
            artist[alen - 1] = '\0';
        }
    }
    if (snprintf(title, tlen, "%s", t) >= (int)tlen) {
        title[tlen - 1] = '\0';
    }
}

static bool at_root(void)
{
    return strcmp(s_music.cwd, aos_hal_path_music()) == 0;
}

static void show_player(bool player)
{
    if (player) {
        lv_obj_add_flag(s_music.list_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_music.player_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_music.list_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_music.player_view, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- the player view ------------------------------------------------------- */

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    aos_player_info_t in;
    if (!aos_hal_player_info(&in)) {
        return;
    }
    bool active = in.state != AOS_PLAYER_STOPPED;

    /* The list's top row: what is playing, in pink; with nothing playing,
     * the last track and where it was, to go on from there. */
    if (s_music.now_row) {
        if (active) {
            set_text(s_music.now_icon, in.state == AOS_PLAYER_PLAYING ? LV_SYMBOL_PLAY
                                                                      : LV_SYMBOL_PAUSE);
            set_text_safe(s_music.now_title, in.title);
            set_text_safe(s_music.now_artist, in.artist);
            lv_obj_set_style_bg_color(s_music.now_row, AOS_C_PINK, 0);
            lv_obj_set_style_text_color(s_music.now_icon, AOS_C_TEXT, 0);
            lv_obj_set_style_text_color(s_music.now_artist, AOS_C_TEXT, 0);
            lv_obj_remove_flag(s_music.now_row, LV_OBJ_FLAG_HIDDEN);
        } else if (s_music.has_last) {
            char when[16], sub[140];
            format_time(when, sizeof(when), s_music.last_pos);
            snprintf(sub, sizeof(sub), "%s %s%s%s", _("Continuar"), when,
                     s_music.last_artist[0] ? " \u00b7 " : "", s_music.last_artist);
            set_text(s_music.now_icon, LV_SYMBOL_PLAY);
            set_text_safe(s_music.now_title, s_music.last_title);
            set_text_safe(s_music.now_artist, sub);
            lv_obj_set_style_bg_color(s_music.now_row, AOS_C_CARD2, 0);
            lv_obj_set_style_text_color(s_music.now_icon, AOS_C_PINK, 0);
            lv_obj_set_style_text_color(s_music.now_artist, AOS_C_DIM, 0);
            lv_obj_remove_flag(s_music.now_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_music.now_row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (!active && !in.path[0]) {
        return;
    }
    char safe[200];
    aos_text_safe(safe, sizeof(safe), in.title[0] ? in.title : "-");
    if (strcmp(lv_label_get_text(s_music.title), safe) != 0) {
        lv_label_set_text(s_music.title, safe);
        /* Two lines when the title needs them, one when not: the artist and
         * the format follow right under it, and the block keeps its middle
         * where it is. */
        lv_point_t size;
        int32_t line = lv_font_get_line_height(aos_font_title);
        lv_text_get_size(&size, safe, aos_font_title, 0, 0, AOS_SCREEN_W - 40, LV_TEXT_FLAG_NONE);
        int lines = size.y > line ? 2 : 1;
        lv_obj_set_height(s_music.title, lines * line);
        lv_obj_align(s_music.title, LV_ALIGN_TOP_MID, 0, lines == 2 ? 136 : 136 + line / 2);
        lv_obj_align_to(s_music.artist, s_music.title, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
        lv_obj_align_to(s_music.format, s_music.artist, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    }
    set_text_safe(s_music.artist, in.artist);

    char buf[64];
    if (in.format && in.format[0]) {
        if (in.kbps) {
            snprintf(buf, sizeof(buf), "%s  %u kbps%s  %u kHz", in.format, in.kbps,
                     in.vbr ? " VBR" : "", (unsigned)(in.sample_rate / 1000));
        } else {
            snprintf(buf, sizeof(buf), "%s  %u kHz", in.format, (unsigned)(in.sample_rate / 1000));
        }
    } else {
        buf[0] = '\0';
    }
    set_text(s_music.format, buf);

    if (in.count > 0) {
        snprintf(buf, sizeof(buf), "%d / %d", in.index + 1, in.count);
    } else {
        buf[0] = '\0';
    }
    set_text(s_music.count_label, buf);
    lv_obj_set_style_text_color(lv_obj_get_child(s_music.shuffle_btn, 0),
                                in.shuffle ? AOS_C_PINK : AOS_C_DIM, 0);
    if (in.count > 1) {
        lv_obj_remove_flag(s_music.shuffle_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_music.shuffle_btn, LV_OBJ_FLAG_HIDDEN);
    }

    char t1[16], t2[16];
    uint32_t pos = in.position_ms;
    if (s_music.seeking && in.duration_ms) {
        pos = (uint32_t)((uint64_t)lv_slider_get_value(s_music.progress) * in.duration_ms / 1000);
    } else {
        int32_t v = in.duration_ms ? (int32_t)((uint64_t)in.position_ms * 1000 / in.duration_ms) : 0;
        if (lv_slider_get_value(s_music.progress) != v) {
            lv_slider_set_value(s_music.progress, v, LV_ANIM_OFF);
        }
    }
    format_time(t1, sizeof(t1), pos);
    format_time(t2, sizeof(t2), in.duration_ms);
    set_text(s_music.elapsed, t1);
    set_text(s_music.total, t2);

    set_text(s_music.play_label, in.state == AOS_PLAYER_PLAYING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    snprintf(s_music.shown, sizeof(s_music.shown), "%s", in.path);
}

static void play_path(const char *name)
{
    char path[480];
    snprintf(path, sizeof(path), "%s/%s", s_music.cwd, name);
    if (!aos_hal_player_play_folder(path)) {
        aos_ui_toast(_("No se pudo reproducir"), 1600);
        return;
    }
    show_player(true);
    refresh(NULL);
}

static void play_cb(lv_event_t *event)
{
    (void)event;
    aos_player_info_t in;
    aos_hal_player_info(&in);
    if (in.state == AOS_PLAYER_PLAYING) {
        aos_hal_player_pause();
    } else if (in.state == AOS_PLAYER_PAUSED) {
        aos_hal_player_resume();
    } else if (s_music.shown[0]) {
        /* stopped with a track on show (a single file that ended): again */
        if (!aos_hal_player_play_folder(s_music.shown)) {
            aos_ui_toast(_("No se pudo reproducir"), 1600);
        }
    } else if (s_music.has_last) {
        s_music.has_last = false;
        if (!aos_hal_player_resume_last()) {
            aos_ui_toast(_("No se pudo reproducir"), 1600);
        }
    }
    refresh(NULL);
}

static void skip_cb(lv_event_t *event)
{
    if ((intptr_t)lv_event_get_user_data(event) > 0) {
        aos_hal_player_next();
    } else {
        aos_hal_player_prev();
    }
}

static void shuffle_cb(lv_event_t *event)
{
    (void)event;
    aos_player_info_t in;
    aos_hal_player_info(&in);
    aos_hal_player_set_shuffle(!in.shuffle);
    aos_ui_toast(in.shuffle ? _("En orden") : _("Aleatorio"), 1000);
    refresh(NULL);
}

static void seek_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        s_music.seeking = true;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        aos_player_info_t in;
        aos_hal_player_info(&in);
        if (in.duration_ms) {
            aos_hal_player_seek((uint32_t)((uint64_t)lv_slider_get_value(s_music.progress) *
                                           in.duration_ms / 1000));
        }
        s_music.seeking = false;
    }
}

static void volume_cb(lv_event_t *event)
{
    /* the player applies it on its next block, 20 ms */
    aos_hal_volume_set((int)lv_slider_get_value(lv_event_get_target(event)));
}

/* ---- the list -------------------------------------------------------------- */

static void open_cb(lv_event_t *event)
{
    int i = (int)(intptr_t)lv_event_get_user_data(event);
    if (i < 0 || i >= s_music.count) {
        return;
    }
    entry_t *en = &s_music.entries[i];
    if (en->dir) {
        size_t len = strlen(s_music.cwd);
        if (len + 1 + strlen(en->name) < sizeof(s_music.cwd)) {
            snprintf(s_music.cwd + len, sizeof(s_music.cwd) - len, "/%s", en->name);
            build_list();
        }
    } else {
        play_path(en->name);
    }
}

static void now_cb(lv_event_t *event)
{
    (void)event;
    aos_player_info_t in;
    aos_hal_player_info(&in);
    if (in.state == AOS_PLAYER_STOPPED) {
        if (!aos_hal_player_resume_last()) {
            aos_ui_toast(_("No se pudo reproducir"), 1600);
            return;
        }
        s_music.has_last = false;       /* it is playing now: the pink row */
    }
    show_player(true);
    refresh(NULL);
}

/* A row of the list: a glyph and one or two lines. 'sub', when there is one,
 * goes under in grey: a track shows its title over its artist. */
static lv_obj_t *row_create(lv_obj_t *parent, const char *symbol, lv_color_t symbol_color,
                            const char *text, const char *sub, lv_color_t text_color)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, AOS_SCREEN_W - 56, 58);
    lv_obj_set_style_bg_color(row, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = aos_label(row, symbol, aos_font_body, symbol_color);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);

    /* One line each, cut with dots: without a height a long name wraps and
     * spills out of the row. */
    char safe[NAME_LEN];
    aos_text_safe(safe, sizeof(safe), text);
    lv_obj_t *name = aos_label(row, safe, aos_font_body, text_color);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_size(name, AOS_SCREEN_W - 130, lv_font_get_line_height(aos_font_body));
    lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
    if (sub && sub[0]) {
        aos_text_safe(safe, sizeof(safe), sub);
        lv_obj_t *second = aos_label(row, safe, aos_font_small, AOS_C_DIM);
        lv_label_set_long_mode(second, LV_LABEL_LONG_DOT);
        lv_obj_set_size(second, AOS_SCREEN_W - 130, lv_font_get_line_height(aos_font_small));
        lv_obj_remove_flag(second, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 52, 7);
        lv_obj_align(second, LV_ALIGN_BOTTOM_LEFT, 52, -7);
    } else {
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 52, 0);
    }
    return row;
}

static void build_list(void)
{
    lv_obj_clean(s_music.list_view);
    s_music.now_row = NULL;
    scan();

    /* where we are, below the root */
    if (!at_root()) {
        const char *slash = strrchr(s_music.cwd, '/');
        /* the name alone through aos_text_safe: it does not know the
         * FontAwesome glyphs and turned the folder into a dot */
        char safe[NAME_LEN];
        aos_text_safe(safe, sizeof(safe), slash ? slash + 1 : s_music.cwd);
        char head[NAME_LEN + 8];
        snprintf(head, sizeof(head), LV_SYMBOL_DIRECTORY "  %s", safe);
        lv_obj_t *h = aos_label(s_music.list_view, head, aos_font_body, AOS_C_DIM);
        lv_label_set_long_mode(h, LV_LABEL_LONG_DOT);
        lv_obj_set_width(h, AOS_SCREEN_W - 72);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
    }

    /* what is playing, one tap from the player */
    s_music.now_row = row_create(s_music.list_view, LV_SYMBOL_PLAY, AOS_C_TEXT, "", " ", AOS_C_TEXT);
    lv_obj_set_style_bg_color(s_music.now_row, AOS_C_PINK, 0);
    s_music.now_icon = lv_obj_get_child(s_music.now_row, 0);
    s_music.now_title = lv_obj_get_child(s_music.now_row, 1);
    s_music.now_artist = lv_obj_get_child(s_music.now_row, 2);
    lv_obj_set_style_text_color(s_music.now_artist, AOS_C_TEXT, 0);
    lv_obj_add_event_cb(s_music.now_row, now_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_music.now_row, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < s_music.count; i++) {
        entry_t *en = &s_music.entries[i];
        /* a track: the extension says nothing, and "Artist - Title" reads
         * better as the title over the artist, like the player shows it */
        char label[NAME_LEN];
        const char *sub = NULL;
        char artist[NAME_LEN];
        snprintf(label, sizeof(label), "%s", en->name);
        if (!en->dir) {
            char *dot = strrchr(label, '.');
            if (dot) {
                *dot = '\0';
            }
            char *sep = strstr(label, " - ");
            if (sep) {
                snprintf(artist, sizeof(artist), "%.*s", (int)(sep - label), label);
                const char *title = sep + 3;
                while (*title == ' ') {
                    title++;
                }
                memmove(label, title, strlen(title) + 1);
                sub = artist;
            }
        }
        lv_obj_t *row = row_create(s_music.list_view,
                                   en->dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO,
                                   en->dir ? AOS_C_YELLOW : AOS_C_PINK, label, sub, AOS_C_TEXT);
        lv_obj_add_event_cb(row, open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    if (s_music.count == 0) {
        char msg[240];
        if (at_root()) {
            snprintf(msg, sizeof(msg), _("No hay musica en\n%s\n\nse aceptan .wav y .mp3"),
                     aos_hal_path_music());
        } else {
            snprintf(msg, sizeof(msg), "%s", _("Carpeta vacía"));
        }
        lv_obj_t *empty = aos_label(s_music.list_view, msg, aos_font_body, AOS_C_DIM);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_scroll_to_y(s_music.list_view, 0, LV_ANIM_OFF);
    refresh(NULL);
}

/* -------------------------------------------------------------------------- */

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);

    s_music.entries = entries_alloc(MAX_ENTRIES * sizeof(entry_t));
    s_music.count = 0;
    s_music.seeking = false;
    s_music.shown[0] = '\0';
    snprintf(s_music.cwd, sizeof(s_music.cwd), "%s", aos_hal_path_music());

    /* Something playing from under the music folder: open where it is.
     * Nothing playing but a last track remembered: open in its folder, with
     * the row to go on from there on top. */
    aos_player_info_t in;
    aos_hal_player_info(&in);
    bool active = in.state != AOS_PLAYER_STOPPED && in.path[0];
    char last[256];
    s_music.has_last = !active && aos_hal_player_last(last, sizeof(last), &s_music.last_pos);
    if (s_music.has_last) {
        split_name(last, s_music.last_title, sizeof(s_music.last_title),
                   s_music.last_artist, sizeof(s_music.last_artist));
    }
    const char *here = active ? in.path : (s_music.has_last ? last : NULL);
    size_t root_len = strlen(s_music.cwd);
    if (here && strncmp(here, s_music.cwd, root_len) == 0 && here[root_len] == '/') {
        const char *slash = strrchr(here, '/');
        size_t len = (size_t)(slash - here);
        if (len < sizeof(s_music.cwd)) {
            memcpy(s_music.cwd, here, len);
            s_music.cwd[len] = '\0';
        }
    }

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

    /* --- player --- */
    s_music.player_view = lv_obj_create(page);
    lv_obj_remove_style_all(s_music.player_view);
    lv_obj_set_size(s_music.player_view, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_music.player_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_music.player_view, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *art = lv_obj_create(s_music.player_view);
    lv_obj_remove_style_all(art);
    lv_obj_set_size(art, 112, 112);
    lv_obj_set_style_radius(art, 28, 0);
    lv_obj_set_style_bg_color(art, lv_color_hex(0xFF375F), 0);
    lv_obj_set_style_bg_grad_color(art, lv_color_hex(0x7A2FA0), 0);
    lv_obj_set_style_bg_grad_dir(art, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    lv_obj_align(art, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_t *note = aos_label(art, LV_SYMBOL_AUDIO, aos_font_title, AOS_C_TEXT);
    lv_obj_center(note);
    aos_make_decorative(art);

    s_music.count_label = aos_label_boxed(s_music.player_view, "", aos_font_small,
                                          AOS_C_DIM, 80, 20);
    lv_obj_align(s_music.count_label, LV_ALIGN_TOP_LEFT, 18, 30);

    s_music.shuffle_btn = aos_button(s_music.player_view, LV_SYMBOL_SHUFFLE, AOS_C_CARD2,
                                     shuffle_cb, NULL);
    lv_obj_set_size(s_music.shuffle_btn, 64, 48);
    lv_obj_align(s_music.shuffle_btn, LV_ALIGN_TOP_RIGHT, -18, 22);

    /* two lines of title: "Her Imagination and His planet (Original Mix)" */
    s_music.title = aos_label_boxed(s_music.player_view, "-", aos_font_title,
                                    AOS_C_TEXT, AOS_SCREEN_W - 40,
                                    2 * lv_font_get_line_height(aos_font_title));
    lv_label_set_long_mode(s_music.title, LV_LABEL_LONG_DOT);
    lv_obj_align(s_music.title, LV_ALIGN_TOP_MID, 0, 138);

    s_music.artist = aos_label_boxed(s_music.player_view, "", aos_font_body,
                                     AOS_C_DIM, AOS_SCREEN_W - 40,
                                     lv_font_get_line_height(aos_font_body));
    lv_label_set_long_mode(s_music.artist, LV_LABEL_LONG_DOT);
    lv_obj_align(s_music.artist, LV_ALIGN_TOP_MID, 0, 204);

    s_music.format = aos_label_boxed(s_music.player_view, "", aos_font_small,
                                     AOS_C_DIM, AOS_SCREEN_W - 40, 20);
    lv_obj_align(s_music.format, LV_ALIGN_TOP_MID, 0, 230);

    s_music.progress = lv_slider_create(s_music.player_view);
    lv_obj_set_size(s_music.progress, AOS_SCREEN_W - 90, 8);
    lv_obj_align(s_music.progress, LV_ALIGN_TOP_MID, 0, 264);
    lv_slider_set_range(s_music.progress, 0, 1000);
    lv_obj_set_style_bg_color(s_music.progress, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_music.progress, AOS_C_PINK, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_music.progress, AOS_C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_music.progress, 4, LV_PART_KNOB);
    /* a thin bar is a hard target: the touch area reaches past it */
    lv_obj_set_ext_click_area(s_music.progress, 18);
    lv_obj_add_event_cb(s_music.progress, seek_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_music.progress, seek_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_music.progress, seek_cb, LV_EVENT_PRESS_LOST, NULL);

    s_music.elapsed = aos_label(s_music.player_view, "0:00", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_music.elapsed, LV_ALIGN_TOP_LEFT, 45, 280);
    s_music.total = aos_label(s_music.player_view, "0:00", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_music.total, LV_ALIGN_TOP_RIGHT, -45, 280);

    lv_obj_t *prev = aos_button(s_music.player_view, LV_SYMBOL_PREV, AOS_C_CARD2,
                                skip_cb, (void *)(intptr_t)-1);
    lv_obj_set_size(prev, 74, 60);
    lv_obj_align(prev, LV_ALIGN_TOP_LEFT, 30, 310);

    lv_obj_t *play = aos_button(s_music.player_view, LV_SYMBOL_PLAY, AOS_C_PINK, play_cb, NULL);
    lv_obj_set_size(play, 92, 60);
    lv_obj_align(play, LV_ALIGN_TOP_MID, 0, 310);
    s_music.play_label = lv_obj_get_child(play, 0);

    lv_obj_t *next = aos_button(s_music.player_view, LV_SYMBOL_NEXT, AOS_C_CARD2,
                                skip_cb, (void *)(intptr_t)1);
    lv_obj_set_size(next, 74, 60);
    lv_obj_align(next, LV_ALIGN_TOP_RIGHT, -30, 310);

    lv_obj_t *volume = lv_slider_create(s_music.player_view);
    lv_obj_set_size(volume, AOS_SCREEN_W - 110, 12);
    lv_obj_align(volume, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_slider_set_range(volume, 0, 100);
    lv_slider_set_value(volume, aos_hal_volume_get(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(volume, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume, AOS_C_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume, AOS_C_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(volume, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (s_music.entries) {
        build_list();
    }
    show_player(active);
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
    free(s_music.entries);
    s_music.entries = NULL;
    s_music.list_view = NULL;
    s_music.player_view = NULL;
    s_music.now_row = NULL;
}

/* Back: from the player to the list, up one folder, and from the music
 * folder to the menu. The music goes on playing: the player lives in the HAL,
 * not in the app. */
static bool back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_music.player_view && !lv_obj_has_flag(s_music.player_view, LV_OBJ_FLAG_HIDDEN)) {
        show_player(false);
        return true;
    }
    if (s_music.list_view && s_music.entries && !at_root()) {
        char *slash = strrchr(s_music.cwd, '/');
        if (slash) {
            *slash = '\0';
            build_list();
            return true;
        }
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
