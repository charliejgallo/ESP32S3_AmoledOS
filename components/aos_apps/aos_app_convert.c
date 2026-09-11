/*
 * AmoledOS - Unit converter
 *
 * Ten families, each with its base unit, and the conversion solved with a
 * straight line: base_value = v * factor + offset. The offset exists for a
 * single family -temperature, where Fahrenheit and Kelvin do not pass through
 * zero at the same time as Celsius- but having it in the table avoids the
 * special case in the code, which is where the mistake always creeps in.
 *
 * The screen is a sibling of the calculator's: round keypad, display on the
 * right, Spanish decimal comma.
 *
 * The units are picked from a full-screen list and not from a roller, which
 * was the original idea. An lv_roller decides its own height from the font and
 * from how many rows it wants to show: with three rows that is ~90 px, and two
 * of those plus the keypad do not fit in 448. Dropping it to one row leaves it
 * showing only the chosen option, that is, without the one advantage it had.
 * The list, besides, fits the whole name: "nudo" is understood and "kn" is
 * not.
 */
#include "aos_apps.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_i18n.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Geometry (no status bar: 368x448) -----------------------------------
 * Four columns of 54 with 18 of spacing is 270, centred that leaves 49 of
 * margin. Four rows of 54 with 6 end at 420, which is the same 28 px of air
 * the calculator left itself: the watch's rounded bezel eats the last few
 * pixels at the bottom. */
/* 2026-09-11: the panel reports nothing above y = 55 (AOS_TOUCH_Y_MIN), so
 * everything touchable starts at 56 and the rows above are for text. */
#define KEY_D       46
#define KEY_GAP_X   18
#define KEY_GAP_Y   4
#define GRID_W      (4 * KEY_D + 3 * KEY_GAP_X)
#define GRID_X      ((AOS_SCREEN_W - GRID_W) / 2)
#define GRID_Y      210
#define ROW_X       12
#define ROW_W       (AOS_SCREEN_W - 2 * ROW_X)
#define ROW_H       54
#define ROW_A_Y     92
#define ROW_B_Y     150
#define FAMILY_Y    56
#define CHIP_W      104

#define C_NUM       lv_color_hex(0x3A3A3C)
#define C_FN        lv_color_hex(0x2C4A63)
#define C_SWAP      lv_color_hex(0x0A84FF)

#define ENTRY_MAX   14
#define MAX_UNITS   9

typedef struct {
    const char *sym;        /* what goes on the roller: short */
    const char *full;       /* the whole name, below the number */
    double      factor;     /* base = v * factor + offset */
    double      offset;
} unit_t;

typedef struct {
    const char *name;
    int         count;
    unit_t      unit[MAX_UNITS];
} family_t;

/* The factors are exact where the imperial system defines them by definition
 * (the inch is 25.4 mm since 1959, the pound 0.45359237 kg) and not rounded to
 * four figures, which is where the annoying differences on converting back and
 * forth come from. */
/* The magnitude's name and each unit's long name are marked. The symbol (mm,
 * kB, m/s) is NOT: it is SI notation, the same in every language, and
 * translating it would only invite mistakes. */
