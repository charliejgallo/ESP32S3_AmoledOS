/*
 * AmoledOS - World clock
 *
 * Up to eight cities, each with its time, its offset and whether it is a day
 * behind or ahead. The list is picked from a catalogue and stored in
 * preferences.
 *
 * How another city's time is computed: the HAL already uses POSIX TZ strings
 * (aos_hal_timezone_set), so here it is enough to set TZ, call tzset() and
 * convert with localtime_r(). Two cautions:
 *
 *   - aos_hal_timezone_set() is NOT used, since it also writes the zone into
 *     NVS: that would change the time of the whole watch. The environment
 *     variable is set by hand and the previous one restored.
 *   - It is done in a batch and once a minute, not per city and not on every
 *     tick: each tzset() re-parses the daylight saving rule.
 *
 * Zones with daylight saving carry the full rule in the string; those without
 * go with the bare offset. That is why the catalogue is a hand-written table
 * and not a list of IANA names: there is no time zone database on the board,
 * only newlib's POSIX parser.
 */
#include "aos_apps.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SEL     8

typedef struct {
    const char *name;
    const char *tz;
} city_t;

/* Ordered west to east. The daylight saving rules are those of 2026: the USA
 * second Sunday of March to first Sunday of November, Europe the last Sunday
 * of March to the last of October, and the other way round in the south. The
 * countries that dropped it (Mexico 2022, Brazil 2019, Turkey 2016, Uruguay
 * 2015, Russia 2014) go with the fixed offset. */
/* Only the name carries N_: the string on the right is a POSIX TZ rule
 * consumed by setenv("TZ"), that is, configuration. Translating it would break
 * the clock. */
