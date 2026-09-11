/*
 * AmoledOS - Calendar
 *
 * Month view with today marked, like the watch's calendar: the month's name in
 * red, the week starting on Monday and the days of the previous and next month
 * dimmed so the grid is never lopsided.
 *
 * Navigation, in three forms that do the same thing because on a 368 px screen
 * there is no single one that always works:
 *
 *   - the header's arrows, which is what can be seen without anyone
 *     explaining;
 *   - swiping up/down, which is how a real calendar is browsed;
 *   - tapping the month's name, which opens the year view (the twelve months)
 *     and from there you jump to any month, or change year with its arrows.
 *
 * Swiping right is still leaving, and that is why the app keeps the gestures
 * (AOS_APP_FLAG_NO_SWIPE) rather than leaving them to the runtime: the v2's
 * touch chip detects fast swipes on its own and stops sending coordinates, so
 * the vertical ones would never arrive through LVGL.
 *
 * Dates are computed with integers (Sakamoto for the day of the week), not
 * with mktime(): a 32-bit time_t runs out in 2038 and here you can navigate as
 * far as 2099.
 */
#include "aos_apps.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <string.h>

/* ---- Geometry --------------------------------------------------------------
 * With the status bar in place the root is 368x418. The screen's background is
 * rounded, so nothing goes below ~415 px of screen (386 of the root).
 *
 * 7 columns of 48 px is 336, centred that leaves 16 of margin. 6 rows of 46
 * always cover the longest month starting on a Sunday. */
#define COL_W       48
#define ROW_H       46
#define GRID_X      ((AOS_SCREEN_W - 7 * COL_W) / 2)
#define GRID_Y      74
#define CELL_W      44                  /* the touchable cell, with air around it */
#define CELL_H      40
#define HEADER_Y    2
#define HEADER_H    46
#define WEEK_Y      50
#define FOOTER_Y    356
#define FOOTER_H    30

#define YEAR_MIN    1970
#define YEAR_MAX    2099

/* Two paths bring the same swipe -the chip's and LVGL's, if it was slow- and
 * the second has to be discarded or a clumsy finger jumps two months. */
#define GESTURE_GAP_MS  400

#define C_OTHER     lv_color_hex(0x48484A)  /* days of the neighbouring month */
#define C_WEEKEND   lv_color_hex(0xA0A0A6)  /* Saturday and Sunday of the current month */
#define C_TILE      lv_color_hex(0x1C1C1E)  /* background of the months in the year view */

static const char *const MES_LARGO[12] = {
    N_("Enero"), N_("Febrero"), N_("Marzo"), N_("Abril"), N_("Mayo"), N_("Junio"),
    N_("Julio"), N_("Agosto"), N_("Septiembre"), N_("Octubre"), N_("Noviembre"),
    N_("Diciembre"),
};

static const char *const MES_CORTO_MIN[12] = {
    N_("enero"), N_("febrero"), N_("marzo"), N_("abril"), N_("mayo"), N_("junio"),
    N_("julio"), N_("agosto"), N_("septiembre"), N_("octubre"), N_("noviembre"),
    N_("diciembre"),
};

/* With accents since aos_fonts brings the Latin-1 supplement. It used to say
 * "Miercoles" and "Sabado" on purpose, because LVGL's Montserrat is bare ASCII
 * and LVGL draws NOTHING for a glyph it lacks: the word came out with a hole in
 * the middle. */
static const char *const DIA_LARGO[7] = {
    N_("Domingo"), N_("Lunes"), N_("Martes"), N_("Miércoles"), N_("Jueves"),
    N_("Viernes"), N_("Sábado"),
};

/* Header of the grid, with the week starting on Monday.
 *
 * With context, and not on their own: in Spanish martes and miercoles both
 * start with M, but in English they are T and W. One "M" key cannot give
 * both. */
static const char *const DIA_INICIAL[7] = {
    NC_("lun", "L"), NC_("mar", "M"), NC_("mie", "M"), NC_("jue", "J"),
    NC_("vie", "V"), NC_("sab", "S"), NC_("dom", "D"),
};
/* In lower case, like MES_CORTO_MIN. The footer used to lower the first letter
 * by hand with dia[0] - 'A' + 'a', which is SPANISH ORTHOGRAPHY written into
 * the code: in English the days are capitalised and that subtraction broke
 * them. With a separate table the catalogue decides. */