static const family_t FAMILY[] = {
    { N_("Longitud"), 9, {
        { "mm",  N_("milimetro"),     0.001,        0 },
        { "cm",  N_("centimetro"),    0.01,         0 },
        { "m",   N_("metro"),         1.0,          0 },
        { "km",  N_("kilometro"),     1000.0,       0 },
        { "in",  N_("pulgada"),       0.0254,       0 },
        { "ft",  N_("pie"),           0.3048,       0 },
        { "yd",  N_("yarda"),         0.9144,       0 },
        { "mi",  N_("milla"),         1609.344,     0 },
        { "nmi", N_("milla naut."),   1852.0,       0 },
    } },
    { N_("Masa"), 7, {
        { "mg",  N_("miligramo"),     0.000001,     0 },
        { "g",   N_("gramo"),         0.001,        0 },
        { "kg",  N_("kilogramo"),     1.0,          0 },
        { "t",   N_("tonelada"),      1000.0,       0 },
        { "oz",  N_("onza"),          0.028349523125, 0 },
        { "lb",  N_("libra"),         0.45359237,   0 },
        { "st",  N_("stone"),         6.35029318,   0 },
    } },
    { N_("Temperatura"), 3, {
        { "C",   N_("Celsius"),       1.0,          0.0 },
        { "F",   N_("Fahrenheit"),    5.0 / 9.0,   -160.0 / 9.0 },
        { "K",   N_("Kelvin"),        1.0,         -273.15 },
    } },
    { N_("Volumen"), 9, {
        { "ml",  N_("mililitro"),     0.001,        0 },
        { "cl",  N_("centilitro"),    0.01,         0 },
        { "l",   N_("litro"),         1.0,          0 },
        { "m3",  N_("metro cubico"),  1000.0,       0 },
        { "floz",N_("onza liquida"),  0.0295735295625, 0 },
        { "cup", N_("taza"),          0.2365882365, 0 },
        { "pt",  N_("pinta"),         0.473176473,  0 },
        { "qt",  N_("cuarto"),        0.946352946,  0 },
        { "gal", N_("galon"),         3.785411784,  0 },
    } },
    { N_("Velocidad"), 5, {
        { "m/s", N_("metro/segundo"), 1.0,          0 },
        { "km/h",N_("kilometro/hora"),1.0 / 3.6,    0 },
        { "mph", N_("milla/hora"),    0.44704,      0 },
        { "ft/s",N_("pie/segundo"),   0.3048,       0 },
        { "kn",  N_("nudo"),          1852.0 / 3600.0, 0 },
    } },
    { N_("Area"), 9, {
        { "cm2", N_("centimetro2"),   0.0001,       0 },
        { "m2",  N_("metro2"),        1.0,          0 },
        { "ha",  N_("hectarea"),      10000.0,      0 },
        { "km2", N_("kilometro2"),    1000000.0,    0 },
        { "in2", N_("pulgada2"),      0.00064516,   0 },
        { "ft2", N_("pie2"),          0.09290304,   0 },
        { "yd2", N_("yarda2"),        0.83612736,   0 },
        { "ac",  N_("acre"),          4046.8564224, 0 },
        { "mi2", N_("milla2"),        2589988.110336, 0 },
    } },
    { N_("Presion"), 7, {
        { "Pa",  N_("pascal"),        1.0,          0 },
        { "hPa", N_("hectopascal"),   100.0,        0 },
        { "kPa", N_("kilopascal"),    1000.0,       0 },
        { "bar", N_("bar"),           100000.0,     0 },
        { "atm", N_("atmosfera"),     101325.0,     0 },
        { "psi", N_("psi"),           6894.757293168, 0 },
        { "mmHg",N_("mm de mercurio"),133.322387415, 0 },
    } },
    { N_("Datos"), 8, {
        { "B",   N_("byte"),          1.0,          0 },
        { "kB",  N_("kilobyte"),      1000.0,       0 },
        { "MB",  N_("megabyte"),      1000000.0,    0 },
        { "GB",  N_("gigabyte"),      1000000000.0, 0 },
        { "TB",  N_("terabyte"),      1000000000000.0, 0 },
        { "KiB", N_("kibibyte"),      1024.0,       0 },
        { "MiB", N_("mebibyte"),      1048576.0,    0 },
        { "GiB", N_("gibibyte"),      1073741824.0, 0 },
    } },
    { N_("Energia"), 7, {
        { "J",   N_("joule"),         1.0,          0 },
        { "kJ",  N_("kilojoule"),     1000.0,       0 },
        { "cal", N_("caloria"),       4.184,        0 },
        { "kcal",N_("kilocaloria"),   4184.0,       0 },
        { "Wh",  N_("watt-hora"),     3600.0,       0 },
        { "kWh", N_("kilowatt-hora"), 3600000.0,    0 },
        { "BTU", N_("BTU"),           1055.05585262, 0 },
    } },
    { N_("Tiempo"), 6, {
        { "ms",  N_("milisegundo"),   0.001,        0 },
        { "s",   N_("segundo"),       1.0,          0 },
        { "min", N_("minuto"),        60.0,         0 },
        { "h",   N_("hora"),          3600.0,       0 },
        { "d",   N_("dia"),           86400.0,      0 },
        { "sem", N_("semana"),        604800.0,     0 },
    } },
};
#define FAMILY_COUNT    ((int)(sizeof(FAMILY) / sizeof(FAMILY[0])))