static const city_t CITIES[] = {
    { N_("Honolulu"),       "HST10" },
    { N_("Anchorage"),      "AKST9AKDT,M3.2.0,M11.1.0" },
    { N_("Los Angeles"),    "PST8PDT,M3.2.0,M11.1.0" },
    { N_("Denver"),         "MST7MDT,M3.2.0,M11.1.0" },
    { N_("Ciudad de Mexico"), "CST6" },
    { N_("Chicago"),        "CST6CDT,M3.2.0,M11.1.0" },
    { N_("Bogota"),         "COT5" },
    { N_("Lima"),           "PET5" },
    { N_("Nueva York"),     "EST5EDT,M3.2.0,M11.1.0" },
    { N_("Toronto"),        "EST5EDT,M3.2.0,M11.1.0" },
    { N_("Caracas"),        "VET4" },
    { N_("Santiago"),       "CLT4CLST,M9.1.6/24,M4.1.6/24" },
    { N_("Buenos Aires"),   "ART3" },
    { N_("Montevideo"),     "UYT3" },
    { N_("Sao Paulo"),      "BRT3" },
    { N_("Londres"),        "GMT0BST,M3.5.0/1,M10.5.0/2" },
    { N_("Lisboa"),         "WET0WEST,M3.5.0/1,M10.5.0/2" },
    { N_("Madrid"),         "CET-1CEST,M3.5.0,M10.5.0/3" },
    { N_("Paris"),          "CET-1CEST,M3.5.0,M10.5.0/3" },
    { N_("Berlin"),         "CET-1CEST,M3.5.0,M10.5.0/3" },
    { N_("Roma"),           "CET-1CEST,M3.5.0,M10.5.0/3" },
    { N_("Lagos"),          "WAT-1" },
    { N_("Atenas"),         "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { N_("Helsinki"),       "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { N_("El Cairo"),       "EET-2EEST,M4.5.5/0,M10.5.4/24" },
    { N_("Johannesburgo"),  "SAST-2" },
    { N_("Estambul"),       "TRT-3" },
    { N_("Moscu"),          "MSK-3" },
    { N_("Nairobi"),        "EAT-3" },
    { N_("Dubai"),          "GST-4" },
    { N_("Nueva Delhi"),    "IST-5:30" },
    { N_("Bangkok"),        "ICT-7" },
    { N_("Yakarta"),        "WIB-7" },
    { N_("Pekin"),          "CST-8" },
    { N_("Hong Kong"),      "HKT-8" },
    { N_("Singapur"),       "SGT-8" },
    { N_("Tokio"),          "JST-9" },
    { N_("Seul"),           "KST-9" },
    { N_("Sidney"),         "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { N_("Auckland"),       "NZST-12NZDT,M9.5.0,M4.1.0/3" },
};
#define CITY_COUNT  ((int)(sizeof(CITIES) / sizeof(CITIES[0])))

/* Starting cities for anybody who has never chosen: one per continent. */
static const uint8_t DEFAULT_SEL[] = { 12, 8, 17, 36 };  /* BsAs, NY, Madrid, Tokyo */

typedef struct {
    lv_obj_t *page;

    /* list view */
    lv_obj_t *list_view;
    lv_obj_t *lbl_local_time;
    lv_obj_t *lbl_local_date;
    lv_obj_t *row[MAX_SEL];
    lv_obj_t *row_name[MAX_SEL];
    lv_obj_t *row_sub[MAX_SEL];
    lv_obj_t *row_time[MAX_SEL];
    lv_obj_t *row_dot[MAX_SEL];
    lv_obj_t *lbl_empty;

    /* catalogue; built the first time it is opened, not when the app is
     * created: it is 40 rows and building them all at once shows on the
     * board */
    lv_obj_t *pick_view;
    lv_obj_t *pick_row[CITY_COUNT];
    lv_obj_t *pick_check[CITY_COUNT];
    bool      pick_built;
    bool      pick_open;

    uint8_t sel[MAX_SEL];
    int     sel_count;

    int  last_min;          /* -1 = nothing has been drawn yet */
    bool want_exit;
} wc_t;

AOS_BSS_PSRAM static wc_t s_wc;

/* -------------------------------------------------------------------------- */
/* Zones                                                                       */

/* Difference in days between two nearby dates: -1, 0 or +1. */
static int day_delta(const struct tm *a, const struct tm *b)
{
    if (a->tm_year != b->tm_year) {
        return a->tm_year > b->tm_year ? 1 : -1;
    }
    int d = a->tm_yday - b->tm_yday;
    return d > 0 ? 1 : (d < 0 ? -1 : 0);
}

/* Writes "GMT-3" or "GMT+5:30" from the offset in minutes. */
static void format_offset(int off_min, char *out, size_t len)
{
    char sign = off_min < 0 ? '-' : '+';
    int abs_min = off_min < 0 ? -off_min : off_min;
    if (abs_min % 60 == 0) {
        snprintf(out, len, "GMT%c%d", sign, abs_min / 60);
    } else {
        snprintf(out, len, "GMT%c%d:%02d", sign, abs_min / 60, abs_min % 60);
    }
}

/* Resolves the times of every chosen city in a single pass.
 *
 * getenv() returns a pointer into the environment and setenv() may move it, so
 * the previous zone is copied before anything is touched. */
static void resolve(struct tm out[MAX_SEL], int off_min[MAX_SEL],
                    int day_off[MAX_SEL], const struct tm *local)
{
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);

    char saved[48];
    const char *cur = getenv("TZ");
    snprintf(saved, sizeof(saved), "%s", cur ? cur : aos_hal_timezone_get());

    for (int i = 0; i < s_wc.sel_count; i++) {
        setenv("TZ", CITIES[s_wc.sel[i]].tz, 1);
        tzset();
        localtime_r(&now, &out[i]);

        off_min[i] = (out[i].tm_hour * 60 + out[i].tm_min) -
                     (utc.tm_hour * 60 + utc.tm_min) +
                     day_delta(&out[i], &utc) * 24 * 60;
        day_off[i] = day_delta(&out[i], local);
    }

    setenv("TZ", saved, 1);
    tzset();
}

/* -------------------------------------------------------------------------- */
/* Preferences                                                                 */

static void save_sel(void)
{
    char buf[MAX_SEL * 4 + 1];
    int  n = 0;
    buf[0] = '\0';
    for (int i = 0; i < s_wc.sel_count; i++) {
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, i ? ",%d" : "%d",
                      (int)s_wc.sel[i]);
        if (n >= (int)sizeof(buf)) {
            break;
        }
    }
    aos_hal_pref_set_str("wc_sel", buf);
}

static void load_sel(void)
{
    char buf[MAX_SEL * 4 + 1];
    s_wc.sel_count = 0;

    if (aos_hal_pref_get_str("wc_sel", buf, sizeof(buf))) {
        const char *p = buf;
        while (*p && s_wc.sel_count < MAX_SEL) {
            int v = atoi(p);
            if (v >= 0 && v < CITY_COUNT) {
                s_wc.sel[s_wc.sel_count++] = (uint8_t)v;
            }
            const char *comma = strchr(p, ',');
            if (!comma) {
                break;
            }
            p = comma + 1;
        }
        return;         /* a deliberately stored empty list is honoured */
    }

    for (unsigned i = 0; i < sizeof(DEFAULT_SEL) / sizeof(DEFAULT_SEL[0]); i++) {
        s_wc.sel[s_wc.sel_count++] = DEFAULT_SEL[i];
    }
}