static const char *const DIA_LARGO_MIN[7] = {
    N_("domingo"), N_("lunes"), N_("martes"), N_("miércoles"), N_("jueves"),
    N_("viernes"), N_("sábado"),
};

static const char *const DIA_INICIAL_CTX[7] = {
    "lun", "mar", "mie", "jue", "vie", "sab", "dom",
};

typedef struct {
    lv_obj_t *page;

    /* month view */
    lv_obj_t *month_view;
    lv_obj_t *lbl_month;
    lv_obj_t *lbl_year;
    lv_obj_t *cell[42];             /* touchable container */
    lv_obj_t *cell_lbl[42];
    lv_obj_t *lbl_footer;
    lv_obj_t *chip_today;

    /* year view */
    lv_obj_t *year_view;
    lv_obj_t *lbl_yv;
    lv_obj_t *tile[12];

    int  view_y, view_m;            /* month being looked at (m: 0..11) */
    int  yv_year;                   /* year of the year view */
    int  sel_d;                     /* selected day, 0 = none */
    int  cell_day[42];              /* day each cell shows */
    int  cell_off[42];              /* -1 previous month, 0 this one, +1 next */

    int  today_y, today_m, today_d; /* today_y < 0 = the clock is not set */

    bool     year_open;
    bool     want_exit;
    bool     time_ok;               /* cached answer of time_is_valid()       */
    uint32_t last_gesture_ms;
    uint32_t last_check_ms;         /* last re-read of the date               */
} cal_t;

AOS_BSS_PSRAM static cal_t s_cal;

/* -------------------------------------------------------------------------- */
/* Dates, all with integers                                                    */

static bool leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m)
{
    static const int len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 1 && leap(y)) ? 29 : len[m];
}

/* Sakamoto: 0 = Sunday. Valid for any date in the Gregorian calendar. */
static int weekday(int y, int m, int d)
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 2) {
        y--;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m] + d) % 7;
}

/* Column of the grid, with the week starting on Monday. */
static int col_of(int wday)
{
    return (wday + 6) % 7;
}

static uint32_t now_ms(void)
{
    return (uint32_t)aos_hal_uptime_ms();
}

static void read_today(void)
{
    /* aos_hal_time_is_valid() goes all the way to NVS, so it is not asked five
     * times a second: once on opening and after that only while there is still
     * no time, which is when the answer can still change. */
    if (!s_cal.time_ok) {
        s_cal.time_ok = aos_hal_time_is_valid();
    }
    if (!s_cal.time_ok) {
        s_cal.today_y = -1;
        return;
    }

    struct tm tm_now;
    aos_hal_time_now(&tm_now);
    s_cal.today_y = tm_now.tm_year + 1900;
    s_cal.today_m = tm_now.tm_mon;
    s_cal.today_d = tm_now.tm_mday;
}

static bool viewing_today_month(void)
{
    return s_cal.today_y >= 0 &&
           s_cal.today_y == s_cal.view_y && s_cal.today_m == s_cal.view_m;
}

/* -------------------------------------------------------------------------- */
/* Month view                                                                  */

static void footer_refresh(void)
{
    char buf[64];

    if (s_cal.today_y < 0) {
        lv_label_set_text(s_cal.lbl_footer, _("reloj sin ajustar"));
        lv_obj_set_style_text_color(s_cal.lbl_footer, AOS_C_ORANGE, 0);
        return;
    }
    lv_obj_set_style_text_color(s_cal.lbl_footer, AOS_C_DIM, 0);

    if (s_cal.sel_d > 0) {
        int wd = weekday(s_cal.view_y, s_cal.view_m, s_cal.sel_d);
        snprintf(buf, sizeof(buf), _("%s %d de %s"),
                 _(DIA_LARGO[wd]), s_cal.sel_d, _(MES_CORTO_MIN[s_cal.view_m]));
    } else {
        /* With nothing selected the footer talks about today, and says so:
         * otherwise, in a month that is not today's there is a loose date that
         * looks as if it belonged to the month being looked at. */
        int wd = weekday(s_cal.today_y, s_cal.today_m, s_cal.today_d);
        snprintf(buf, sizeof(buf), _("Hoy: %s %d de %s"),
                 _(DIA_LARGO_MIN[wd]), s_cal.today_d,
                 _(MES_CORTO_MIN[s_cal.today_m]));
    }
    lv_label_set_text(s_cal.lbl_footer, buf);
}

/* Fills the 42 cells that already exist. Rebuilding them would be convenient
 * and on the board costs four times as much: 111-124 ms against 18-31 (see
 * HANDOFF-APPS). */