typedef struct {
    lv_obj_t *page;
    lv_obj_t *btn_family;
    lv_obj_t *lbl_family;
    lv_obj_t *lbl_chip_a;
    lv_obj_t *lbl_chip_b;
    lv_obj_t *val_a;
    lv_obj_t *val_b;
    lv_obj_t *full_a;
    lv_obj_t *full_b;

    /* A single dropdown for all three lists: families, source unit and target
     * unit. Ten buttons are enough for all three (ten families, nine units at
     * most) and they are filled in when it is opened. */
    lv_obj_t *ov_view;
    lv_obj_t *ov_title;
    lv_obj_t *ov_btn[10];
    lv_obj_t *ov_lbl[10];
    int       ov_mode;          /* -1 closed, 0 family, 1 source, 2 target */

    int  family;
    int  from;
    int  to;
    char entry[ENTRY_MAX + 1];
    bool negative;
} conv_t;

#define OV_SLOTS    ((int)(sizeof(((conv_t *)0)->ov_btn) / sizeof(lv_obj_t *)))

static conv_t s_conv;

/* -------------------------------------------------------------------------- */
/* Numbers                                                                     */

/* %.9g: it has to be able to show 0,000393701 and 1073741824 alike without
 * inventing zeros or eating digits. */
static void format_number(double v, char *out, size_t len)
{
    if (!isfinite(v)) {
        snprintf(out, len, "---");
        return;
    }
    snprintf(out, len, "%.9g", v);
    for (char *p = out; *p; p++) {
        if (*p == '.') {
            *p = ',';           /* the Spanish decimal comma */
        }
    }
}

static double entry_value(void)
{
    if (s_conv.entry[0] == '\0') {
        return 0.0;
    }
    char buf[ENTRY_MAX + 2];
    snprintf(buf, sizeof(buf), "%s", s_conv.entry);
    for (char *p = buf; *p; p++) {
        if (*p == ',') {
            *p = '.';           /* strtod expects a full stop */
        }
    }
    double v = strtod(buf, NULL);
    return s_conv.negative ? -v : v;
}

static double convert(double v)
{
    const family_t *f = &FAMILY[s_conv.family];
    const unit_t *a = &f->unit[s_conv.from];
    const unit_t *b = &f->unit[s_conv.to];
    double base = v * a->factor + a->offset;
    if (b->factor == 0.0) {
        return NAN;
    }
    return (base - b->offset) / b->factor;
}

/* -------------------------------------------------------------------------- */
/* Preferences                                                                 */

static void save_prefs(void)
{
    aos_hal_pref_set_i32("conv_fam",  s_conv.family);
    aos_hal_pref_set_i32("conv_from", s_conv.from);
    aos_hal_pref_set_i32("conv_to",   s_conv.to);
}

static void load_prefs(void)
{
    int32_t v = 0;
    if (aos_hal_pref_get_i32("conv_fam", &v) && v >= 0 && v < FAMILY_COUNT) {
        s_conv.family = (int)v;
    }
    int n = FAMILY[s_conv.family].count;
    if (aos_hal_pref_get_i32("conv_from", &v) && v >= 0 && v < n) {
        s_conv.from = (int)v;
    }
    if (aos_hal_pref_get_i32("conv_to", &v) && v >= 0 && v < n) {
        s_conv.to = (int)v;
    }
    if (s_conv.to == s_conv.from) {
        s_conv.to = (s_conv.from + 1) % n;
    }
}

/* -------------------------------------------------------------------------- */
/* Drawing                                                                     */

