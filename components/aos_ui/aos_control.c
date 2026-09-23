/*
 * AmoledOS - The control centre (see aos_control.h).
 *
 * It is a child of the active screen, created last so it sits above the
 * stage (face, launcher, status bar), and below lv_layer_top, where the
 * notification overlay and the toasts live: a notification or a tile's toast
 * still shows over it. Not on lv_layer_top itself: the screen captures
 * (tools/captura.py, the simulator's AOS_SIM_SHOT) only see the screen.
 *
 * The watchface under it is hidden once the panel has finished covering it
 * (aos_ui.c does that for the launcher and the apps too): a face redrawing
 * its seconds under an opaque panel is paid for on every frame for nothing.
 *
 * Everything fits on one screen on purpose: a panel that scrolled would fight
 * the swipe up that closes it.
 */
#include "aos_control.h"
#include "aos_quick.h"
#include "aos_ui.h"
#include "aos_internal.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_settings_glyphs.h"

#include <stdio.h>
#include <string.h>

#define PAD_SIDE     18
#define CONTENT_W    (AOS_SCREEN_W - 2 * PAD_SIDE)
#define ANIM_MS      220
#define REFRESH_MS   1000

static struct {
    lv_obj_t *root;
    lv_obj_t *date, *batt, *phone;
    lv_obj_t *tiles;
    lv_obj_t *music;                /* the player row, hidden with no music */
    lv_obj_t *player, *play;
    uint32_t  last_refresh;
    bool      closing;
} s_cc;