static void month_refresh(void)
{
    char buf[16];

    lv_label_set_text(s_cal.lbl_month, _(MES_LARGO[s_cal.view_m]));
    snprintf(buf, sizeof(buf), "%d", s_cal.view_y);
    lv_label_set_text(s_cal.lbl_year, buf);

    const int start   = col_of(weekday(s_cal.view_y, s_cal.view_m, 1));
    const int dim     = days_in_month(s_cal.view_y, s_cal.view_m);
    const int prev_m  = (s_cal.view_m + 11) % 12;
    const int prev_y  = (s_cal.view_m == 0) ? s_cal.view_y - 1 : s_cal.view_y;
    const int prev_dim = days_in_month(prev_y, prev_m);

    for (int i = 0; i < 42; i++) {
        int day, off;

        if (i < start) {
            day = prev_dim - start + 1 + i;
            off = -1;
        } else if (i - start < dim) {
            day = i - start + 1;
            off = 0;
        } else {
            day = i - start - dim + 1;
            off = +1;
        }
        s_cal.cell_day[i] = day;
        s_cal.cell_off[i] = off;

        snprintf(buf, sizeof(buf), "%d", day);
        lv_label_set_text(s_cal.cell_lbl[i], buf);

        bool today    = (off == 0 && viewing_today_month() && day == s_cal.today_d);
        bool selected = (off == 0 && day == s_cal.sel_d && !today);
        bool weekend  = (i % 7) >= 5;

        lv_color_t fg = C_OTHER;
        if (off == 0) {
            fg = today ? AOS_C_TEXT : (weekend ? C_WEEKEND : AOS_C_TEXT);
        }
        lv_obj_set_style_text_color(s_cal.cell_lbl[i], fg, 0);

        if (today) {
            lv_obj_set_style_bg_color(s_cal.cell[i], AOS_C_RED, 0);
            lv_obj_set_style_bg_opa(s_cal.cell[i], LV_OPA_COVER, 0);
        } else if (selected) {
            lv_obj_set_style_bg_color(s_cal.cell[i], AOS_C_CARD2, 0);
            lv_obj_set_style_bg_opa(s_cal.cell[i], LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_opa(s_cal.cell[i], LV_OPA_TRANSP, 0);
        }
    }

    /* The shortcut to today only makes sense if today is not on screen. */
    if (viewing_today_month() || s_cal.today_y < 0) {
        lv_obj_add_flag(s_cal.chip_today, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_cal.chip_today, LV_OBJ_FLAG_HIDDEN);
    }

    footer_refresh();
}

/* delta in months, carrying into the year and clamped to the navigable range */
static void go_month(int delta)
{
    int total = s_cal.view_y * 12 + s_cal.view_m + delta;
    int y = total / 12;
    int m = total % 12;
    if (m < 0) {
        m += 12;
        y--;
    }
    if (y < YEAR_MIN || y > YEAR_MAX) {
        return;
    }
    s_cal.view_y = y;
    s_cal.view_m = m;
    s_cal.sel_d  = 0;           /* the selection belongs to the month we left behind */
    month_refresh();
}

static void go_today(void)
{
    read_today();
    if (s_cal.today_y < 0) {
        return;
    }
    s_cal.view_y = s_cal.today_y;
    s_cal.view_m = s_cal.today_m;
    s_cal.sel_d  = 0;
    month_refresh();
}

/* -------------------------------------------------------------------------- */
/* Year view                                                                   */

static void year_refresh(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", s_cal.yv_year);
    lv_label_set_text(s_cal.lbl_yv, buf);

    for (int m = 0; m < 12; m++) {
        bool es_hoy    = (s_cal.today_y == s_cal.yv_year && s_cal.today_m == m);
        bool es_actual = (s_cal.view_y == s_cal.yv_year && s_cal.view_m == m);

        lv_obj_set_style_bg_color(s_cal.tile[m], es_hoy ? AOS_C_RED : C_TILE, 0);
        lv_obj_set_style_border_width(s_cal.tile[m], es_actual && !es_hoy ? 2 : 0, 0);
    }
}

static void year_view_set(bool open)
{
    s_cal.year_open = open;
    if (open) {
        s_cal.yv_year = s_cal.view_y;
        year_refresh();
        lv_obj_remove_flag(s_cal.year_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_cal.month_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_cal.year_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_cal.month_view, LV_OBJ_FLAG_HIDDEN);
    }
}

static void go_year(int delta)
{
    int y = s_cal.yv_year + delta;
    if (y < YEAR_MIN || y > YEAR_MAX) {
        return;
    }
    s_cal.yv_year = y;
    year_refresh();
}

/* -------------------------------------------------------------------------- */
/* Events                                                                      */

static void prev_cb(lv_event_t *e)
{
    (void)e;
    aos_hal_activity();
    go_month(-1);
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    aos_hal_activity();
    go_month(+1);
}

static void header_cb(lv_event_t *e)
{
    (void)e;
    aos_hal_activity();
    year_view_set(true);
}

static void today_cb(lv_event_t *e)
{
    (void)e;
    aos_hal_activity();
    go_today();
}

static void cell_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    aos_hal_activity();

    /* Tapping a day of the neighbouring month takes you to that month, which
     * is what you expect when you tap the "1" poking out at the end of the
     * grid. */
    if (s_cal.cell_off[i] != 0) {
        int dia = s_cal.cell_day[i];
        go_month(s_cal.cell_off[i]);
        s_cal.sel_d = dia;
        month_refresh();
        return;
    }
    s_cal.sel_d = (s_cal.sel_d == s_cal.cell_day[i]) ? 0 : s_cal.cell_day[i];
    month_refresh();
}