static int sel_index_of(int city)
{
    for (int i = 0; i < s_wc.sel_count; i++) {
        if (s_wc.sel[i] == city) {
            return i;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------- */
/* Drawing                                                                     */

static void refresh_rows(void)
{
    struct tm local;
    aos_hal_time_now(&local);

    char buf[48];
    snprintf(buf, sizeof(buf), "%02d:%02d", local.tm_hour, local.tm_min);
    lv_label_set_text(s_wc.lbl_local_time, buf);

    snprintf(buf, sizeof(buf), "%s %d %s", aos_day_name(local.tm_wday),
             local.tm_mday, aos_month_name(local.tm_mon));
    lv_label_set_text(s_wc.lbl_local_date, buf);

    struct tm zone[MAX_SEL];
    int off[MAX_SEL], dayoff[MAX_SEL];
    memset(zone, 0, sizeof(zone));
    memset(off, 0, sizeof(off));
    memset(dayoff, 0, sizeof(dayoff));
    resolve(zone, off, dayoff, &local);

    for (int i = 0; i < MAX_SEL; i++) {
        if (i >= s_wc.sel_count) {
            lv_obj_add_flag(s_wc.row[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_wc.row[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_wc.row_name[i], _(CITIES[s_wc.sel[i]].name));

        snprintf(buf, sizeof(buf), "%02d:%02d", zone[i].tm_hour, zone[i].tm_min);
        lv_label_set_text(s_wc.row_time[i], buf);

        char gmt[16];
        format_offset(off[i], gmt, sizeof(gmt));
        snprintf(buf, sizeof(buf), "%s  %s", gmt,
                 dayoff[i] > 0 ? _("manana") : (dayoff[i] < 0 ? _("ayer") : _("hoy")));
        lv_label_set_text(s_wc.row_sub[i], buf);

        /* Day or night, so you do not have to do the sum in your head before
         * calling somebody. */
        bool day = zone[i].tm_hour >= 7 && zone[i].tm_hour < 20;
        lv_obj_set_style_bg_color(s_wc.row_dot[i],
                                  day ? AOS_C_YELLOW : lv_color_hex(0x30456A), 0);
        lv_obj_set_style_text_color(s_wc.row_time[i],
                                    day ? AOS_C_TEXT : AOS_C_DIM, 0);
    }

    if (s_wc.sel_count == 0) {
        lv_obj_remove_flag(s_wc.lbl_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_wc.lbl_empty, LV_OBJ_FLAG_HIDDEN);
    }

    s_wc.last_min = local.tm_min;
}

static void refresh_checks(void)
{
    if (!s_wc.pick_built) {
        return;
    }
    for (int i = 0; i < CITY_COUNT; i++) {
        bool on = sel_index_of(i) >= 0;
        lv_obj_set_style_text_color(s_wc.pick_check[i],
                                    on ? AOS_C_GREEN : AOS_C_CARD2, 0);
        lv_obj_set_style_bg_opa(s_wc.pick_row[i], on ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

/* -------------------------------------------------------------------------- */
/* Callbacks                                                                   */

static void pick_toggle_cb(lv_event_t *event)
{
    int city = (int)(intptr_t)lv_event_get_user_data(event);
    int at   = sel_index_of(city);

    if (at >= 0) {
        for (int i = at; i < s_wc.sel_count - 1; i++) {
            s_wc.sel[i] = s_wc.sel[i + 1];
        }
        s_wc.sel_count--;
        aos_hal_beep(900, 20);
    } else if (s_wc.sel_count < MAX_SEL) {
        s_wc.sel[s_wc.sel_count++] = (uint8_t)city;
        aos_hal_beep(1400, 20);
    } else {
        aos_ui_toast(_("Ya hay ocho ciudades"), 1500);
        return;
    }

    save_sel();
    refresh_checks();
}

static void show_pick(bool open);

static void open_pick_cb(lv_event_t *event)
{
    (void)event;
    show_pick(true);
}

static void close_pick_cb(lv_event_t *event)
{
    (void)event;
    show_pick(false);
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */

static void build_list(lv_obj_t *parent)
{
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_remove_style_all(v);
    lv_obj_set_size(v, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(v, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(v, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(v, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(v, 6, 0);
    lv_obj_set_style_pad_ver(v, 10, 0);
    s_wc.list_view = v;

    /* Header: the time here, which is what the others are compared against. */
    lv_obj_t *head = lv_obj_create(v);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, 336, 76);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *here = aos_label(head, _("AQUI"), aos_font_small, AOS_C_ACCENT);
    lv_obj_align(here, LV_ALIGN_TOP_LEFT, 6, 4);

    s_wc.lbl_local_date = aos_label(head, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_wc.lbl_local_date, LV_ALIGN_BOTTOM_LEFT, 6, -6);

    s_wc.lbl_local_time = aos_label_boxed(head, "--:--", aos_font_huge,
                                          AOS_C_TEXT, 200, 56);
    lv_obj_align(s_wc.lbl_local_time, LV_ALIGN_RIGHT_MID, -6, 0);

    /* The eight rows are created once and filled in; the spare ones are
     * hidden. Rebuilding them on every refresh is what costs 111 ms on the
     * board. */
    for (int i = 0; i < MAX_SEL; i++) {
        lv_obj_t *row = lv_obj_create(v);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 336, 58);
        lv_obj_set_style_radius(row, 18, 0);
        lv_obj_set_style_bg_color(row, AOS_C_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 14, -12);

        lv_obj_t *name = aos_label(row, "", aos_font_body, AOS_C_TEXT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 32, -12);

        lv_obj_t *sub = aos_label(row, "", aos_font_small, AOS_C_DIM);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 32, 14);

        lv_obj_t *hhmm = aos_label_boxed(row, "--:--", aos_font_title,
                                         AOS_C_TEXT, 108, 34);
        lv_obj_align(hhmm, LV_ALIGN_RIGHT_MID, -10, 0);

        s_wc.row[i]      = row;
        s_wc.row_dot[i]  = dot;
        s_wc.row_name[i] = name;
        s_wc.row_sub[i]  = sub;
        s_wc.row_time[i] = hhmm;
    }

    s_wc.lbl_empty = aos_label_boxed(v, _("sin ciudades"), aos_font_body,
                                     AOS_C_DIM, 300, 40);
    lv_obj_add_flag(s_wc.lbl_empty, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *btn = aos_button(v, _("Ciudades"), AOS_C_CARD2, open_pick_cb, NULL);
    lv_obj_set_size(btn, 200, 56);
}

static void build_pick(void)
{
    lv_obj_t *v = lv_obj_create(s_wc.page);
    lv_obj_remove_style_all(v);
    lv_obj_set_size(v, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(v, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(v, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(v, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(v, 6, 0);
    lv_obj_set_style_pad_ver(v, 10, 0);
    s_wc.pick_view = v;

    lv_obj_t *title = aos_label(v, _("Ciudades"), aos_font_title, AOS_C_TEXT);
    lv_obj_set_style_pad_bottom(title, 6, 0);

    for (int i = 0; i < CITY_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(v);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 336, 46);
        lv_obj_set_style_radius(row, 14, 0);
        lv_obj_set_style_bg_color(row, AOS_C_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, pick_toggle_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *name = aos_label(row, _(CITIES[i].name), aos_font_body, AOS_C_TEXT);
        lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 16, 0);

        lv_obj_t *check = aos_label(row, LV_SYMBOL_OK, aos_font_body, AOS_C_CARD2);
        lv_obj_remove_flag(check, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(check, LV_ALIGN_RIGHT_MID, -16, 0);

        s_wc.pick_row[i]   = row;
        s_wc.pick_check[i] = check;
    }

    lv_obj_t *done = aos_button(v, _("Listo"), AOS_C_ACCENT, close_pick_cb, NULL);
    lv_obj_set_size(done, 180, 56);

    s_wc.pick_built = true;
}

static void show_pick(bool open)
{
    if (open && !s_wc.pick_built) {
        build_pick();
    }
    s_wc.pick_open = open;
    if (open) {
        lv_obj_add_flag(s_wc.list_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_wc.pick_view, LV_OBJ_FLAG_HIDDEN);
        refresh_checks();
    } else {
        lv_obj_remove_flag(s_wc.list_view, LV_OBJ_FLAG_HIDDEN);
        if (s_wc.pick_view) {
            lv_obj_add_flag(s_wc.pick_view, LV_OBJ_FLAG_HIDDEN);
        }
        refresh_rows();
    }
}

/* -------------------------------------------------------------------------- */
/* Life cycle                                                                  */

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    memset(&s_wc, 0, sizeof(s_wc));
    s_wc.last_min = -1;

    load_sel();

    s_wc.page = aos_page(root);
    build_list(s_wc.page);
    refresh_rows();
    return &s_wc;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    memset(&s_wc, 0, sizeof(s_wc));
}

static bool back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_wc.pick_open) {
        show_pick(false);
        return true;
    }
    return false;
}

static void tick(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_wc.pick_open || !s_wc.lbl_local_time) {
        return;
    }
    /* Once a minute: the other cities' times change with the one here, and
     * every refresh re-parses the zones' rules. */
    struct tm now;
    aos_hal_time_now(&now);
    if (now.tm_min != s_wc.last_min) {
        refresh_rows();
    }
}

void aos_app_worldclock_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.worldclock",
            .name     = "Reloj mundial",
            .icon_vec = AOS_ICON_GLOBE,
            .color_a  = 0x0A84FF,
            .color_b  = 0x0B3E7A,
            .order    = 35,
        },
        .create  = create,
        .destroy = destroy,
        .back    = back,
        .tick    = tick,
    };
}