static lv_obj_t *glyph(lv_obj_t *parent, const char *g, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, g);
    lv_obj_set_style_text_font(l, &aos_settings_font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

/* A row that holds its children in a line, transparent. */
static lv_obj_t *row(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (l && strcmp(lv_label_get_text(l), t) != 0) {
        lv_label_set_text(l, t);
    }
}

/* --------------------------------------------------------------------------
 * What changes while it is up
 * -------------------------------------------------------------------------- */

static void refresh(void)
{
    if (!s_cc.root) {
        return;
    }
    char buf[64];

    struct tm now;
    aos_hal_time_now(&now);
    snprintf(buf, sizeof(buf), "%s %d %s", aos_day_name(now.tm_wday), now.tm_mday,
             aos_month_name(now.tm_mon));
    set_text(s_cc.date, buf);

    aos_battery_t b;
    if (aos_hal_battery_read(&b) && b.percent >= 0) {
        snprintf(buf, sizeof(buf), "%d %%%s", b.percent,
                 b.charging ? "  " AOS_SG_LIGHTNING_BOLT : "");
    } else {
        buf[0] = '\0';
    }
    set_text(s_cc.batt, buf);

    int pila = -1;
    if (aos_hal_bt_state() == AOS_BT_CONNECTED && aos_hal_bt_phone_battery(&pila) && pila >= 0) {
        snprintf(buf, sizeof(buf), AOS_SG_CELLPHONE " %d %%", pila);
    } else {
        buf[0] = '\0';
    }
    set_text(s_cc.phone, buf);

    aos_quick_tiles_paint(s_cc.tiles);

    /* The player only with something to control: the phone connected and a
     * track, playing or paused. */
    aos_media_info_t mi;
    bool hay = aos_hal_media_link() == AOS_MEDIA_CONNECTED && aos_hal_media_info(&mi) &&
               (mi.playing || (mi.has_metadata && mi.title[0]));
    if (s_cc.music) {
        if (hay) {
            /* Which app is playing, not the track: a title and an artist do
             * not fit beside three buttons, and cut short they said less
             * than "Spotify" does. The track is in the Music app. */
            const char *who = aos_hal_media_player();
            set_text(s_cc.player, who && who[0] ? who : _("Música"));
            set_text(s_cc.play, mi.playing ? AOS_SG_PAUSE : AOS_SG_PLAY);
            lv_obj_remove_flag(s_cc.music, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_cc.music, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void aos_control_tick(void)
{
    if (!s_cc.root || s_cc.closing) {
        return;
    }
    uint32_t t = (uint32_t)aos_hal_uptime_ms();
    if (t - s_cc.last_refresh >= REFRESH_MS) {
        s_cc.last_refresh = t;
        refresh();
    }
}

/* --------------------------------------------------------------------------
 * Controls
 * -------------------------------------------------------------------------- */

static void brightness_cb(lv_event_t *e)
{
    aos_hal_brightness_set((int)lv_slider_get_value(lv_event_get_target(e)));
    aos_hal_activity();
}

static void volume_cb(lv_event_t *e)
{
    aos_hal_volume_set((int)lv_slider_get_value(lv_event_get_target(e)));
    aos_hal_activity();
}

/* One beep when the finger lifts, at the new volume: beeping on every step
 * of the drag is a buzz. */
static void volume_released_cb(lv_event_t *e)
{
    (void)e;
    aos_hal_beep(1000, 40);
}

static void media_cb(lv_event_t *e)
{
    aos_hal_activity();
    aos_hal_media_command((aos_media_cmd_t)(intptr_t)lv_event_get_user_data(e));
    s_cc.last_refresh = 0;                 /* show the new state on the next tick */
}

static void settings_cb(lv_event_t *e)
{
    (void)e;
    aos_hal_activity();
    /* Deferred, and aos_ui_open() closes this panel on its way. */
    aos_ui_request_open("aos.settings");
}

static lv_obj_t *media_button(lv_obj_t *parent, const char *g, aos_media_cmd_t cmd)
{
    lv_obj_t *b = glyph(parent, g, AOS_C_TEXT);
    lv_obj_set_style_pad_all(b, 8, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_opa(b, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_add_event_cb(b, media_cb, LV_EVENT_CLICKED, (void *)(intptr_t)cmd);
    return b;
}

/* A card: the look of Settings' rows. */
static lv_obj_t *card(lv_obj_t *parent, int32_t h)
{
    lv_obj_t *c = row(parent, CONTENT_W, h);
    lv_obj_set_style_bg_color(c, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_pad_column(c, 8, 0);
    return c;
}

/* --------------------------------------------------------------------------
 * Opening and closing
 * -------------------------------------------------------------------------- */

static void anim_y_cb(void *obj, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)obj, v);
}

static void shown_cb(lv_anim_t *a)
{
    (void)a;
    aos_ui_face_hide_if_covered();
}

static void gone_cb(lv_anim_t *a)
{
    lv_obj_delete((lv_obj_t *)a->var);
}

static void slide(lv_obj_t *obj, int32_t from, int32_t to, lv_anim_completed_cb_t done)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

bool aos_control_visible(void)
{
    return s_cc.root != NULL && !s_cc.closing;
}

void aos_control_open(void)
{
    if (s_cc.root) {
        return;
    }
    uint64_t t0 = aos_hal_uptime_ms();
    memset(&s_cc, 0, sizeof(s_cc));

    lv_obj_t *p = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_set_style_bg_color(p, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(p, AOS_C_TEXT, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(p, 14, 0);
    lv_obj_set_style_pad_row(p, 8, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    /* Clickable, so a tap on an empty spot stays on the panel and does not
     * fall through to the face underneath. */
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    s_cc.root = p;

    /* The date on the left, the batteries on the right. */
    lv_obj_t *top = row(p, CONTENT_W - 8, 28);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    s_cc.date = aos_label(top, "", aos_font_small, AOS_C_DIM);
    lv_obj_t *right = row(top, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_pad_column(right, 12, 0);
    s_cc.phone = aos_label(right, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_style_text_font(s_cc.phone, aos_font_small, 0);
    s_cc.batt = aos_label(right, "", aos_font_small, AOS_C_TEXT);
    /* The glyphs inside those two labels come from the icon font, the digits
     * from the text font: a fallback chain, text first. */
    static lv_font_t mixed;
    mixed = *aos_font_small;
    mixed.fallback = &aos_settings_font;
    lv_obj_set_style_text_font(s_cc.phone, &mixed, 0);
    lv_obj_set_style_text_font(s_cc.batt, &mixed, 0);
    aos_make_decorative(top);

    s_cc.tiles = aos_quick_tiles_create(p, CONTENT_W, 68);

    aos_quick_slider(p, AOS_SG_WHITE_BALANCE_SUNNY, aos_hal_brightness_get(), 5, 100,
                     CONTENT_W, 42, brightness_cb);
    lv_obj_t *vol = aos_quick_slider(p, AOS_SG_VOLUME_HIGH, aos_hal_volume_get(), 0, 100,
                                     CONTENT_W, 42, volume_cb);
    lv_obj_add_event_cb(vol, volume_released_cb, LV_EVENT_RELEASED, NULL);

    /* The player: previous, which app is playing, play/pause, next. One
     * line, as tall as the Settings row. */
    s_cc.music = card(p, 46);
    media_button(s_cc.music, AOS_SG_SKIP_PREVIOUS, AOS_MEDIA_PREV);
    lv_obj_t *mid = row(s_cc.music, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(mid, 1);
    lv_obj_set_style_pad_column(mid, 6, 0);
    glyph(mid, AOS_SG_MUSIC_NOTE, AOS_C_PINK);
    s_cc.player = aos_label(mid, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_flex_grow(s_cc.player, 1);
    /* Height of one line: with the height free, DOTS wraps instead of
     * cutting, which is how the track's name ran out of the row. */
    lv_obj_set_height(s_cc.player, lv_font_get_line_height(aos_font_body));
    lv_label_set_long_mode(s_cc.player, LV_LABEL_LONG_MODE_DOTS);
    aos_make_decorative(mid);
    s_cc.play = media_button(s_cc.music, AOS_SG_PLAY, AOS_MEDIA_PLAY_PAUSE);
    media_button(s_cc.music, AOS_SG_SKIP_NEXT, AOS_MEDIA_NEXT);
    lv_obj_add_flag(s_cc.music, LV_OBJ_FLAG_HIDDEN);

    /* Into Settings. */
    lv_obj_t *set = card(p, 46);
    glyph(set, AOS_SG_COG, AOS_C_DIM);
    lv_obj_t *sl = aos_label(set, _("Ajustes"), aos_font_body, AOS_C_TEXT);
    lv_obj_set_flex_grow(sl, 1);
    glyph(set, AOS_SG_CHEVRON_RIGHT, lv_color_hex(0x636366));
    aos_make_decorative(set);
    lv_obj_add_flag(set, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(set, AOS_C_CARD2, LV_STATE_PRESSED);
    lv_obj_add_event_cb(set, settings_cb, LV_EVENT_CLICKED, NULL);

    /* The grabber says which way it goes back. */
    lv_obj_t *grab = lv_obj_create(p);
    lv_obj_remove_style_all(grab);
    lv_obj_set_size(grab, 46, 5);
    lv_obj_set_style_radius(grab, 3, 0);
    lv_obj_set_style_bg_color(grab, lv_color_hex(0x48484A), 0);
    lv_obj_set_style_bg_opa(grab, LV_OPA_COVER, 0);

    refresh();
    s_cc.last_refresh = (uint32_t)aos_hal_uptime_ms();
    aos_hal_log("ui", "control centre built in %u ms",
                (unsigned)(aos_hal_uptime_ms() - t0));

    slide(p, -AOS_SCREEN_H, 0, shown_cb);
}

void aos_control_close(bool animate)
{
    if (!s_cc.root || s_cc.closing) {
        return;
    }
    lv_obj_t *p = s_cc.root;
    aos_ui_face_show();
    if (animate) {
        s_cc.closing = true;
        lv_anim_delete(p, anim_y_cb);
        slide(p, lv_obj_get_y(p), -AOS_SCREEN_H, gone_cb);
        /* The pointers go now: the tick must not touch a panel on its way
         * out, and a new one may open before this one is gone. */
        memset(&s_cc, 0, sizeof(s_cc));
    } else {
        memset(&s_cc, 0, sizeof(s_cc));
        lv_obj_delete_async(p);
    }
}
