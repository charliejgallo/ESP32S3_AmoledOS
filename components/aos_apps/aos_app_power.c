/* AmoledOS - Power: the battery, the charger and what the firmware is doing
 * to stretch both. Everything on this screen is either read from the AXP2101
 * or kept by the HAL; nothing is estimated here. */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"

#include <math.h>
#include <stdio.h>

typedef struct {
    lv_obj_t   *arc;
    lv_obj_t   *percent;
    lv_obj_t   *state;
    lv_obj_t   *details;
    lv_obj_t   *rows;
    lv_obj_t   *memory;
    lv_timer_t *timer;
} power_t;

static power_t s_power;

static const char *charge_state_text(aos_charge_state_t state)
{
    switch (state) {
    case AOS_CHG_TRICKLE:   return _("goteo");
    case AOS_CHG_PRECHARGE: return _("precarga");
    case AOS_CHG_CC:        return _("corriente constante");
    case AOS_CHG_CV:        return _("tension constante");
    case AOS_CHG_DONE:      return _("carga completa");
    default:                return _("en espera");
    }
}

/* "3h 20m" or "45m" */
static void duration(char *out, size_t len, uint32_t seconds)
{
    uint32_t minutes = seconds / 60;
    if (minutes >= 60) {
        snprintf(out, len, "%uh %02um", (unsigned)(minutes / 60), (unsigned)(minutes % 60));
    } else {
        snprintf(out, len, "%um", (unsigned)minutes);
    }
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;

    aos_battery_t    batt;
    aos_power_info_t pw;
    char buf[320];

    bool have_batt = aos_hal_battery_read(&batt);
    bool have_pw   = aos_hal_power_info(&pw);

    if (have_batt) {
        lv_arc_set_value(s_power.arc, batt.percent < 0 ? 0 : batt.percent);
        snprintf(buf, sizeof(buf), "%d%%", batt.percent);
        lv_label_set_text(s_power.percent, buf);

        lv_color_t color = batt.charging ? AOS_C_GREEN :
                           (batt.percent <= 15 ? AOS_C_RED : AOS_C_TEAL);
        lv_obj_set_style_arc_color(s_power.arc, color, LV_PART_INDICATOR);

        /* The icon goes as an argument: glued to the literal, the key aos_tr()
         * looks for carries its bytes in front and the catalogue does not have
         * it. */
        char state[96];
        if (batt.charging) {
            snprintf(state, sizeof(state), LV_SYMBOL_CHARGE " %s  ·  %s", _("cargando"),
                     have_pw ? charge_state_text(pw.charge_state) : "");
        } else if (batt.usb_present) {
            snprintf(state, sizeof(state), LV_SYMBOL_USB " %s  ·  %s", _("conectado"),
                     have_pw ? charge_state_text(pw.charge_state) : "");
        } else if (have_pw && pw.on_battery_s > 0) {
            char since[16];
            duration(since, sizeof(since), pw.on_battery_s);
            snprintf(state, sizeof(state), "%s  ·  %s", _("a bateria"), since);
        } else {
            snprintf(state, sizeof(state), "%s", _("a bateria"));
        }
        lv_label_set_text(s_power.state, state);

        /* Voltage and the two temperatures the PMU has: its own die and the
         * NTC on the board next to it. No current: the chip cannot measure it. */
        if (have_pw && !isnan(pw.board_temperature)) {
            snprintf(buf, sizeof(buf), "%.3f V   %.1f C  ·  %s %.1f C",
                     batt.voltage, batt.temperature, _("placa"), pw.board_temperature);
        } else {
            snprintf(buf, sizeof(buf), "%.3f V   %.1f C", batt.voltage, batt.temperature);
        }
        lv_label_set_text(s_power.details, buf);
    }

    if (have_pw) {
        char drain[48], left[24], total[16];
        if (isnan(pw.drain_pct_per_hour)) {
            snprintf(drain, sizeof(drain), "%s", _("midiendo..."));
        } else {
            duration(left, sizeof(left), (uint32_t)(pw.hours_left * 3600.0f));
            snprintf(drain, sizeof(drain), "%.1f %%/h  ·  ~%s", pw.drain_pct_per_hour, left);
        }
        duration(total, sizeof(total), pw.battery_minutes_total * 60);

        snprintf(buf, sizeof(buf),
                 "%s   %s\n"
                 "%s   %d mA  ->  %.2f V\n"
                 "%s   %u\n"
                 "%s   %s\n"
                 "%s   %d MHz%s%s",
                 _("Consumo"), drain,
                 _("Cargador"), pw.charge_ma, pw.charge_target_mv / 1000.0f,
                 _("Ciclos"), (unsigned)pw.charge_cycles,
                 _("Uso a bateria"), total,
                 _("CPU"), pw.cpu_mhz,
                 pw.power_saving_active ? "  ·  " : "",
                 pw.power_saving_active ? _("ahorro") : "");
        lv_label_set_text(s_power.rows, buf);
    } else {
        lv_label_set_text(s_power.rows, _("sin PMU"));
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
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(page, 10, 0);
    lv_obj_set_style_pad_ver(page, 14, 0);

    /* The arc and its number are one block, so the flex column can place
     * them together. */
    lv_obj_t *gauge = lv_obj_create(page);
    lv_obj_remove_style_all(gauge);
    lv_obj_set_size(gauge, 160, 160);
    lv_obj_remove_flag(gauge, LV_OBJ_FLAG_SCROLLABLE);

    s_power.arc = lv_arc_create(gauge);
    lv_obj_remove_style(s_power.arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_power.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_power.arc, 160, 160);
    lv_arc_set_rotation(s_power.arc, 270);
    lv_arc_set_bg_angles(s_power.arc, 0, 360);
    lv_obj_set_style_arc_width(s_power.arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_power.arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_power.arc, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_center(s_power.arc);

    s_power.percent = aos_label_boxed(gauge, "--%", aos_font_huge, AOS_C_TEXT,
                                      150, 60);
    lv_obj_center(s_power.percent);

    s_power.state = aos_label(page, "", aos_font_body, AOS_C_DIM);
    lv_obj_set_width(s_power.state, AOS_SCREEN_W - 60);
    lv_obj_set_style_text_align(s_power.state, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_power.state, LV_LABEL_LONG_MODE_WRAP);

    s_power.details = aos_label(page, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(s_power.details, AOS_SCREEN_W - 60);
    lv_obj_set_style_text_align(s_power.details, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_power.details, LV_LABEL_LONG_MODE_WRAP);

    s_power.rows = aos_label(page, "", aos_font_small, AOS_C_TEXT);
    lv_obj_set_width(s_power.rows, AOS_SCREEN_W - 70);
    lv_obj_set_style_text_align(s_power.rows, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(s_power.rows, 6, 0);
    lv_label_set_long_mode(s_power.rows, LV_LABEL_LONG_MODE_WRAP);

    s_power.memory = aos_label(page, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_width(s_power.memory, AOS_SCREEN_W - 60);
    lv_obj_set_style_text_align(s_power.memory, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_power.memory, LV_LABEL_LONG_MODE_WRAP);

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
