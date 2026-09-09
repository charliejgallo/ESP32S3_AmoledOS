/*
 * AmoledOS - Control of the phone's music.
 *
 * The ESP32-S3 has no Bluetooth Classic, so there is no AVRCP: the watch
 * presents itself as a BLE HID device and sends the media keys, which iOS and
 * Android both understand.
 *
 * The metadata (title, artist) only appears with an iPhone, because it arrives
 * over AMS, which is BLE. With Android you are left with the controls and the
 * notice that there is no information available.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <stdio.h>

typedef struct {
    lv_obj_t *status;
    lv_obj_t *title;
    lv_obj_t *artist;
    lv_obj_t *progress;
    lv_obj_t *play_label;
    lv_obj_t *controls;
    lv_obj_t *album;
    lv_obj_t *player;
    lv_obj_t *hint;
    lv_timer_t *timer;
} remote_t;

static remote_t s_remote;

static void command_cb(lv_event_t *event)
{
    aos_media_cmd_t cmd = (aos_media_cmd_t)(intptr_t)lv_event_get_user_data(event);
    if (!aos_hal_media_command(cmd)) {
        aos_ui_toast(_("Sin telefono conectado"), 1400);
        return;
    }
    aos_hal_beep(1400, 15);
}

static void toggle_cb(lv_event_t *event)
{
    (void)event;
    bool on = !aos_hal_media_enabled();
    aos_hal_media_enable(on);
    /* The button does NOT switch bluetooth on: it switches music control on
     * over the link that already exists. Saying "Bluetooth" here made people
     * believe that switching it off left the watch without notifications,
     * which is precisely the opposite. */
    aos_ui_toast(on ? _("Control de musica encendido")
                    : _("Control de musica apagado"), 1600);
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;

    aos_media_link_t link = aos_hal_media_link();
    /* artist + separator + album fit exactly in 160 */
    char buf[160];

    switch (link) {
    case AOS_MEDIA_OFF:
        snprintf(buf, sizeof(buf), LV_SYMBOL_CLOSE "  %s", _("apagado"));
        lv_label_set_text(s_remote.status, buf);
        lv_obj_set_style_text_color(s_remote.status, AOS_C_DIM, 0);
        break;
    case AOS_MEDIA_ADVERTISING:
        snprintf(buf, sizeof(buf), LV_SYMBOL_BLUETOOTH "  %s", _("buscando telefono"));
        lv_label_set_text(s_remote.status, buf);
        lv_obj_set_style_text_color(s_remote.status, AOS_C_ORANGE, 0);
        break;
    case AOS_MEDIA_CONNECTED:
        snprintf(buf, sizeof(buf), LV_SYMBOL_BLUETOOTH "  %s", aos_hal_media_peer());
        lv_label_set_text(s_remote.status, buf);
        lv_obj_set_style_text_color(s_remote.status, AOS_C_ACCENT, 0);
        break;
    }

    bool usable = (link == AOS_MEDIA_CONNECTED);
    lv_obj_set_style_opa(s_remote.controls, usable ? LV_OPA_COVER : LV_OPA_40, 0);

    aos_media_info_t info;
    if (usable && aos_hal_media_info(&info)) {
        if (info.has_metadata) {
            lv_label_set_text(s_remote.title, info.title);
            lv_label_set_text(s_remote.artist, info.artist);
            lv_label_set_text(s_remote.album, info.album);
            lv_label_set_text(s_remote.player, aos_hal_media_player());
            lv_obj_remove_flag(s_remote.progress, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(s_remote.progress,
                             info.duration_s
                                 ? (int32_t)(info.position_s * 100 / info.duration_s)
                                 : 0,
                             LV_ANIM_OFF);
            lv_label_set_text(s_remote.hint, "");
        } else {
            lv_label_set_text(s_remote.title, _("Reproduciendo"));
            lv_label_set_text(s_remote.artist, "");
            lv_label_set_text(s_remote.album, "");
            lv_label_set_text(s_remote.player, aos_hal_media_player());
            lv_obj_add_flag(s_remote.progress, LV_OBJ_FLAG_HIDDEN);
            /* Android does not publish the metadata over BLE, it only accepts the keys */
            lv_label_set_text(s_remote.hint, _("sin datos del tema"));
        }
        lv_label_set_text(s_remote.play_label,
                          info.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    } else {
        lv_label_set_text(s_remote.title, "-");
        lv_label_set_text(s_remote.artist, "");
        lv_label_set_text(s_remote.album, "");
        lv_label_set_text(s_remote.player, "");
        lv_obj_add_flag(s_remote.progress, LV_OBJ_FLAG_HIDDEN);
        /* Three different reasons for having no music, and saying them wrong
         * sends the user looking in the wrong place. */
        const char *por_que;
        if (aos_hal_bt_state() == AOS_BT_OFF) {
            por_que = _("prender el bluetooth en Ajustes");
        } else if (link == AOS_MEDIA_OFF) {
            por_que = _("tocar el boton para encender");
        } else if (!aos_hal_bt_bonded()) {
            por_que = _("emparejar desde el telefono");
        } else {
            por_que = _("buscando el telefono...");
        }
        lv_label_set_text(s_remote.hint, por_que);
    }
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);

    s_remote.status = aos_label_boxed(page, "", aos_font_small, AOS_C_DIM,
                                      AOS_SCREEN_W, 22);
    lv_obj_align(s_remote.status, LV_ALIGN_TOP_MID, 0, 14);

    /* There used to be a 110x110 box here called 'art': the place for the
     * album cover, drawn before the stack existed. **AMS sends no cover art**
     * -only text: title, artist, album, duration and state-, so that box could
     * never show what it promised, and meanwhile it ate the 110 px the artist
     * was short of, which was clipped with an ellipsis.
     *
     * In its place: the name of the app that is playing, and artist and album
     * on lines of their own and in full. Less ornament and more of what you
     * came to read. */
    s_remote.player = aos_label_boxed(page, "", aos_font_small, AOS_C_ACCENT,
                                      AOS_SCREEN_W - 40, 22);
    lv_label_set_long_mode(s_remote.player, LV_LABEL_LONG_DOT);
    lv_obj_align(s_remote.player, LV_ALIGN_TOP_MID, 0, 44);

    /* Two lines reserved for the title -a long track name is the norm, not the
     * exception- but CENTRED inside that box: otherwise a single-line title
     * sticks to the top and leaves a forty-pixel gap right in the middle of
     * the screen. The box is reserved anyway so that the artist and the album
     * do not jump when the title goes to two lines. */
    lv_obj_t *caja_titulo = lv_obj_create(page);
    lv_obj_remove_style_all(caja_titulo);
    lv_obj_set_size(caja_titulo, AOS_SCREEN_W - 40, 76);
    lv_obj_align(caja_titulo, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_flex_flow(caja_titulo, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(caja_titulo, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(caja_titulo, LV_OBJ_FLAG_SCROLLABLE);

    s_remote.title = aos_label(caja_titulo, "-", aos_font_title, AOS_C_TEXT);
    lv_label_set_long_mode(s_remote.title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_remote.title, lv_pct(100));
    lv_obj_set_height(s_remote.title, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(s_remote.title, 76, 0);
    lv_obj_set_style_text_align(s_remote.title, LV_TEXT_ALIGN_CENTER, 0);

    s_remote.artist = aos_label_boxed(page, "", aos_font_body, AOS_C_TEXT,
                                      AOS_SCREEN_W - 40, 26);
    lv_label_set_long_mode(s_remote.artist, LV_LABEL_LONG_DOT);
    lv_obj_align(s_remote.artist, LV_ALIGN_TOP_MID, 0, 158);

    s_remote.album = aos_label_boxed(page, "", aos_font_small, AOS_C_DIM,
                                     AOS_SCREEN_W - 40, 22);
    lv_label_set_long_mode(s_remote.album, LV_LABEL_LONG_DOT);
    lv_obj_align(s_remote.album, LV_ALIGN_TOP_MID, 0, 188);

    s_remote.progress = lv_bar_create(page);
    lv_obj_set_size(s_remote.progress, AOS_SCREEN_W - 90, 6);
    lv_obj_align(s_remote.progress, LV_ALIGN_TOP_MID, 0, 218);
    lv_obj_set_style_bg_color(s_remote.progress, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_remote.progress, AOS_C_ACCENT, LV_PART_INDICATOR);

    /* --- keypad --- */
    s_remote.controls = lv_obj_create(page);
    lv_obj_remove_style_all(s_remote.controls);
    lv_obj_set_size(s_remote.controls, lv_pct(100), 150);
    /* 236 and not 252: with the keypad higher up, the volume row ends at
     * y 388, just inside the touch ceiling (AOS_TOUCH_Y_MAX). */
    lv_obj_align(s_remote.controls, LV_ALIGN_TOP_MID, 0, 236);

    lv_obj_t *prev = aos_button(s_remote.controls, LV_SYMBOL_PREV, AOS_C_CARD2,
                                command_cb, (void *)(intptr_t)AOS_MEDIA_PREV);
    lv_obj_set_size(prev, 74, 60);
    lv_obj_align(prev, LV_ALIGN_TOP_LEFT, 30, 0);

    lv_obj_t *play = aos_button(s_remote.controls, LV_SYMBOL_PLAY, AOS_C_ACCENT,
                                command_cb, (void *)(intptr_t)AOS_MEDIA_PLAY_PAUSE);
    lv_obj_set_size(play, 92, 60);
    lv_obj_align(play, LV_ALIGN_TOP_MID, 0, 0);
    s_remote.play_label = lv_obj_get_child(play, 0);

    lv_obj_t *next = aos_button(s_remote.controls, LV_SYMBOL_NEXT, AOS_C_CARD2,
                                command_cb, (void *)(intptr_t)AOS_MEDIA_NEXT);
    lv_obj_set_size(next, 74, 60);
    lv_obj_align(next, LV_ALIGN_TOP_RIGHT, -30, 0);

    lv_obj_t *vol_down = aos_button(s_remote.controls, LV_SYMBOL_VOLUME_MID,
                                    AOS_C_CARD2, command_cb,
                                    (void *)(intptr_t)AOS_MEDIA_VOL_DOWN);
    lv_obj_set_size(vol_down, 110, 50);
    lv_obj_align(vol_down, LV_ALIGN_TOP_LEFT, 30, 72);

    lv_obj_t *vol_up = aos_button(s_remote.controls, LV_SYMBOL_VOLUME_MAX,
                                  AOS_C_CARD2, command_cb,
                                  (void *)(intptr_t)AOS_MEDIA_VOL_UP);
    lv_obj_set_size(vol_up, 110, 50);
    lv_obj_align(vol_up, LV_ALIGN_TOP_RIGHT, -30, 72);

    s_remote.hint = aos_label_boxed(page, "", aos_font_small, AOS_C_DIM,
                                    AOS_SCREEN_W, 20);
    lv_obj_align(s_remote.hint, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* The power button slots into the gap the two volume buttons leave: they
     * are 110 px with 30 of margin and leave 88 free in the middle. It used to
     * be right at the bottom (y 406..441) and never responded: this board's
     * touch panel reports nothing below 395. */
    lv_obj_t *toggle = aos_button(s_remote.controls, LV_SYMBOL_POWER,
                                  AOS_C_CARD2, toggle_cb, NULL);
    lv_obj_set_size(toggle, 84, 36);
    lv_obj_align(toggle, LV_ALIGN_TOP_MID, 0, 79);

    s_remote.timer = lv_timer_create(refresh, 400, NULL);
    refresh(NULL);
    return &s_remote;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_remote.timer) {
        lv_timer_delete(s_remote.timer);
        s_remote.timer = NULL;
    }
}

void aos_app_remote_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.remote",
            .name     = "Control BT",
            .icon     = LV_SYMBOL_BLUETOOTH,
            .color_a  = 0x0A84FF,
            .color_b  = 0x0B3E80,
            .order    = 46,
        },
        .create  = create,
        .destroy = destroy,
    };
}