static void refresh(void)
{
    if (!s_conv.val_a) {
        return;
    }
    const family_t *f = &FAMILY[s_conv.family];

    char buf[40];
    if (s_conv.entry[0] == '\0') {
        snprintf(buf, sizeof(buf), s_conv.negative ? "-0" : "0");
    } else {
        snprintf(buf, sizeof(buf), "%s%s", s_conv.negative ? "-" : "",
                 s_conv.entry);
    }
    lv_label_set_text(s_conv.val_a, buf);

    format_number(convert(entry_value()), buf, sizeof(buf));
    lv_label_set_text(s_conv.val_b, buf);

    lv_label_set_text(s_conv.lbl_chip_a, f->unit[s_conv.from].sym);
    lv_label_set_text(s_conv.lbl_chip_b, f->unit[s_conv.to].sym);
    lv_label_set_text(s_conv.full_a, _(f->unit[s_conv.from].full));
    lv_label_set_text(s_conv.full_b, _(f->unit[s_conv.to].full));
    lv_label_set_text(s_conv.lbl_family, _(f->name));
}

/* -------------------------------------------------------------------------- */
/* Dropdown                                                                    */

static void overlay_close(void)
{
    s_conv.ov_mode = -1;
    lv_obj_add_flag(s_conv.ov_view, LV_OBJ_FLAG_HIDDEN);
}

static void overlay_open(int mode)
{
    const family_t *f = &FAMILY[s_conv.family];
    int count = (mode == 0) ? FAMILY_COUNT : f->count;
    char buf[48];

    s_conv.ov_mode = mode;
    lv_label_set_text(s_conv.ov_title,
                      mode == 0 ? _("Magnitud") :
                      mode == 1 ? _("Convertir de") : _("Convertir a"));

    for (int i = 0; i < OV_SLOTS; i++) {
        if (i >= count) {
            lv_obj_add_flag(s_conv.ov_btn[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_conv.ov_btn[i], LV_OBJ_FLAG_HIDDEN);

        int current;
        if (mode == 0) {
            snprintf(buf, sizeof(buf), "%s", _(FAMILY[i].name));
            current = s_conv.family;
        } else {
            snprintf(buf, sizeof(buf), "%s  -  %s", f->unit[i].sym, _(f->unit[i].full));
            current = (mode == 1) ? s_conv.from : s_conv.to;
        }
        lv_label_set_text(s_conv.ov_lbl[i], buf);
        lv_obj_set_style_bg_color(s_conv.ov_btn[i],
                                  i == current ? AOS_C_ACCENT : AOS_C_CARD2, 0);
    }

    lv_obj_remove_flag(s_conv.ov_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_conv.ov_view);
    lv_obj_scroll_to_y(s_conv.ov_view, 0, LV_ANIM_OFF);
}

static void overlay_pick_cb(lv_event_t *event)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(event);

    if (s_conv.ov_mode == 0) {
        if (idx != s_conv.family) {
            s_conv.family   = idx;
            s_conv.from     = 0;
            s_conv.to       = 1;
            s_conv.entry[0] = '\0';
            s_conv.negative = false;
        }
    } else if (s_conv.ov_mode == 1) {
        s_conv.from = idx;
        if (s_conv.to == s_conv.from) {
            s_conv.to = (s_conv.from + 1) % FAMILY[s_conv.family].count;
        }
    } else if (s_conv.ov_mode == 2) {
        s_conv.to = idx;
        if (s_conv.to == s_conv.from) {
            s_conv.from = (s_conv.to + 1) % FAMILY[s_conv.family].count;
        }
    }

    save_prefs();
    overlay_close();
    refresh();
}

static void open_family_cb(lv_event_t *event) { (void)event; overlay_open(0); }
static void open_from_cb(lv_event_t *event)   { (void)event; overlay_open(1); }
static void open_to_cb(lv_event_t *event)     { (void)event; overlay_open(2); }

/* -------------------------------------------------------------------------- */
/* Keypad                                                                      */

static void key_cb(lv_event_t *event)
{
    char code = (char)(intptr_t)lv_event_get_user_data(event);
    size_t len = strlen(s_conv.entry);

    switch (code) {
    case 'C':
        s_conv.entry[0] = '\0';
        s_conv.negative = false;
        break;

    case '<':
        if (len > 0) {
            s_conv.entry[len - 1] = '\0';
        } else {
            s_conv.negative = false;
        }
        break;

    case '~':
        s_conv.negative = !s_conv.negative;
        break;

    case 'S': {
        int t = s_conv.from;
        s_conv.from = s_conv.to;
        s_conv.to   = t;
        save_prefs();
        break;
    }

    case ',':
        if (len < ENTRY_MAX && !strchr(s_conv.entry, ',')) {
            if (len == 0) {
                s_conv.entry[len++] = '0';
            }
            s_conv.entry[len]     = ',';
            s_conv.entry[len + 1] = '\0';
        }
        break;

    default:                                /* digits */
        if (len < ENTRY_MAX) {
            /* A lone zero admits no more leading zeros. */
            if (len == 1 && s_conv.entry[0] == '0' && code == '0') {
                break;
            }
            if (len == 1 && s_conv.entry[0] == '0' && code != '0') {
                len = 0;
            }
            s_conv.entry[len]     = code;
            s_conv.entry[len + 1] = '\0';
        }
        break;
    }

    refresh();
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */

static lv_obj_t *key(lv_obj_t *parent, const char *text, char code,
                     lv_color_t color, int col, int row, int span)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, span * KEY_D + (span - 1) * KEY_GAP_X, KEY_D);
    lv_obj_set_pos(btn, GRID_X + col * (KEY_D + KEY_GAP_X),
                        GRID_Y + row * (KEY_D + KEY_GAP_Y));
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    /* The pressed look goes through opacity and not through a transform: a
     * transform is a layer for LVGL, and that has already hung the board
     * once. */
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, LV_STATE_PRESSED);

    lv_obj_t *label = aos_label(btn, text, aos_font_title, AOS_C_TEXT);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(label);

    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    return btn;
}