static void yv_prev_cb(lv_event_t *e) { (void)e; aos_hal_activity(); go_year(-1); }
static void yv_next_cb(lv_event_t *e) { (void)e; aos_hal_activity(); go_year(+1); }

static void tile_cb(lv_event_t *e)
{
    int m = (int)(intptr_t)lv_event_get_user_data(e);
    aos_hal_activity();

    s_cal.view_y = s_cal.yv_year;
    s_cal.view_m = m;
    s_cal.sel_d  = 0;
    month_refresh();
    year_view_set(false);
}

/* A single place for the two paths a swipe arrives by. */
static void navigate(lv_dir_t dir)
{
    uint32_t t = now_ms();
    if (t - s_cal.last_gesture_ms < GESTURE_GAP_MS) {
        return;
    }
    s_cal.last_gesture_ms = t;
    aos_hal_activity();

    if (s_cal.year_open) {
        switch (dir) {
        case LV_DIR_TOP:    go_year(+1);         break;
        case LV_DIR_BOTTOM: go_year(-1);         break;
        case LV_DIR_RIGHT:  year_view_set(false); break;
        default: break;
        }
        return;
    }
    switch (dir) {
    case LV_DIR_TOP:    go_month(+1);        break;
    case LV_DIR_BOTTOM: go_month(-1);        break;
    case LV_DIR_LEFT:   year_view_set(true); break;
    case LV_DIR_RIGHT:  s_cal.want_exit = true; break;   /* done on the tick */
    default: break;
    }
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        navigate(lv_indev_get_gesture_dir(indev));
    }
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */

