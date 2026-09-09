/*
 * AmoledOS - Calculator
 *
 * Inspired by Dieter Rams and Dietrich Lubs's Braun ET66 (1987): black body,
 * round keys, numbers in dark grey, operators a shade lighter, and the equals
 * in green, which is the detail that makes it recognisable. Thin typography
 * and a right-aligned display.
 *
 * Four operations, percentage, sign change and decimal comma. No history and
 * no memory: the ET66 did not have those on show either.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ---- Geometry --------------------------------------------------------------
 * Sized for 368x448 with the status bar on top: 4 columns of 62 px with 14 of
 * spacing gives 290, centred that leaves 39 of margin on each side. Five rows
 * of 62 with 7 of spacing is 338, and with the display they fit exactly. */
#define KEY_D           62
#define KEY_GAP_X       14
#define KEY_GAP_Y       7
#define GRID_W          (4 * KEY_D + 3 * KEY_GAP_X)
#define GRID_X          ((AOS_SCREEN_W - GRID_W) / 2)
/* Without the status bar (AOS_APP_FLAG_FULLSCREEN) the grid starts at the top
 * and ends at 82 + 338 = 420, with 28 px of air below. That margin is needed:
 * the watch's rounded bezel eats the last few pixels and with the bar in place
 * the zero row came out clipped. */
#define GRID_Y          82
#define DISPLAY_Y       18

/* The ET66's palette: the body is black and the keys are told apart by value,
 * not by colour. The only colour is the equals. */
/* First version: numbers dark grey, operators light grey, equals green. On the
 * board they all looked the same -the contrast between two neighbouring greys
 * is lost-, so now the hierarchy goes by colour and not by value: the numbers
 * in grey, the whole function column in brown and the equals in amber. */
#define C_NUM           lv_color_hex(0x3A3A3C)
#define C_OP            lv_color_hex(0x7A5230)
#define C_EQ            lv_color_hex(0xE0A21A)

#define ENTRY_MAX       16

typedef struct {
    lv_obj_t *display;

    char   entry[ENTRY_MAX + 1];    /* what is being typed                 */
    bool   typing;                  /* false = the display shows acc       */
    double acc;                     /* accumulator of the pending operation */
    char   pending;                 /* 0, '+', '-', '*', '/'               */
    bool   error;
} calc_t;

static calc_t s_calc;

/* -------------------------------------------------------------------------- */

/* %.10g and not %.2f: a calculator has to be able to show 1/3 and 1e12 alike
 * without inventing zeros or eating digits. */
static void format_number(double v, char *out, size_t len)
{
    if (!isfinite(v)) {
        snprintf(out, len, "%s", _("Error"));
        return;
    }
    snprintf(out, len, "%.10g", v);

    /* The Spanish decimal comma. Done at the end, on the already formatted
     * text, so as not to fight with printf's locale. */
    for (char *p = out; *p; p++) {
        if (*p == '.') {
            *p = ',';
        }
    }
}

static double entry_value(void)
{
    char buf[ENTRY_MAX + 1];
    snprintf(buf, sizeof(buf), "%s", s_calc.entry);
    for (char *p = buf; *p; p++) {
        if (*p == ',') {
            *p = '.';           /* strtod expects a full stop */
        }
    }
    return strtod(buf, NULL);
}

static double current_value(void)
{
    return s_calc.typing ? entry_value() : s_calc.acc;
}

static void refresh(void)
{
    if (!s_calc.display) {
        return;
    }
    char buf[32];
    if (s_calc.error) {
        snprintf(buf, sizeof(buf), "%s", _("Error"));
    } else if (s_calc.typing) {
        snprintf(buf, sizeof(buf), "%s", s_calc.entry);
    } else {
        format_number(s_calc.acc, buf, sizeof(buf));
    }
    lv_label_set_text(s_calc.display, buf);
}

static void reset_all(void)
{
    memset(&s_calc.entry, 0, sizeof(s_calc.entry));
    s_calc.typing  = false;
    s_calc.acc     = 0.0;
    s_calc.pending = 0;
    s_calc.error   = false;
}

static void apply_pending(double rhs)
{
    switch (s_calc.pending) {
    case '+': s_calc.acc += rhs; break;
    case '-': s_calc.acc -= rhs; break;
    case '*': s_calc.acc *= rhs; break;
    case '/':
        if (rhs == 0.0) {
            s_calc.error = true;
            return;
        }
        s_calc.acc /= rhs;
        break;
    default:  s_calc.acc = rhs;  break;
    }
    if (!isfinite(s_calc.acc)) {
        s_calc.error = true;
    }
}