/* One of the two rows: the unit on the left, the large number on the right and
 * the whole name below. */
static void build_row(lv_obj_t *parent, int y, bool input, lv_event_cb_t chip_cb,
                      lv_obj_t **chip_label, lv_obj_t **value, lv_obj_t **full)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, ROW_W, ROW_H);
    lv_obj_set_pos(row, ROW_X, y);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_set_style_bg_color(row, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(row, input ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    if (input) {
        /* The top row is where you type: it is marked with a border so you do
         * not have to guess which of the two numbers changes. */
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, AOS_C_ACCENT, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_60, 0);
    }

    lv_obj_t *chip = lv_obj_create(row);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, CHIP_W, 40);
    lv_obj_set_style_radius(chip, 14, 0);
    lv_obj_set_style_bg_color(chip, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(chip, chip_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(chip, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t *cl = aos_label_boxed(chip, "", aos_font_body, AOS_C_TEXT,
                                   CHIP_W, 24);
    lv_obj_remove_flag(cl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(cl);

    lv_obj_t *v = aos_label_boxed(row, "0", aos_font_title, AOS_C_TEXT,
                                  ROW_W - CHIP_W - 28, 32);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -12, 4);

    lv_obj_t *fl = aos_label_boxed(row, "", aos_font_small, AOS_C_DIM,
                                   ROW_W - CHIP_W - 28, 20);
    lv_obj_set_style_text_align(fl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(fl, LV_ALIGN_BOTTOM_RIGHT, -12, -6);

    *chip_label = cl;
    *value      = v;
    *full       = fl;
}

static void build_overlay(void)
{
    lv_obj_t *v = lv_obj_create(s_conv.page);
    lv_obj_remove_style_all(v);
    lv_obj_set_size(v, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(v, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_add_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(v, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(v, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(v, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(v, 6, 0);
    lv_obj_set_style_pad_ver(v, 14, 0);
    lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    s_conv.ov_view = v;

    s_conv.ov_title = aos_label(v, "", aos_font_title, AOS_C_TEXT);
    lv_obj_set_style_pad_bottom(s_conv.ov_title, 6, 0);

    for (int i = 0; i < OV_SLOTS; i++) {
        lv_obj_t *btn = aos_button(v, "", AOS_C_CARD2, overlay_pick_cb,
                                   (void *)(intptr_t)i);
        lv_obj_set_size(btn, 320, 44);
        s_conv.ov_btn[i] = btn;
        s_conv.ov_lbl[i] = lv_obj_get_child(btn, 0);
        lv_obj_set_style_text_font(s_conv.ov_lbl[i], aos_font_small, 0);
    }
}

/* -------------------------------------------------------------------------- */
/* Life cycle                                                                  */

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    s_conv.family   = 0;
    s_conv.from     = 0;
    s_conv.to       = 1;
    s_conv.entry[0] = '\0';
    s_conv.negative = false;
    s_conv.ov_mode  = -1;
    load_prefs();

    lv_obj_t *page = aos_page(root);
    s_conv.page = page;

    s_conv.btn_family = aos_button(page, "", AOS_C_CARD2, open_family_cb, NULL);
    lv_obj_set_size(s_conv.btn_family, 240, 30);
    lv_obj_align(s_conv.btn_family, LV_ALIGN_TOP_MID, 0, FAMILY_Y);
    s_conv.lbl_family = lv_obj_get_child(s_conv.btn_family, 0);
    lv_obj_set_style_text_font(s_conv.lbl_family, aos_font_small, 0);

    build_row(page, ROW_A_Y, true,  open_from_cb, &s_conv.lbl_chip_a,
              &s_conv.val_a, &s_conv.full_a);
    build_row(page, ROW_B_Y, false, open_to_cb,   &s_conv.lbl_chip_b,
              &s_conv.val_b, &s_conv.full_b);

    key(page, "7", '7', C_NUM,  0, 0, 1);
    key(page, "8", '8', C_NUM,  1, 0, 1);
    key(page, "9", '9', C_NUM,  2, 0, 1);
    key(page, "C", 'C', C_FN,   3, 0, 1);

    key(page, "4", '4', C_NUM,  0, 1, 1);
    key(page, "5", '5', C_NUM,  1, 1, 1);
    key(page, "6", '6', C_NUM,  2, 1, 1);
    key(page, LV_SYMBOL_BACKSPACE, '<', C_FN, 3, 1, 1);

    key(page, "1", '1', C_NUM,  0, 2, 1);
    key(page, "2", '2', C_NUM,  1, 2, 1);
    key(page, "3", '3', C_NUM,  2, 2, 1);
    key(page, LV_SYMBOL_LOOP, 'S', C_SWAP, 3, 2, 1);

    key(page, "0",   '0', C_NUM, 0, 3, 2);      /* the zero takes up two columns */
    key(page, ",",   ',', C_NUM, 2, 3, 1);
    key(page, "+/-", '~', C_FN,  3, 3, 1);

    build_overlay();
    refresh();
    return &s_conv;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    /* Only the object pointers: the chosen magnitude and units are kept for
     * the next opening, just as the calculator does. */
    s_conv.page       = NULL;
    s_conv.btn_family = NULL;
    s_conv.lbl_family = NULL;
    s_conv.lbl_chip_a = NULL;
    s_conv.lbl_chip_b = NULL;
    s_conv.val_a      = NULL;
    s_conv.val_b      = NULL;
    s_conv.full_a     = NULL;
    s_conv.full_b     = NULL;
    s_conv.ov_view    = NULL;
    s_conv.ov_title   = NULL;
    s_conv.ov_mode    = -1;
    memset(s_conv.ov_btn, 0, sizeof(s_conv.ov_btn));
    memset(s_conv.ov_lbl, 0, sizeof(s_conv.ov_lbl));
}

static bool back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_conv.ov_mode >= 0) {
        overlay_close();
        return true;
    }
    return false;
}

void aos_app_convert_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.convert",
            .name     = "Conversor",
            .icon_vec = AOS_ICON_CONVERT,
            .color_a  = 0x40C8E0,
            .color_b  = 0x1C5C77,
            .flags    = AOS_APP_FLAG_FULLSCREEN,
            .order    = 56,
        },
        .create  = create,
        .destroy = destroy,
        .back    = back,
    };
}
