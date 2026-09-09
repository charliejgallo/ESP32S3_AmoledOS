/* AmoledOS - Power: state of the AXP2101 and of the memory. */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"

#include <stdio.h>

typedef struct {
    lv_obj_t   *arc;
    lv_obj_t   *percent;
    lv_obj_t   *state;
    lv_obj_t   *details;
    lv_obj_t   *memory;
    lv_timer_t *timer;
} power_t;

static power_t s_power;

static void refresh(lv_timer_t *timer)
{
    (void)timer;

    aos_battery_t batt;
    char buf[160];

    if (aos_hal_battery_read(&batt)) {
        lv_arc_set_value(s_power.arc, batt.percent < 0 ? 0 : batt.percent);
        snprintf(buf, sizeof(buf), "%d%%", batt.percent);
        lv_label_set_text(s_power.percent, buf);

        lv_color_t color = batt.charging ? AOS_C_GREEN :
                           (batt.percent <= 15 ? AOS_C_RED : AOS_C_TEAL);
        lv_obj_set_style_arc_color(s_power.arc, color, LV_PART_INDICATOR);

        /* The icon goes as an argument: glued to the literal, the key aos_tr()
         * looks for carries its bytes in front and the catalogue does not have
         * it. */
        char state[48];
        if (batt.charging) {
            snprintf(state, sizeof(state), LV_SYMBOL_CHARGE " %s", _("cargando"));
        } else if (batt.usb_present) {
            snprintf(state, sizeof(state), LV_SYMBOL_USB " %s", _("conectado"));
        } else {
            snprintf(state, sizeof(state), "%s", _("a bateria"));
        }
        lv_label_set_text(s_power.state, state);

        snprintf(buf, sizeof(buf), "%.3f V\n%+.0f mA\n%.1f C",
                 batt.voltage, batt.current, batt.temperature);
        lv_label_set_text(s_power.details, buf);
    }

    uint32_t internal = 0, psram = 0;
    aos_hal_heap_info(&internal, &psram);
    snprintf(buf, sizeof(buf), _("RAM  %u KB    PSRAM  %u KB\n%s\nuptime %llu s"),
             (unsigned)(internal / 1024), (unsigned)(psram / 1024),
             aos_hal_board_name(),
             (unsigned long long)(aos_hal_uptime_ms() / 1000));
    lv_label_set_text(s_power.memory, buf);
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);

    s_power.arc = lv_arc_create(page);
    lv_obj_remove_style(s_power.arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_power.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_power.arc, 180, 180);
    lv_arc_set_rotation(s_power.arc, 270);
    lv_arc_set_bg_angles(s_power.arc, 0, 360);
    lv_obj_set_style_arc_width(s_power.arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_power.arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_power.arc, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_align(s_power.arc, LV_ALIGN_TOP_MID, 0, 16);

    s_power.percent = aos_label_boxed(page, "--%", aos_font_huge, AOS_C_TEXT,
                                      170, 60);
    lv_obj_align_to(s_power.percent, s_power.arc, LV_ALIGN_CENTER, 0, 0);

    s_power.state = aos_label(page, "", aos_font_body, AOS_C_DIM);
    lv_obj_align(s_power.state, LV_ALIGN_TOP_MID, 0, 208);

    s_power.details = aos_label(page, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_style_text_align(s_power.details, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_power.details, LV_ALIGN_TOP_MID, 0, 244);

    s_power.memory = aos_label(page, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_style_text_align(s_power.memory, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_power.memory, LV_ALIGN_BOTTOM_MID, 0, -16);

    s_power.timer = lv_timer_create(refresh, 1000, NULL);
    refresh(NULL);
    return &s_power;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_power.timer) {
        lv_timer_delete(s_power.timer);
        s_power.timer = NULL;
    }
}

void aos_app_power_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.power",
            .name     = "Bateria",
            .icon     = LV_SYMBOL_BATTERY_FULL,
            .color_a  = 0x30D158,
            .color_b  = 0x1E8E3E,
            .order    = 70,
        },
        .create  = create,
        .destroy = destroy,
    };
}
