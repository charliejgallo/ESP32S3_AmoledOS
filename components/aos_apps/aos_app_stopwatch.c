/* AmoledOS - Stopwatch with laps. Keeps counting in the background. */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <stdio.h>
#include <string.h>

#define MAX_LAPS    32

typedef struct {
    lv_obj_t *display;
    lv_obj_t *laps_list;
    lv_obj_t *start_btn;
    lv_obj_t *start_label;
    lv_obj_t *lap_btn;
    lv_timer_t *timer;

    bool     running;
    uint64_t started_ms;        /* uptime at the start              */
    uint64_t accumulated_ms;    /* accumulated from previous legs   */
    uint32_t laps[MAX_LAPS];
    int      lap_count;
    uint64_t last_lap_ms;
} stopwatch_t;

static stopwatch_t s_sw;    /* state persists even if the UI closes */

static uint64_t elapsed_ms(void)
{
    uint64_t total = s_sw.accumulated_ms;
    if (s_sw.running) {
        total += aos_hal_uptime_ms() - s_sw.started_ms;
    }
    return total;
}

static void format_time(char *out, size_t len, uint64_t ms)
{
    unsigned centis  = (unsigned)((ms / 10) % 100);
    unsigned seconds = (unsigned)((ms / 1000) % 60);
    unsigned minutes = (unsigned)((ms / 60000) % 60);
    unsigned hours   = (unsigned)(ms / 3600000);

    if (hours > 0) {
        snprintf(out, len, "%u:%02u:%02u.%02u", hours, minutes, seconds, centis);
    } else {
        snprintf(out, len, "%02u:%02u.%02u", minutes, seconds, centis);
    }
}

static void refresh(void)
{
    if (!s_sw.display) {
        return;
    }
    char buf[24];
    format_time(buf, sizeof(buf), elapsed_ms());

    /* Only write if it changed: lv_label_set_text invalidates the object even
     * when the text is identical, and stopped this was redrawing 30 times a
     * second. */
    if (strcmp(lv_label_get_text(s_sw.display), buf) != 0) {
        lv_label_set_text(s_sw.display, buf);
    }
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh();
}

static void update_buttons(void)
{
    if (!s_sw.start_label) {
        return;
    }
    lv_label_set_text(s_sw.start_label, s_sw.running ? _("Parar") : _("Iniciar"));
    lv_obj_set_style_bg_color(s_sw.start_btn,
                              s_sw.running ? AOS_C_RED : AOS_C_GREEN, 0);
    lv_obj_t *lap_label = lv_obj_get_child(s_sw.lap_btn, 0);
    if (lap_label) {
        lv_label_set_text(lap_label, s_sw.running ? _("Vuelta") : _("Reset"));
    }
}

static void start_cb(lv_event_t *event)
{
    (void)event;
    if (s_sw.running) {
        s_sw.accumulated_ms = elapsed_ms();
        s_sw.running = false;
    } else {
        s_sw.started_ms = aos_hal_uptime_ms();
        s_sw.running = true;
    }
    aos_hal_beep(1200, 25);
    update_buttons();
}

static void lap_cb(lv_event_t *event)
{
    (void)event;
    if (!s_sw.running) {
        /* reset */
        s_sw.accumulated_ms = 0;
        s_sw.lap_count = 0;
        s_sw.last_lap_ms = 0;
        if (s_sw.laps_list) {
            lv_obj_clean(s_sw.laps_list);
        }
        refresh();
        return;
    }

    if (s_sw.lap_count >= MAX_LAPS) {
        aos_ui_toast(_("Maximo de vueltas"), 1200);
        return;
    }

    uint64_t now = elapsed_ms();
    uint32_t split = (uint32_t)(now - s_sw.last_lap_ms);
    s_sw.last_lap_ms = now;
    s_sw.laps[s_sw.lap_count++] = split;

    if (s_sw.laps_list) {
        char buf[40];
        char time_buf[24];
        format_time(time_buf, sizeof(time_buf), split);
        snprintf(buf, sizeof(buf), "%2d      %s", s_sw.lap_count, time_buf);

        lv_obj_t *row = aos_label(s_sw.laps_list, buf, aos_font_body, AOS_C_DIM);
        lv_obj_move_to_index(row, 0);
    }
    aos_hal_beep(1600, 20);
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);

    /* Deliberately no transform_scale: a scaled label is a LAYER for LVGL
     * (separate buffer + memset + render + compositing), and this text is
     * updated 30 times a second. Measured on the board: with the layer, the
     * app did not even manage to open. With the font at its natural size, it
     * flies. */
    s_sw.display = aos_label(page, "00:00.00", aos_font_huge, AOS_C_TEXT);
    lv_obj_align(s_sw.display, LV_ALIGN_TOP_MID, 0, 46);

    s_sw.start_btn = aos_button(page, _("Iniciar"), AOS_C_GREEN, start_cb, NULL);
    lv_obj_set_size(s_sw.start_btn, 132, 60);
    lv_obj_align(s_sw.start_btn, LV_ALIGN_TOP_LEFT, 28, 120);
    s_sw.start_label = lv_obj_get_child(s_sw.start_btn, 0);

    s_sw.lap_btn = aos_button(page, _("Reset"), AOS_C_CARD2, lap_cb, NULL);
    lv_obj_set_size(s_sw.lap_btn, 132, 60);
    lv_obj_align(s_sw.lap_btn, LV_ALIGN_TOP_RIGHT, -28, 120);

    s_sw.laps_list = lv_obj_create(page);
    lv_obj_remove_style_all(s_sw.laps_list);
    lv_obj_set_size(s_sw.laps_list, AOS_SCREEN_W - 56, 190);
    lv_obj_align(s_sw.laps_list, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_flex_flow(s_sw.laps_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_sw.laps_list, 10, 0);
    lv_obj_set_scroll_dir(s_sw.laps_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_sw.laps_list, LV_SCROLLBAR_MODE_OFF);

    for (int i = s_sw.lap_count - 1; i >= 0; i--) {
        char buf[40];
        char time_buf[24];
        format_time(time_buf, sizeof(time_buf), s_sw.laps[i]);
        snprintf(buf, sizeof(buf), "%2d      %s", i + 1, time_buf);
        aos_label(s_sw.laps_list, buf, aos_font_body, AOS_C_DIM);
    }

    s_sw.timer = lv_timer_create(tick_cb, 33, NULL);
    update_buttons();
    refresh();
    return &s_sw;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_sw.timer) {
        lv_timer_delete(s_sw.timer);
        s_sw.timer = NULL;
    }
    s_sw.display = NULL;
    s_sw.laps_list = NULL;
    s_sw.start_btn = NULL;
    s_sw.start_label = NULL;
    s_sw.lap_btn = NULL;
}

static void hide(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_sw.timer) {
        lv_timer_pause(s_sw.timer);
    }
}

static void show(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_sw.timer) {
        lv_timer_resume(s_sw.timer);
    }
    refresh();
}

void aos_app_stopwatch_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.stopwatch",
            .name     = "Cronometro",
            .icon_vec = AOS_ICON_STOPWATCH,
            .color_a  = 0x2C2C2E,
            .color_b  = 0x1C1C1E,
            .flags    = AOS_APP_FLAG_BACKGROUND,
            .order    = 20,
        },
        .create  = create,
        .destroy = destroy,
        .show    = show,
        .hide    = hide,
    };
}
