/*
 * AmoledOS - Alarms.
 *
 * Stored in preferences as integers: minute_of_day | (enabled << 16). The
 * checking lives in aos_alarm_service_tick(), which always runs from the main
 * loop, whether the app is open or not.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <stdio.h>
#include <string.h>

#define MAX_ALARMS      6
#define KEY_FMT         "alarm%d"

typedef struct {
    int  minute_of_day;     /* -1 = empty */
    bool enabled;
} alarm_slot_t;

static alarm_slot_t s_alarms[MAX_ALARMS];
static bool         s_loaded;
static volatile bool s_reload;      /* the portal changed something */
static void rebuild_list(void);
static int          s_last_fired_minute = -1;
static int          s_ringing_left;

static lv_obj_t *s_list;
static lv_obj_t *s_editor;
static lv_obj_t *s_roller_h;
static lv_obj_t *s_roller_m;
static int       s_editing = -1;

static void alarms_load(void)
{
    if (s_loaded) {
        return;
    }
    for (int i = 0; i < MAX_ALARMS; i++) {
        char key[16];
        snprintf(key, sizeof(key), KEY_FMT, i);
        int32_t value = 0;
        if (aos_hal_pref_get_i32(key, &value) && (value & 0xFFFF) != 0xFFFF) {
            s_alarms[i].minute_of_day = value & 0xFFFF;
            s_alarms[i].enabled       = (value >> 16) != 0;
        } else {
            s_alarms[i].minute_of_day = -1;
            s_alarms[i].enabled       = false;
        }
    }
    s_loaded = true;
}

static void alarm_save(int index)
{
    char key[16];
    snprintf(key, sizeof(key), KEY_FMT, index);
    int32_t value = s_alarms[index].minute_of_day < 0
                  ? 0xFFFF
                  : (s_alarms[index].minute_of_day | (s_alarms[index].enabled ? 1 << 16 : 0));
    aos_hal_pref_set_i32(key, value);
}

/* -------------------------------------------------------------------------- */

bool aos_alarm_get(int index, int *minute_of_day, bool *enabled)
{
    if (index < 0 || index >= MAX_ALARMS) {
        return false;
    }
    char key[16];
    snprintf(key, sizeof(key), KEY_FMT, index);
    int32_t value = 0;
    bool have = aos_hal_pref_get_i32(key, &value) && (value & 0xFFFF) != 0xFFFF;
    if (minute_of_day) *minute_of_day = have ? (int)(value & 0xFFFF) : -1;
    if (enabled)       *enabled       = have && (value >> 16) != 0;
    return true;
}

bool aos_alarm_set(int index, int minute_of_day, bool enabled)
{
    if (index < 0 || index >= MAX_ALARMS || minute_of_day >= 24 * 60) {
        return false;
    }
    char key[16];
    snprintf(key, sizeof(key), KEY_FMT, index);
    int32_t value = minute_of_day < 0 ? 0xFFFF
                  : (minute_of_day | (enabled ? 1 << 16 : 0));
    if (!aos_hal_pref_set_i32(key, value)) {
        return false;
    }
    s_reload = true;
    return true;
}

void aos_alarm_service_tick(void)
{
    if (s_reload) {
        /* Written from the server task; applied here, with the lock held,
         * which is also where the list can be redrawn if the app is open. */
        s_reload = false;
        s_loaded = false;
        alarms_load();
        rebuild_list();
    }
    alarms_load();

    if (s_ringing_left > 0) {
        aos_hal_beep(2200, 150);
        s_ringing_left--;
        return;
    }

    struct tm now;
    aos_hal_time_now(&now);
    int minute_of_day = now.tm_hour * 60 + now.tm_min;
    if (minute_of_day == s_last_fired_minute) {
        return;
    }

    for (int i = 0; i < MAX_ALARMS; i++) {
        if (s_alarms[i].enabled && s_alarms[i].minute_of_day == minute_of_day) {
            s_last_fired_minute = minute_of_day;
            s_ringing_left = 20;
            char buf[48];
            snprintf(buf, sizeof(buf), LV_SYMBOL_BELL "  %s  %02d:%02d",
                     _("Alarma"), minute_of_day / 60, minute_of_day % 60);
            aos_ui_toast(buf, 8000);
            aos_hal_display_on(true);
            aos_hal_activity();
            return;
        }
    }
}

/* -------------------------------------------------------------------------- */

static void rebuild_list(void);

static void toggle_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    s_alarms[index].enabled = lv_obj_has_state(lv_event_get_target(event),
                                               LV_STATE_CHECKED);
    alarm_save(index);
}

static void delete_cb(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    s_alarms[index].minute_of_day = -1;
    s_alarms[index].enabled = false;
    alarm_save(index);
    rebuild_list();
}

static void editor_show(bool visible)
{
    if (visible) {
        lv_obj_remove_flag(s_editor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_editor);
    } else {
        lv_obj_add_flag(s_editor, LV_OBJ_FLAG_HIDDEN);
    }
}

static void add_cb(lv_event_t *event)
{
    (void)event;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (s_alarms[i].minute_of_day < 0) {
            s_editing = i;
            struct tm now;
            aos_hal_time_now(&now);
            lv_roller_set_selected(s_roller_h, (uint32_t)now.tm_hour, LV_ANIM_OFF);
            lv_roller_set_selected(s_roller_m, 0, LV_ANIM_OFF);
            editor_show(true);
            return;
        }
    }
    aos_ui_toast(_("No hay lugar para mas alarmas"), 1600);
}