static lv_obj_t *arrow(lv_obj_t *parent, const char *sym, int32_t x,
                       lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, 44, HEADER_H);
    lv_obj_set_pos(btn, x, HEADER_Y);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);

    lv_obj_t *lbl = aos_label(btn, sym, aos_font_body, AOS_C_DIM);
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(lbl);

    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static void build_month_view(lv_obj_t *parent)
{
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_remove_style_all(v);
    lv_obj_set_size(v, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(v, LV_OBJ_FLAG_CLICKABLE);
    s_cal.month_view = v;

    arrow(v, LV_SYMBOL_LEFT,  8, prev_cb);
    arrow(v, LV_SYMBOL_RIGHT, AOS_SCREEN_W - 52, next_cb);

    /* Month and year in a flex row: the width of the name varies a lot between
     * "Mayo" and "Septiembre" and this way nothing has to be repositioned by
     * hand. */
    lv_obj_t *hdr = lv_obj_create(v);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, AOS_SCREEN_W - 120, HEADER_H);
    lv_obj_set_pos(hdr, 60, HEADER_Y);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 8, 0);
    lv_obj_set_style_radius(hdr, 14, 0);
    lv_obj_set_style_bg_color(hdr, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_STATE_PRESSED);

    s_cal.lbl_month = aos_label(hdr, "", aos_font_title, AOS_C_RED);
    /* Width ceiling and an ellipsis: the month's name is the longest string on
     * the screen and in another language it can grow quite a bit. Without this
     * it pushes the year out of the row and both run off the header -found by
     * the pseudolocalisation, English did not reach-. Cut with an ellipsis is
     * ugly; overlapping the year is worse. */
    lv_obj_set_size(s_cal.lbl_month, AOS_SCREEN_W - 200, HEADER_H - 6);
    lv_label_set_long_mode(s_cal.lbl_month, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(s_cal.lbl_month, LV_TEXT_ALIGN_RIGHT, 0);

    s_cal.lbl_year  = aos_label(hdr, "", aos_font_body,  AOS_C_DIM);
    lv_obj_remove_flag(s_cal.lbl_month, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_cal.lbl_year,  LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hdr, header_cb, LV_EVENT_CLICKED, NULL);

    /* Header of the week */
    for (int c = 0; c < 7; c++) {
        lv_obj_t *lbl = aos_label_boxed(v, C_(DIA_INICIAL_CTX[c], DIA_INICIAL[c]), aos_font_small,
                                        c >= 5 ? C_OTHER : AOS_C_DIM,
                                        COL_W, 20);
        lv_obj_set_pos(lbl, GRID_X + c * COL_W, WEEK_Y);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t *line = lv_obj_create(v);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 7 * COL_W, 1);
    lv_obj_set_pos(line, GRID_X, WEEK_Y + 24);
    lv_obj_set_style_bg_color(line, C_OTHER, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);

    /* The 42 cells are created once and after that only filled in. */
    for (int i = 0; i < 42; i++) {
        lv_obj_t *cell = lv_obj_create(v);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, CELL_W, CELL_H);
        lv_obj_set_pos(cell, GRID_X + (i % 7) * COL_W + (COL_W - CELL_W) / 2,
                             GRID_Y + (i / 7) * ROW_H + (ROW_H - CELL_H) / 2);
        lv_obj_set_style_radius(cell, 12, 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(cell, AOS_C_TEXT, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(cell, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cell, cell_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = aos_label(cell, "", aos_font_body, AOS_C_TEXT);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(lbl);

        s_cal.cell[i]     = cell;
        s_cal.cell_lbl[i] = lbl;
    }

    s_cal.lbl_footer = aos_label(v, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_pos(s_cal.lbl_footer, GRID_X, FOOTER_Y + 6);
    lv_obj_remove_flag(s_cal.lbl_footer, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *chip = lv_obj_create(v);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 60, FOOTER_H);
    lv_obj_set_pos(chip, AOS_SCREEN_W - GRID_X - 60, FOOTER_Y);
    lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(chip, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(chip, today_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *chip_lbl = aos_label(chip, _("HOY"), aos_font_small, AOS_C_TEXT);
    lv_obj_remove_flag(chip_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(chip_lbl);
    s_cal.chip_today = chip;
}

static void build_year_view(lv_obj_t *parent)
{
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_remove_style_all(v);
    lv_obj_set_size(v, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(v, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    s_cal.year_view = v;

    arrow(v, LV_SYMBOL_LEFT,  8, yv_prev_cb);
    arrow(v, LV_SYMBOL_RIGHT, AOS_SCREEN_W - 52, yv_next_cb);

    s_cal.lbl_yv = aos_label_boxed(v, "", aos_font_title, AOS_C_TEXT,
                                   AOS_SCREEN_W - 120, HEADER_H);
    lv_obj_set_pos(s_cal.lbl_yv, 60, HEADER_Y + 8);
    lv_obj_remove_flag(s_cal.lbl_yv, LV_OBJ_FLAG_CLICKABLE);

    /* 3 columns of 100 with 14 of spacing: 328, centred in 368. Four rows of
     * 62 with 10 end at 334, which leaves air for the hint. */
    const int32_t tw = 100, th = 62, gx = 14, gy = 10;
    const int32_t x0 = (AOS_SCREEN_W - (3 * tw + 2 * gx)) / 2;
    const int32_t y0 = 58;

    for (int m = 0; m < 12; m++) {
        lv_obj_t *tile = lv_obj_create(v);
        lv_obj_remove_style_all(tile);
        lv_obj_set_size(tile, tw, th);
        lv_obj_set_pos(tile, x0 + (m % 3) * (tw + gx), y0 + (m / 3) * (th + gy));
        lv_obj_set_style_radius(tile, 16, 0);
        lv_obj_set_style_bg_color(tile, C_TILE, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(tile, AOS_C_ACCENT, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_60, LV_STATE_PRESSED);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, tile_cb, LV_EVENT_CLICKED, (void *)(intptr_t)m);

        lv_obj_t *lbl = aos_label(tile, aos_month_name(m), aos_font_title,
                                  AOS_C_TEXT);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(lbl);

        s_cal.tile[m] = tile;
    }

    lv_obj_t *hint = aos_label(v, _("tocar un mes"), aos_font_small, C_OTHER);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, FOOTER_Y - 2);
    lv_obj_remove_flag(hint, LV_OBJ_FLAG_CLICKABLE);
}

/* -------------------------------------------------------------------------- */
/* Life cycle                                                                  */

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    memset(&s_cal, 0, sizeof(s_cal));
    /* With the clock at zero, "more than a second ago" would be true on the
     * first tick of every opening: the counters start with the time set. */
    s_cal.last_gesture_ms = now_ms();
    s_cal.last_check_ms   = s_cal.last_gesture_ms;

    s_cal.page = aos_page(root);
    build_month_view(s_cal.page);
    build_year_view(s_cal.page);

    read_today();
    if (s_cal.today_y < 0) {                /* clock not set: something has to be shown */
        struct tm tm_now;
        aos_hal_time_now(&tm_now);
        s_cal.view_y = tm_now.tm_year + 1900;
        s_cal.view_m = tm_now.tm_mon;
        if (s_cal.view_y < YEAR_MIN || s_cal.view_y > YEAR_MAX) {
            s_cal.view_y = YEAR_MIN;
            s_cal.view_m = 0;
        }
    } else {
        s_cal.view_y = s_cal.today_y;
        s_cal.view_m = s_cal.today_m;
    }
    month_refresh();

    /* The gesture is listened for on the root, and the bubbling flag has to be
     * taken off the root: LVGL delivers LV_EVENT_GESTURE to the first ancestor
     * that does NOT have it, so if it is left on the event goes straight to
     * the screen and the app never finds out. */
    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, gesture_cb, LV_EVENT_GESTURE, NULL);
    return &s_cal;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;

    /* The objects are deleted HERE, with the context still standing. If we
     * left them to the runtime -which deletes the root only after destroy()-,
     * any event from the deletion would reach callbacks already reading a
     * zeroed context. The empty root it deletes all the same. */
    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    memset(&s_cal, 0, sizeof(s_cal));
}

static bool back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_cal.year_open) {
        year_view_set(false);
        return true;                    /* consumed: we navigate inside */
    }
    return false;
}

static void tick(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;

    /* Leaving is deferred: aos_ui_back() destroys the app, and calling it from
     * the gesture's callback would be destroying it while it runs. */
    if (s_cal.want_exit) {
        s_cal.want_exit = false;
        aos_ui_back();
        return;                         /* after this there is no context left */
    }

    /* Swipes LVGL never saw: on the v2 the fast ones are detected by the touch
     * chip and no intermediate coordinate arrives through LVGL. */
    switch (aos_ui_take_gesture()) {
    case AOS_TOUCH_GESTURE_UP:    navigate(LV_DIR_TOP);    break;
    case AOS_TOUCH_GESTURE_DOWN:  navigate(LV_DIR_BOTTOM); break;
    case AOS_TOUCH_GESTURE_LEFT:  navigate(LV_DIR_LEFT);   break;
    case AOS_TOUCH_GESTURE_RIGHT: navigate(LV_DIR_RIGHT);  break;
    default: break;
    }
    if (s_cal.want_exit) {
        s_cal.want_exit = false;
        aos_ui_back();
        return;
    }

    /* With the app open at midnight, today moves to another cell. Once a
     * second is enough and it also avoids touching the clock on every tick. */
    uint32_t t = now_ms();
    if (t - s_cal.last_check_ms >= 1000) {
        s_cal.last_check_ms = t;

        int prev_y = s_cal.today_y, prev_m = s_cal.today_m, prev_d = s_cal.today_d;
        read_today();
        if (s_cal.today_y != prev_y || s_cal.today_m != prev_m ||
            s_cal.today_d != prev_d) {
            month_refresh();
        }
    }
}

void aos_app_calendar_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.calendar",
            .name     = "Calendario",
            .icon_vec = AOS_ICON_CALENDAR,
            .color_a  = 0xFF453A,
            .color_b  = 0x8E1F18,
            .flags    = AOS_APP_FLAG_NO_SWIPE,
            .order    = 42,
        },
        .create  = create,
        .destroy = destroy,
        .back    = back,
        .tick    = tick,
    };
}