static void push_digit(char c)
{
    if (s_calc.error) {
        reset_all();
    }
    if (!s_calc.typing) {
        s_calc.entry[0] = 0;
        s_calc.typing = true;
    }
    size_t len = strlen(s_calc.entry);

    if (c == ',') {
        if (strchr(s_calc.entry, ',')) {
            return;                     /* it already has a comma */
        }
        if (len == 0) {                 /* ",5" is written "0,5" */
            s_calc.entry[len++] = '0';
            s_calc.entry[len]   = 0;
        }
    } else if (len == 1 && s_calc.entry[0] == '0') {
        s_calc.entry[0] = c;            /* we do not leave "07" */
        return;
    }

    if (len >= ENTRY_MAX) {
        return;
    }
    s_calc.entry[len]     = c;
    s_calc.entry[len + 1] = 0;
}

static void key_cb(lv_event_t *event)
{
    char key = (char)(intptr_t)lv_event_get_user_data(event);
    aos_hal_activity();

    if (key >= '0' && key <= '9') {
        push_digit(key);
    } else if (key == ',') {
        push_digit(',');
    } else if (key == 'C') {
        reset_all();
    } else if (key == '~') {                    /* sign change */
        if (s_calc.typing) {
            if (s_calc.entry[0] == '-') {
                memmove(s_calc.entry, s_calc.entry + 1, strlen(s_calc.entry));
            } else if (strlen(s_calc.entry) < ENTRY_MAX) {
                memmove(s_calc.entry + 1, s_calc.entry, strlen(s_calc.entry) + 1);
                s_calc.entry[0] = '-';
            }
        } else {
            s_calc.acc = -s_calc.acc;
        }
    } else if (key == '%') {
        double v = current_value() / 100.0;
        s_calc.acc    = v;
        s_calc.typing = false;
    } else if (key == '=') {
        if (!s_calc.error) {
            apply_pending(current_value());
            s_calc.pending = 0;
            s_calc.typing  = false;
        }
    } else {                                    /* + - * / */
        if (!s_calc.error) {
            /* It chains: 2 + 3 + shows 5 before going on. */
            apply_pending(s_calc.typing ? entry_value() : s_calc.acc);
            s_calc.pending = key;
            s_calc.typing  = false;
        }
    }

    refresh();
}

/* -------------------------------------------------------------------------- */

static lv_obj_t *key(lv_obj_t *parent, const char *text, char code,
                     lv_color_t color, int col, int row, int span)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);

    int32_t w = span * KEY_D + (span - 1) * KEY_GAP_X;
    lv_obj_set_size(btn, w, KEY_D);
    lv_obj_set_pos(btn, GRID_X + col * (KEY_D + KEY_GAP_X),
                        GRID_Y + row * (KEY_D + KEY_GAP_Y));

    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

    /* The pressed look on touch goes through opacity and not through a
     * transform: a transform is a LAYER for LVGL, and that has already hung
     * the board once. */
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, LV_STATE_PRESSED);

    lv_obj_t *label = aos_label(btn, text, aos_font_title, AOS_C_TEXT);
    lv_obj_center(label);

    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    return btn;
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    s_calc.display = aos_label(page, "0", aos_font_huge, AOS_C_TEXT);
    lv_obj_set_width(s_calc.display, AOS_SCREEN_W - 2 * GRID_X);
    lv_obj_set_style_text_align(s_calc.display, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_calc.display, GRID_X, DISPLAY_Y);

    /* row 0 */
    key(page, "C",  'C', C_OP,  0, 0, 1);
    key(page, "+/-",'~', C_OP,  1, 0, 1);
    key(page, "%",  '%', C_OP,  2, 0, 1);
    key(page, "/",  '/', C_OP,  3, 0, 1);
    /* digit rows */
    key(page, "7", '7', C_NUM, 0, 1, 1);
    key(page, "8", '8', C_NUM, 1, 1, 1);
    key(page, "9", '9', C_NUM, 2, 1, 1);
    key(page, "x", '*', C_OP,  3, 1, 1);

    key(page, "4", '4', C_NUM, 0, 2, 1);
    key(page, "5", '5', C_NUM, 1, 2, 1);
    key(page, "6", '6', C_NUM, 2, 2, 1);
    key(page, "-", '-', C_OP,  3, 2, 1);

    key(page, "1", '1', C_NUM, 0, 3, 1);
    key(page, "2", '2', C_NUM, 1, 3, 1);
    key(page, "3", '3', C_NUM, 2, 3, 1);
    key(page, "+", '+', C_OP,  3, 3, 1);

    key(page, "0", '0', C_NUM, 0, 4, 2);    /* the zero takes up two columns */
    key(page, ",", ',', C_NUM, 2, 4, 1);
    key(page, "=", '=', C_EQ,  3, 4, 1);

    reset_all();
    refresh();
    return &s_calc;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    s_calc.display = NULL;      /* the numeric state is kept on purpose */
}

void aos_app_calc_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.calc",
            .name     = "Calculadora",
            .icon_vec = AOS_ICON_CALC,
            .color_a  = 0xE0A21A,       /* the equals amber */
            .color_b  = 0x7A5230,       /* and the functions brown */
            .flags    = AOS_APP_FLAG_FULLSCREEN,
            .order    = 55,
        },
        .create  = create,
        .destroy = destroy,
    };
}