static void save_cb(lv_event_t *event)
{
    (void)event;
    if (s_editing >= 0) {
        int hour   = (int)lv_roller_get_selected(s_roller_h);
        int minute = (int)lv_roller_get_selected(s_roller_m);
        s_alarms[s_editing].minute_of_day = hour * 60 + minute;
        s_alarms[s_editing].enabled = true;
        alarm_save(s_editing);
        s_editing = -1;
    }
    editor_show(false);
    rebuild_list();
}

static void cancel_cb(lv_event_t *event)
{
    (void)event;
    s_editing = -1;
    editor_show(false);
}

static void rebuild_list(void)
{
    if (!s_list) {
        return;
    }
    lv_obj_clean(s_list);

    int shown = 0;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (s_alarms[i].minute_of_day < 0) {
            continue;
        }
        shown++;

        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, AOS_SCREEN_W - 60, 62);
        lv_obj_set_style_bg_color(row, AOS_C_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        char buf[24];
        snprintf(buf, sizeof(buf), "%02d:%02d",
                 s_alarms[i].minute_of_day / 60, s_alarms[i].minute_of_day % 60);
        lv_obj_t *label = aos_label(row, buf, aos_font_title, AOS_C_TEXT);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 18, 0);

        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_set_size(sw, 56, 30);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -16, 0);
        lv_obj_set_style_bg_color(sw, AOS_C_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (s_alarms[i].enabled) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(sw, toggle_cb, LV_EVENT_VALUE_CHANGED,
                            (void *)(intptr_t)i);

        lv_obj_t *del = aos_label(row, LV_SYMBOL_TRASH, aos_font_body, AOS_C_DIM);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, -86, 0);
        lv_obj_add_flag(del, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(del, delete_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    if (shown == 0) {
        lv_obj_t *empty = aos_label(s_list, _("Sin alarmas"), aos_font_body, AOS_C_DIM);
        lv_obj_set_style_pad_top(empty, 40, 0);
    }
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    alarms_load();

    lv_obj_t *page = aos_page(root);

    s_list = lv_obj_create(page);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, AOS_SCREEN_W, AOS_SCREEN_H - 150);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_list, 10, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    char add_txt[32];
    snprintf(add_txt, sizeof(add_txt), LV_SYMBOL_PLUS "  %s", _("Nueva"));
    lv_obj_t *add = aos_button(page, add_txt, AOS_C_ACCENT,
                               add_cb, NULL);
    lv_obj_set_size(add, 190, 60);
    lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* --- editor --- */
    s_editor = lv_obj_create(page);
    lv_obj_remove_style_all(s_editor);
    lv_obj_set_size(s_editor, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_editor, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(s_editor, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_editor, LV_OBJ_FLAG_HIDDEN);

    static char hours_opts[24 * 3 + 1];
    static char mins_opts[60 * 3 + 1];
    if (hours_opts[0] == '\0') {
        /* "00\n01\n...": built by hand so as not to fight snprintf's
         * truncation analysis over small buffers */
        char *p = hours_opts;
        for (int i = 0; i < 24; i++) {
            *p++ = (char)('0' + i / 10);
            *p++ = (char)('0' + i % 10);
            if (i != 23) {
                *p++ = '\n';
            }
        }
        *p = '\0';

        p = mins_opts;
        for (int i = 0; i < 60; i++) {
            *p++ = (char)('0' + i / 10);
            *p++ = (char)('0' + i % 10);
            if (i != 59) {
                *p++ = '\n';
            }
        }
        *p = '\0';
    }

    s_roller_h = lv_roller_create(s_editor);
    lv_roller_set_options(s_roller_h, hours_opts, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_h, 3);
    lv_obj_set_style_text_font(s_roller_h, aos_font_title, 0);
    lv_obj_set_style_bg_color(s_roller_h, AOS_C_CARD, 0);
    lv_obj_set_style_bg_color(s_roller_h, AOS_C_CARD2, LV_PART_SELECTED);
    lv_obj_align(s_roller_h, LV_ALIGN_TOP_MID, -70, 60);

    s_roller_m = lv_roller_create(s_editor);
    lv_roller_set_options(s_roller_m, mins_opts, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_m, 3);
    lv_obj_set_style_text_font(s_roller_m, aos_font_title, 0);
    lv_obj_set_style_bg_color(s_roller_m, AOS_C_CARD, 0);
    lv_obj_set_style_bg_color(s_roller_m, AOS_C_CARD2, LV_PART_SELECTED);
    lv_obj_align(s_roller_m, LV_ALIGN_TOP_MID, 70, 60);

    lv_obj_t *save = aos_button(s_editor, _("Guardar"), AOS_C_GREEN, save_cb, NULL);
    lv_obj_set_size(save, 140, 60);
    lv_obj_align(save, LV_ALIGN_BOTTOM_LEFT, 26, -24);

    lv_obj_t *cancel = aos_button(s_editor, _("Cancelar"), AOS_C_CARD2, cancel_cb, NULL);
    lv_obj_set_size(cancel, 140, 60);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_RIGHT, -26, -24);

    rebuild_list();
    return &s_alarms;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    s_list = NULL;
    s_editor = NULL;
    s_editing = -1;
}

static bool back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_editor && !lv_obj_has_flag(s_editor, LV_OBJ_FLAG_HIDDEN)) {
        editor_show(false);
        s_editing = -1;
        return true;
    }
    return false;
}

void aos_app_alarm_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.alarm",
            .name     = "Alarmas",
            .icon_vec = AOS_ICON_ALARM,
            .color_a  = 0xFF9F0A,
            .color_b  = 0xD2691E,
            .order    = 40,
        },
        .create  = create,
        .destroy = destroy,
        .back    = back,
    };
}
