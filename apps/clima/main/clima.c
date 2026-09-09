/*
 * CLIMA - weather forecast for AmoledOS.
 *
 * It is the first dynamic app to use the network. Since an app loaded by
 * dlopen() has neither sockets nor a way to create tasks, the request is made
 * by the HAL in a separate task (aos_hal_http_get) and this app asks from its
 * tick, five times a second, whether it has arrived. The LVGL thread is never
 * blocked.
 *
 * Two views: the forecast and the city search. The location is stored in
 * preferences, so the app opens already knowing where it stands, and the last
 * good JSON is kept on the microSD so there is something to show while the
 * network answers (or when there is no network).
 *
 * Data from Open-Meteo (open-meteo.com), free and without a key.
 */
#include "aos_app.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"
#include "aos_theme.h"

#include "wx_api.h"
#include "wx_art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIG_ICON        104
#define SMALL_ICON       30
#define REFRESH_MS   900000     /* 15 minutes */
#define PAGE_W          368
#define PAGE_H          418     /* 448 minus the status bar */
#define RAIL_W          110     /* rail of the min-max bar */

typedef enum {
    VIEW_MAIN = 0,
    VIEW_SEARCH,
} view_t;

/* One column of the hourly strip and one row of the week. They are built once
 * and live until the app closes; the refreshes only change their contents. */
typedef struct {
    lv_obj_t *col, *hour, *img, *temp;
} hour_cell_t;

typedef struct {
    lv_obj_t *row, *name, *img, *pop, *min, *max, *rail, *bar;
} day_row_t;

typedef struct {
    view_t     view;

    /* data */
    wx_data_t  data;
    char       city[40];
    int32_t    lat10k, lon10k;
    bool       have_place;
    uint64_t   fetched_ms;      /* uptime of the last good data */
    bool       from_cache;

    /* network */
    int        req_wx;
    int        req_geo;

    /* main view */
    lv_obj_t  *main_view;
    lv_obj_t  *city_lbl;
    lv_obj_t  *big_img;
    lv_obj_t  *temp_lbl;
    lv_obj_t  *desc_lbl;
    lv_obj_t  *extra_lbl;
    lv_obj_t  *hour_strip;
    lv_obj_t  *day_box;
    hour_cell_t hour_cell[WX_HOURS];
    day_row_t   day_row[WX_DAYS];
    lv_obj_t  *foot_lbl;
    lv_obj_t  *hero;

    /* search */
    lv_obj_t  *search_view;
    lv_obj_t  *input;
    lv_obj_t  *keyboard;
    lv_obj_t  *result_box;
    lv_obj_t  *result_hint;
    lv_obj_t  *portal_lbl;
    wx_place_t places[WX_PLACES];
    int        place_count;
    int        pending_pick;    /* 1 + index, 0 = nothing pending */
    uint32_t   req_started;     /* uptime when the request was fired, to measure it */
    uint32_t   retry_ms;        /* when to retry, 0 = no retry
                                 * pending. Only used by the "no time yet"
                                 * case, which fixes itself. */
    bool       forced;          /* the location came from CLIMA_PLACE, not from NVS */
    uint32_t   pref_ms;         /* last re-read of the preferences */
    uint32_t   foot_ms;         /* last time the footer was repainted */

    /* sprites */
    wx_sprite_t big;
    wx_sprite_t small[WX_ICON_COUNT][2];
} clima_ctx_t;

/* lvgl.h does not drag in the image cache's header (it lives in
 * misc/cache/instance/), but the function is in the firmware's symbol table.
 * It is declared by hand so as not to depend on an internal LVGL path. */
extern void lv_image_cache_drop(const void *src);

static clima_ctx_t *s_ctx;

static void refresh_ui(void);
static void refresh_foot(void);
static void build_hour_cells(void);
static void build_day_rows(void);
static void start_fetch(bool force);
static void show_view(view_t view);

/* -------------------------------------------------------------------------- */
/* Sprites                                                                     */
/* -------------------------------------------------------------------------- */

/* The small ones are generated the first time they are asked for and kept: in
 * a normal week three or four of the sixteen possible ones are used. */
static const lv_image_dsc_t *small_icon(wx_icon_t icon, bool night)
{
    if (icon >= WX_ICON_COUNT) {
        icon = WX_ICON_CLOUDY;
    }
    wx_sprite_t *sp = &s_ctx->small[icon][night ? 1 : 0];
    if (!sp->data && !wx_art_make(sp, icon, night, SMALL_ICON)) {
        return NULL;
    }
    return &sp->dsc;
}

static void set_big_icon(wx_icon_t icon, bool night)
{
    if (s_ctx->big.data && s_ctx->big.icon == icon && s_ctx->big.night == night) {
        return;
    }
    /* The previous image may be in LVGL's cache pointing at this same
     * descriptor: without releasing it, the old icon would go on showing. */
    if (s_ctx->big.data) {
        lv_image_cache_drop(&s_ctx->big.dsc);
    }
    uint32_t t0 = (uint32_t)aos_hal_uptime_ms();
    if (wx_art_make(&s_ctx->big, icon, night, BIG_ICON)) {
        lv_image_set_src(s_ctx->big_img, &s_ctx->big.dsc);
    }
    aos_hal_log("clima", "icono %d (noche=%d) de %d px: %u ms", (int)icon,
                night ? 1 : 0, BIG_ICON, (unsigned)((uint32_t)aos_hal_uptime_ms() - t0));
}

/* -------------------------------------------------------------------------- */
/* Preferences                                                                 */
/* -------------------------------------------------------------------------- */

static void place_load(void)
{
    /* Development switches: on the board they do no harm and in the simulator
     * they save having to search for the city by hand on every start.
     *     CLIMA_PLACE="Buenos Aires,-34.6131,-58.3772"
     *     CLIMA_OFFLINE=1   does not touch the network, only what is stored */
    const char *forced = getenv("CLIMA_PLACE");
    if (forced && forced[0]) {
        char copy[96];
        snprintf(copy, sizeof(copy), "%s", forced);
        char *lat = strchr(copy, ',');
        char *lon = lat ? strchr(lat + 1, ',') : NULL;
        if (lat && lon) {
            *lat++ = 0;
            *lon++ = 0;
            /* copy clipped by hand: with snprintf, gcc warns about truncation
             * and the apps compile with -Werror */
            size_t n = strlen(copy);
            if (n >= sizeof(s_ctx->city)) {
                n = sizeof(s_ctx->city) - 1;
            }
            memcpy(s_ctx->city, copy, n);
            s_ctx->city[n] = 0;
            s_ctx->lat10k     = wx_deg_parse(lat);
            s_ctx->lon10k     = wx_deg_parse(lon);
            s_ctx->have_place = true;
            s_ctx->forced     = true;
            return;
        }
    }

    int32_t v = 0;
    bool ok = aos_hal_pref_get_i32("clima_lat", &v);
    s_ctx->lat10k = v;
    ok = aos_hal_pref_get_i32("clima_lon", &v) && ok;
    s_ctx->lon10k = v;

    s_ctx->city[0] = 0;
    aos_hal_pref_get_str("clima_city", s_ctx->city, sizeof(s_ctx->city));
    s_ctx->have_place = ok && s_ctx->city[0];
}

static void place_save(const wx_place_t *place)
{
    snprintf(s_ctx->city, sizeof(s_ctx->city), "%s", place->name);
    s_ctx->lat10k     = place->lat10k;
    s_ctx->lon10k     = place->lon10k;
    s_ctx->have_place = true;

    aos_hal_pref_set_i32("clima_lat", place->lat10k);
    aos_hal_pref_set_i32("clima_lon", place->lon10k);
    aos_hal_pref_set_str("clima_city", place->name);
}

/* -------------------------------------------------------------------------- */
/* Building the main view                                                      */
/* -------------------------------------------------------------------------- */

static lv_obj_t *card(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, PAGE_W - 24, height);
    lv_obj_set_style_bg_color(c, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_pad_all(c, 10, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static void search_btn_cb(lv_event_t *e)
{
    (void)e;
    show_view(VIEW_SEARCH);
}

static void refresh_btn_cb(lv_event_t *e)
{
    (void)e;
    start_fetch(true);
    refresh_ui();
}

static void build_main(lv_obj_t *parent)
{
    clima_ctx_t *ctx = s_ctx;

    ctx->main_view = lv_obj_create(parent);
    lv_obj_set_size(ctx->main_view, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->main_view, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(ctx->main_view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->main_view, 0, 0);
    lv_obj_set_style_pad_all(ctx->main_view, 0, 0);
    lv_obj_set_style_pad_row(ctx->main_view, 10, 0);
    lv_obj_set_flex_flow(ctx->main_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->main_view, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(ctx->main_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->main_view, LV_SCROLLBAR_MODE_OFF);

    /* --- header: city + the two buttons --- */
    lv_obj_t *head = lv_obj_create(ctx->main_view);
    lv_obj_set_size(head, PAGE_W, 40);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    ctx->city_lbl = aos_label(head, _("Clima"), aos_font_title, AOS_C_TEXT);
    lv_label_set_long_mode(ctx->city_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ctx->city_lbl, 220);
    lv_obj_align(ctx->city_lbl, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *find = lv_button_create(head);
    lv_obj_set_size(find, 44, 34);
    lv_obj_set_style_radius(find, 17, 0);
    lv_obj_set_style_bg_color(find, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_color(find, AOS_C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(find, 0, 0);
    lv_obj_align(find, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_event_cb(find, search_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *find_lbl = aos_label(find, LV_SYMBOL_LIST, aos_font_small, AOS_C_TEXT);
    lv_obj_center(find_lbl);

    lv_obj_t *again = lv_button_create(head);
    lv_obj_set_size(again, 44, 34);
    lv_obj_set_style_radius(again, 17, 0);
    lv_obj_set_style_bg_color(again, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_color(again, AOS_C_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(again, 0, 0);
    lv_obj_align(again, LV_ALIGN_RIGHT_MID, -60, 0);
    lv_obj_add_event_cb(again, refresh_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *again_lbl = aos_label(again, LV_SYMBOL_REFRESH, aos_font_small, AOS_C_TEXT);
    lv_obj_center(again_lbl);

    /* --- the large block: icon, temperature and description --- */
    ctx->hero = card(ctx->main_view, 150);
    lv_obj_set_style_bg_color(ctx->hero, lv_color_hex(0x1C1C1E), 0);

    ctx->big_img = lv_image_create(ctx->hero);
    lv_image_set_antialias(ctx->big_img, false);
    lv_obj_align(ctx->big_img, LV_ALIGN_LEFT_MID, -6, -12);

    ctx->temp_lbl = aos_label_boxed(ctx->hero, "--", aos_font_huge, AOS_C_TEXT, 160, 60);
    lv_obj_align(ctx->temp_lbl, LV_ALIGN_RIGHT_MID, 6, -22);

    ctx->desc_lbl = aos_label_boxed(ctx->hero, "", aos_font_body, AOS_C_TEXT,
                                    PAGE_W - 44, 24);
    lv_obj_align(ctx->desc_lbl, LV_ALIGN_BOTTOM_MID, 0, -18);

    ctx->extra_lbl = aos_label_boxed(ctx->hero, "", aos_font_small, AOS_C_DIM,
                                     PAGE_W - 44, 20);
    lv_obj_align(ctx->extra_lbl, LV_ALIGN_BOTTOM_MID, 0, 2);

    /* --- hourly strip, with horizontal scrolling --- */
    ctx->hour_strip = card(ctx->main_view, 96);
    lv_obj_set_flex_flow(ctx->hour_strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->hour_strip, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctx->hour_strip, 6, 0);
    lv_obj_add_flag(ctx->hour_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ctx->hour_strip, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(ctx->hour_strip, LV_SCROLLBAR_MODE_OFF);

    /* --- the seven days --- */
    ctx->day_box = card(ctx->main_view, 7 * 34 + 20);
    lv_obj_set_flex_flow(ctx->day_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ctx->day_box, 0, 0);

    ctx->foot_lbl = aos_label(ctx->main_view, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_style_pad_bottom(ctx->foot_lbl, 12, 0);

    build_hour_cells();
    build_day_rows();
}

/* -------------------------------------------------------------------------- */
/* Painting the data                                                           */
/* -------------------------------------------------------------------------- */

static const char *day_short(int wday)
{
    /* With context: here "Mar" is Tuesday, but on its own it could be March.
     * The same trap as in the firmware's aos_theme.c, and it is solved the same
     * way. */
    static const char *n[7] = { NC_("dia", "Dom"), NC_("dia", "Lun"),
                                NC_("dia", "Mar"), NC_("dia", "Mie"),
                                NC_("dia", "Jue"), NC_("dia", "Vie"),
                                NC_("dia", "Sab") };
    return C_("dia", n[wday % 7]);
}

/* The cells and the rows are built ONCE and after that only their text and
 * image are changed.
 *
 * The first version did an lv_obj_clean() and recreated the ~80 objects on
 * every refresh. On the Mac that is 1 ms and goes unnoticed; measured on the
 * board it is **120 ms with the LVGL thread blocked**, twice per opening and
 * again on every automatic update. It is the same lesson as gemas: what stands
 * still is left still. Generating the sprites was not the problem —they are
 * cached, and the cost did not drop on the second pass—, it was creating and
 * destroying objects. */

static void build_hour_cells(void)
{
    clima_ctx_t *ctx = s_ctx;

    for (int i = 0; i < WX_HOURS; i++) {
        hour_cell_t *c = &ctx->hour_cell[i];

        c->col = lv_obj_create(ctx->hour_strip);
        lv_obj_set_size(c->col, 46, 74);
        lv_obj_set_style_bg_opa(c->col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(c->col, 0, 0);
        lv_obj_set_style_pad_all(c->col, 0, 0);
        lv_obj_remove_flag(c->col, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(c->col, LV_OBJ_FLAG_CLICKABLE);

        c->hour = aos_label_boxed(c->col, "", aos_font_small, AOS_C_DIM, 46, 18);
        lv_obj_align(c->hour, LV_ALIGN_TOP_MID, 0, 0);

        c->img = lv_image_create(c->col);
        lv_image_set_antialias(c->img, false);
        lv_obj_align(c->img, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(c->img, LV_OBJ_FLAG_CLICKABLE);

        c->temp = aos_label_boxed(c->col, "", aos_font_small, AOS_C_TEXT, 46, 18);
        lv_obj_align(c->temp, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

static void build_day_rows(void)
{
    clima_ctx_t *ctx = s_ctx;

    for (int i = 0; i < WX_DAYS; i++) {
        day_row_t *r = &ctx->day_row[i];

        r->row = lv_obj_create(ctx->day_box);
        lv_obj_set_size(r->row, lv_pct(100), 34);
        lv_obj_set_style_bg_opa(r->row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(r->row, 0, 0);
        lv_obj_set_style_pad_all(r->row, 0, 0);
        lv_obj_remove_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(r->row, LV_OBJ_FLAG_CLICKABLE);

        r->name = aos_label_boxed(r->row, "", aos_font_body, AOS_C_DIM, 48, 24);
        lv_obj_align(r->name, LV_ALIGN_LEFT_MID, 2, 0);

        r->img = lv_image_create(r->row);
        lv_image_set_antialias(r->img, false);
        lv_obj_align(r->img, LV_ALIGN_LEFT_MID, 52, 0);
        lv_obj_remove_flag(r->img, LV_OBJ_FLAG_CLICKABLE);

        r->pop = aos_label_boxed(r->row, "", aos_font_small, AOS_C_TEAL, 40, 18);
        lv_obj_align(r->pop, LV_ALIGN_LEFT_MID, 86, 0);

        r->min = aos_label_boxed(r->row, "", aos_font_body, AOS_C_DIM, 34, 22);
        lv_obj_align(r->min, LV_ALIGN_LEFT_MID, 130, 0);

        r->max = aos_label_boxed(r->row, "", aos_font_body, AOS_C_TEXT, 34, 22);
        lv_obj_align(r->max, LV_ALIGN_RIGHT_MID, -2, 0);

        r->rail = lv_obj_create(r->row);
        lv_obj_set_size(r->rail, RAIL_W, 6);
        lv_obj_set_style_radius(r->rail, 3, 0);
        lv_obj_set_style_bg_color(r->rail, AOS_C_CARD2, 0);
        lv_obj_set_style_bg_opa(r->rail, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(r->rail, 0, 0);
        lv_obj_set_style_pad_all(r->rail, 0, 0);
        lv_obj_align(r->rail, LV_ALIGN_RIGHT_MID, -40, 0);
        aos_make_decorative(r->rail);

        r->bar = lv_obj_create(r->rail);
        lv_obj_set_size(r->bar, 8, 6);
        lv_obj_set_style_radius(r->bar, 3, 0);
        lv_obj_set_style_border_width(r->bar, 0, 0);
        lv_obj_set_style_pad_all(r->bar, 0, 0);
        lv_obj_set_style_bg_opa(r->bar, LV_OPA_COVER, 0);
        aos_make_decorative(r->bar);
    }
}

/* Shows or hides a whole cell, for when less data arrives than there are
 * slots. */
static void show_obj(lv_obj_t *obj, bool visible)
{
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void fill_hours(void)
{
    clima_ctx_t *ctx = s_ctx;
    const wx_data_t *d = &ctx->data;

    for (int i = 0; i < WX_HOURS; i++) {
        hour_cell_t *c = &ctx->hour_cell[i];
        if (i >= d->hours) {
            show_obj(c->col, false);
            continue;
        }
        show_obj(c->col, true);

        int32_t t = d->h_t[i];

        /* Daytime hours are told from night-time ones by the sunrise and
         * sunset that already come in the same JSON: without that, a sun icon
         * at three in the morning. */
        bool night = true;
        for (int k = 0; k < d->days; k++) {
            if (t >= d->d_sunrise[k] && t < d->d_sunset[k]) {
                night = false;
                break;
            }
        }

        char buf[16];
        snprintf(buf, sizeof(buf), "%02d", wx_hour(t, d->utc_offset));
        lv_label_set_text(c->hour, buf);
        lv_obj_set_style_text_color(c->hour, (i == 0) ? AOS_C_ACCENT : AOS_C_DIM, 0);

        const lv_image_dsc_t *src = small_icon(wx_icon_of(d->h_code[i]), night);
        show_obj(c->img, src != NULL);
        if (src) {
            lv_image_set_src(c->img, src);
        }

        char t_s[12];
        wx_temp_str(d->h_temp10[i], t_s, sizeof(t_s));
        snprintf(buf, sizeof(buf), "%s\xC2\xB0", t_s);
        lv_label_set_text(c->temp, buf);
    }
    lv_obj_scroll_to_x(ctx->hour_strip, 0, LV_ANIM_OFF);
}

static void fill_days(void)
{
    clima_ctx_t *ctx = s_ctx;
    const wx_data_t *d = &ctx->data;

    /* The week's extremes, so each day's bar reads as a comparison and not as
     * an ornament. */
    int wmin = d->d_min10[0], wmax = d->d_max10[0];
    for (int i = 1; i < d->days; i++) {
        if (d->d_min10[i] < wmin) wmin = d->d_min10[i];
        if (d->d_max10[i] > wmax) wmax = d->d_max10[i];
    }
    int span = wmax - wmin;
    if (span < 10) {
        span = 10;
    }

    for (int i = 0; i < WX_DAYS; i++) {
        day_row_t *r = &ctx->day_row[i];
        if (i >= d->days) {
            show_obj(r->row, false);
            continue;
        }
        show_obj(r->row, true);

        const char *name = (i == 0) ? _("Hoy")
                                    : day_short(wx_wday(d->d_t[i], d->utc_offset));
        lv_label_set_text(r->name, name);
        lv_obj_set_style_text_color(r->name, (i == 0) ? AOS_C_TEXT : AOS_C_DIM, 0);

        const lv_image_dsc_t *src = small_icon(wx_icon_of(d->d_code[i]), false);
        show_obj(r->img, src != NULL);
        if (src) {
            lv_image_set_src(r->img, src);
        }

        char pop[12] = "";
        if (d->d_pop[i] >= 10) {
            snprintf(pop, sizeof(pop), "%d%%", d->d_pop[i]);
        }
        lv_label_set_text(r->pop, pop);

        char mn[8], mx[8];
        wx_temp_str(d->d_min10[i], mn, sizeof(mn));
        wx_temp_str(d->d_max10[i], mx, sizeof(mx));
        lv_label_set_text(r->min, mn);
        lv_label_set_text(r->max, mx);

        int x0 = (d->d_min10[i] - wmin) * RAIL_W / span;
        int w  = (d->d_max10[i] - d->d_min10[i]) * RAIL_W / span;
        if (w < 8) {
            w = 8;
        }
        if (x0 + w > RAIL_W) {
            x0 = RAIL_W - w;
        }
        lv_obj_set_size(r->bar, w, 6);
        lv_obj_set_pos(r->bar, x0, 0);

        /* cold -> hot, with the day's maximum deciding the colour */
        lv_color_t col = (d->d_max10[i] >= 280) ? AOS_C_ORANGE :
                         (d->d_max10[i] >= 200) ? AOS_C_YELLOW :
                         (d->d_max10[i] >= 120) ? AOS_C_GREEN  : AOS_C_TEAL;
        lv_obj_set_style_bg_color(r->bar, col, 0);
    }
}


static void refresh_ui(void)
{
    clima_ctx_t *ctx = s_ctx;
    const wx_data_t *d = &ctx->data;

    lv_label_set_text(ctx->city_lbl, ctx->have_place ? ctx->city : _("Clima"));

    if (!d->ok) {
        lv_label_set_text(ctx->temp_lbl, "--");
        lv_label_set_text(ctx->desc_lbl,
                          ctx->have_place ? _("Sin datos todavia") : _("Elegi una ciudad"));
        lv_label_set_text(ctx->extra_lbl, "");
        set_big_icon(WX_ICON_CLOUDY, false);
        /* With a zeroed context there are neither hours nor days, so this
         * leaves every cell hidden instead of showing the remains of the
         * previous location; and both cards hide themselves entirely, because
         * empty they are just two grey rectangles. */
        fill_hours();
        fill_days();
        show_obj(ctx->hour_strip, false);
        show_obj(ctx->day_box, false);
    } else {
        show_obj(ctx->hour_strip, true);
        show_obj(ctx->day_box, true);
        char t[12];
        wx_temp_str(d->temp10, t, sizeof(t));
        char buf[32];
        snprintf(buf, sizeof(buf), "%s\xC2\xB0", t);
        lv_label_set_text(ctx->temp_lbl, buf);

        lv_label_set_text(ctx->desc_lbl, wx_text(d->code));

        char feels[12];
        wx_temp_str(d->feels10, feels, sizeof(feels));
        char extra[80];
        /* Montserrat only brings ASCII and a few symbols: the middle dot
         * (U+00B7) comes out as a little box, so they are separated with
         * spaces. The degree sign IS there, and it is two-byte UTF-8:
         * "\xC2\xB0". */
        snprintf(extra, sizeof(extra),
                 _("ST %s\xC2\xB0     HUM %d%%     %d km/h"),
                 feels, d->hum, (d->wind10 + 5) / 10);
        lv_label_set_text(ctx->extra_lbl, extra);

        set_big_icon(wx_icon_of(d->code), d->is_day == 0);
        lv_obj_set_style_bg_color(ctx->hero,
                                  lv_color_hex(wx_art_mood(wx_icon_of(d->code),
                                                           d->is_day == 0)), 0);
        uint32_t t0 = (uint32_t)aos_hal_uptime_ms();
        fill_hours();
        fill_days();
        aos_hal_log("clima", "hour strip and week redrawn in %u ms",
                    (unsigned)((uint32_t)aos_hal_uptime_ms() - t0));
    }

    refresh_foot();
}

/* The footer is a single label and ages on its own, so it has a function of
 * its own: repainting it must NOT cost what rebuilding the hourly strip and
 * the week costs, which on the board is 120 ms. */
static void refresh_foot(void)
{
    clima_ctx_t *ctx = s_ctx;
    const wx_data_t *d = &ctx->data;

    char foot[80];
    if (ctx->req_wx > 0 && aos_hal_http_state(ctx->req_wx) == AOS_HTTP_BUSY) {
        snprintf(foot, sizeof(foot), "%s", _("actualizando..."));
    } else if (!d->ok) {
        snprintf(foot, sizeof(foot), "%s",
                 aos_hal_net_state() == AOS_NET_CONNECTED ? _("sin datos")
                                                          : _("sin conexion"));
    } else {
        uint32_t age_s = (uint32_t)(aos_hal_uptime_ms() - ctx->fetched_ms) / 1000;
        const char *src = ctx->from_cache ? _("guardado") : "open-meteo";
        if (age_s < 90) {
            snprintf(foot, sizeof(foot), _("%s   -   recien"), src);
        } else {
            snprintf(foot, sizeof(foot), _("%s   -   hace %d min"),
                     src, (int)(age_s / 60));
        }
    }
    lv_label_set_text(ctx->foot_lbl, foot);
}

/* -------------------------------------------------------------------------- */
/* Network                                                                     */
/* -------------------------------------------------------------------------- */

static void start_fetch(bool force)
{
    clima_ctx_t *ctx = s_ctx;
    if (!ctx->have_place || ctx->req_wx > 0) {
        return;
    }
    if (!force && ctx->data.ok &&
        (uint32_t)(aos_hal_uptime_ms() - ctx->fetched_ms) < REFRESH_MS) {
        return;
    }
    const char *offline = getenv("CLIMA_OFFLINE");
    if (aos_hal_net_state() != AOS_NET_CONNECTED ||
        (offline && offline[0] && offline[0] != '0')) {
        return;
    }
    ctx->req_wx      = wx_fetch_start(ctx->lat10k, ctx->lon10k);
    ctx->req_started = (uint32_t)aos_hal_uptime_ms();
    ctx->retry_ms    = 0;
    aos_hal_log("clima", "asking for the weather in %s (%ld, %ld) -> id %d", ctx->city,
                (long)ctx->lat10k, (long)ctx->lon10k, ctx->req_wx);
}

/* The forecast's response arrives. */
static void poll_weather(void)
{
    clima_ctx_t *ctx = s_ctx;
    if (ctx->req_wx <= 0) {
        return;
    }
    aos_http_state_t st = aos_hal_http_state(ctx->req_wx);
    if (st == AOS_HTTP_BUSY) {
        return;
    }

    uint32_t took = (uint32_t)aos_hal_uptime_ms() - ctx->req_started;
    if (st == AOS_HTTP_DONE) {
        const char *body = aos_hal_http_body(ctx->req_wx);
        int len = aos_hal_http_len(ctx->req_wx);
        wx_data_t fresh;
        bool ok = body && wx_parse(body, len, &fresh);
        aos_hal_log("clima", "respuesta HTTP %d, %d bytes en %u ms, parseo %s",
                    aos_hal_http_status(ctx->req_wx), len, (unsigned)took,
                    ok ? "ok" : "FALLIDO");
        if (ok) {
            ctx->data       = fresh;
            ctx->fetched_ms = aos_hal_uptime_ms();
            ctx->from_cache = false;
            wx_cache_save(body, len, ctx->lat10k, ctx->lon10k);
            aos_hal_log("clima", "%d dias, %d horas, ahora %d.%d C codigo %d",
                        fresh.days, fresh.hours, fresh.temp10 / 10,
                        abs(fresh.temp10 % 10), fresh.code);
        } else if (body) {
            /* The first few bytes are usually enough to see that it really answered */
            aos_hal_log("clima", "cuerpo: %.80s", body);
        }
    } else {
        int motivo = aos_hal_http_status(ctx->req_wx);
        aos_hal_log("clima", "the request failed: status %d, reason %d, %u ms",
                    (int)st, motivo, (unsigned)took);

        /* Since the queries go over https there is a failure that fixes ITSELF,
         * and telling the user "could not update" would be lying to them: the
         * freshly powered board has not synchronised its clock over SNTP yet,
         * and with no time there is no way to know whether the certificate is
         * still valid. It is a few seconds. It retries instead of leaving the
         * screen on "no data" until somebody opens the app again —which is what
         * would happen, because the tick only asks again when there ALREADY is
         * data—. */
        if (motivo == AOS_HTTP_ERR_SIN_HORA) {
            ctx->retry_ms = (uint32_t)aos_hal_uptime_ms() + 3000;
            aos_hal_log("clima", "still no clock; retrying in 3 s");
        } else {
            aos_ui_toast(_("No se pudo actualizar"), 1500);
        }
    }

    aos_hal_http_release(ctx->req_wx);
    ctx->req_wx = 0;
    refresh_ui();
}

/* -------------------------------------------------------------------------- */
/* City search                                                                 */
/* -------------------------------------------------------------------------- */

/* With six results and the keyboard up only three are visible. When the search
 * brings something back, the keyboard goes and the list fills the screen; it
 * comes back as soon as the text field is touched. */
static void keyboard_show(bool show)
{
    clima_ctx_t *ctx = s_ctx;
    if (show) {
        lv_obj_remove_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(ctx->result_box, 138);
    } else {
        lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(ctx->result_box, PAGE_H - 80);
    }
}

static void input_click_cb(lv_event_t *e)
{
    (void)e;
    keyboard_show(true);
}

/* It is noted and resolved on the next tick. Choosing a city means emptying
 * the results list, and this row is precisely one of its children: deleting it
 * from its own callback is destroying the object dispatching the event. It is
 * the same reason aos_ui_back() is not called from a callback either. */
static void place_pick_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= s_ctx->place_count) {
        return;
    }
    s_ctx->pending_pick = index + 1;
    aos_hal_beep(1800, 25);
}

static void apply_pending_pick(void)
{
    clima_ctx_t *ctx = s_ctx;
    int index = ctx->pending_pick - 1;
    ctx->pending_pick = 0;
    if (index < 0 || index >= ctx->place_count) {
        return;
    }
    place_save(&ctx->places[index]);

    /* new location: what was on screen no longer applies */
    memset(&ctx->data, 0, sizeof(ctx->data));
    ctx->from_cache = false;

    show_view(VIEW_MAIN);
    start_fetch(true);
    refresh_ui();
}

static void fill_results(void)
{
    clima_ctx_t *ctx = s_ctx;
    lv_obj_clean(ctx->result_box);

    for (int i = 0; i < ctx->place_count; i++) {
        const wx_place_t *p = &ctx->places[i];

        lv_obj_t *row = lv_obj_create(ctx->result_box);
        lv_obj_set_size(row, lv_pct(100), 42);
        lv_obj_set_style_bg_color(row, AOS_C_CARD, 0);
        lv_obj_set_style_bg_color(row, AOS_C_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, place_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *name = aos_label(row, p->name, aos_font_body, AOS_C_TEXT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, -8);
        aos_make_decorative(name);

        char sub[80];
        if (p->region[0]) {
            snprintf(sub, sizeof(sub), "%s, %s", p->region, p->country);
        } else {
            snprintf(sub, sizeof(sub), "%s", p->country);
        }
        lv_obj_t *meta = aos_label(row, sub, aos_font_small, AOS_C_DIM);
        lv_obj_align(meta, LV_ALIGN_LEFT_MID, 12, 11);
        aos_make_decorative(meta);
    }
}

static void poll_search(void)
{
    clima_ctx_t *ctx = s_ctx;
    if (ctx->req_geo <= 0) {
        return;
    }
    aos_http_state_t st = aos_hal_http_state(ctx->req_geo);
    if (st == AOS_HTTP_BUSY) {
        return;
    }

    ctx->place_count = 0;
    int len = 0;
    if (st == AOS_HTTP_DONE) {
        const char *body = aos_hal_http_body(ctx->req_geo);
        len = aos_hal_http_len(ctx->req_geo);
        if (body) {
            ctx->place_count = wx_places_parse(body, len, ctx->places, WX_PLACES);
        }
    }
    /* The length is read before the release: afterwards the identifier is no longer valid */
    aos_hal_log("clima", "busqueda: estado %d, %d bytes, %d resultados",
                (int)st, len, ctx->place_count);
    aos_hal_http_release(ctx->req_geo);
    ctx->req_geo = 0;

    fill_results();
    keyboard_show(ctx->place_count == 0);
    lv_label_set_text(ctx->result_hint,
                      ctx->place_count ? "" :
                      (st == AOS_HTTP_DONE ? _("No hay ninguna ciudad asi")
                                           : _("No se pudo buscar")));
}

static void search_go(void)
{
    clima_ctx_t *ctx = s_ctx;
    const char *text = lv_textarea_get_text(ctx->input);
    if (!text || strlen(text) < 2) {
        lv_label_set_text(ctx->result_hint, _("Escribi al menos dos letras"));
        return;
    }
    if (aos_hal_net_state() != AOS_NET_CONNECTED) {
        lv_label_set_text(ctx->result_hint, _("Sin conexion a la red"));
        return;
    }
    if (ctx->req_geo > 0) {
        return;
    }

    ctx->req_geo = wx_search_start(text);
    lv_obj_clean(ctx->result_box);
    ctx->place_count = 0;
    lv_label_set_text(ctx->result_hint,
                      ctx->req_geo > 0 ? _("Buscando...") : _("No se pudo buscar"));
}

static void input_ready_cb(lv_event_t *e)
{
    (void)e;
    search_go();
}

static void build_search(lv_obj_t *parent)
{
    clima_ctx_t *ctx = s_ctx;

    ctx->search_view = lv_obj_create(parent);
    lv_obj_set_size(ctx->search_view, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ctx->search_view, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(ctx->search_view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->search_view, 0, 0);
    lv_obj_set_style_pad_all(ctx->search_view, 0, 0);
    lv_obj_remove_flag(ctx->search_view, LV_OBJ_FLAG_SCROLLABLE);

    ctx->input = lv_textarea_create(ctx->search_view);
    lv_textarea_set_one_line(ctx->input, true);
    lv_textarea_set_placeholder_text(ctx->input, _("Ciudad"));
    lv_obj_set_size(ctx->input, PAGE_W - 24, 40);
    lv_obj_align(ctx->input, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(ctx->input, AOS_C_CARD, 0);
    lv_obj_set_style_border_width(ctx->input, 0, 0);
    lv_obj_set_style_radius(ctx->input, 12, 0);
    lv_obj_set_style_text_color(ctx->input, AOS_C_TEXT, 0);
    lv_obj_set_style_text_font(ctx->input, aos_font_body, 0);
    lv_obj_add_event_cb(ctx->input, input_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(ctx->input, input_click_cb, LV_EVENT_CLICKED, NULL);

    ctx->result_hint = aos_label(ctx->search_view, _("Buscar una ciudad"),
                                 aos_font_small, AOS_C_DIM);
    lv_obj_align(ctx->result_hint, LV_ALIGN_TOP_MID, 0, 52);

    ctx->result_box = lv_obj_create(ctx->search_view);
    lv_obj_set_size(ctx->result_box, PAGE_W - 24, 138);
    lv_obj_align(ctx->result_box, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_opa(ctx->result_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctx->result_box, 0, 0);
    lv_obj_set_style_pad_all(ctx->result_box, 0, 0);
    lv_obj_set_style_pad_row(ctx->result_box, 6, 0);
    lv_obj_set_flex_flow(ctx->result_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(ctx->result_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->result_box, LV_SCROLLBAR_MODE_OFF);

    /* The 368 px keyboard is awkward and unnecessary: the watch's web portal
     * has the same search, with the computer's keyboard. */
    ctx->portal_lbl = aos_label_boxed(ctx->search_view, "", aos_font_small,
                                      AOS_C_DIM, PAGE_W - 24, 20);
    lv_obj_align(ctx->portal_lbl, LV_ALIGN_TOP_MID, 0, PAGE_H - 226);

    ctx->keyboard = lv_keyboard_create(ctx->search_view);
    lv_obj_set_size(ctx->keyboard, PAGE_W, 200);
    lv_obj_align(ctx->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(ctx->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(ctx->keyboard, ctx->input);
    lv_obj_set_style_text_font(ctx->keyboard, aos_font_body, 0);
    lv_obj_set_style_bg_color(ctx->keyboard, AOS_C_BG, 0);
    lv_obj_set_style_bg_color(ctx->keyboard, AOS_C_CARD, LV_PART_ITEMS);
    lv_obj_set_style_text_color(ctx->keyboard, AOS_C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(ctx->keyboard, 0, 0);
    lv_obj_set_style_border_width(ctx->keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(ctx->keyboard, 8, LV_PART_ITEMS);
}

/* -------------------------------------------------------------------------- */
/* Navigation                                                                  */
/* -------------------------------------------------------------------------- */

static void show_view(view_t view)
{
    clima_ctx_t *ctx = s_ctx;
    ctx->view = view;

    if (view == VIEW_MAIN) {
        lv_obj_remove_flag(ctx->main_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->search_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->main_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ctx->search_view, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(ctx->input, "");
        lv_obj_clean(ctx->result_box);
        ctx->place_count = 0;
        keyboard_show(true);
        lv_label_set_text(ctx->result_hint,
                          ctx->have_place ? _("Buscar otra ciudad")
                                          : _("Buscar tu ciudad"));

        char donde[80];
        if (aos_hal_net_state() == AOS_NET_CONNECTED) {
            snprintf(donde, sizeof(donde), _("o desde http://%s/clima"),
                     aos_hal_net_ip());
        } else {
            snprintf(donde, sizeof(donde), "%s", _("sin conexion a la red"));
        }
        lv_label_set_text(ctx->portal_lbl, donde);
    }
}

static bool clima_back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_ctx && s_ctx->view == VIEW_SEARCH) {
        show_view(VIEW_MAIN);
        return true;                    /* consumed: the app is not left */
    }
    return false;
}

/* The location is also chosen from the web portal, which writes the same
 * preference keys. If that happens with the app open, we have to find out: the
 * coordinates are compared every three seconds, which is far cheaper than any
 * notification mechanism between the firmware and a .so. */
static void watch_prefs(void)
{
    clima_ctx_t *ctx = s_ctx;
    if (ctx->forced) {
        return;                 /* CLIMA_PLACE overrides everything */
    }
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    if (now - ctx->pref_ms < 3000) {
        return;
    }
    ctx->pref_ms = now;

    int32_t lat = 0, lon = 0;
    if (!aos_hal_pref_get_i32("clima_lat", &lat) ||
        !aos_hal_pref_get_i32("clima_lon", &lon)) {
        return;
    }
    if (lat == ctx->lat10k && lon == ctx->lon10k) {
        return;
    }

    ctx->lat10k = lat;
    ctx->lon10k = lon;
    ctx->city[0] = 0;
    aos_hal_pref_get_str("clima_city", ctx->city, sizeof(ctx->city));
    ctx->have_place = ctx->city[0] != 0;
    memset(&ctx->data, 0, sizeof(ctx->data));
    ctx->from_cache = false;

    aos_hal_log("clima", "the portal changed the place to '%s' (%ld, %ld)",
                ctx->city, (long)lat, (long)lon);
    if (ctx->view == VIEW_SEARCH) {
        show_view(VIEW_MAIN);
    }
    start_fetch(true);
    refresh_ui();
    aos_ui_toast(ctx->city, 1200);
}

/* -------------------------------------------------------------------------- */
/* Demonstration data                                                          */
/* -------------------------------------------------------------------------- */

/* Walks the eight icons across the seven days and the twelve hours, so all the
 * art can be looked at in one pass and without depending on it raining today.
 * 'code' forces the current weather; 0 uses any old one. */
static void demo_fill(wx_data_t *d, int code)
{
    static const int codes[8] = { 0, 2, 3, 45, 53, 63, 73, 95 };

    memset(d, 0, sizeof(*d));
    d->ok         = true;
    d->utc_offset = -3 * 3600;
    d->now_t      = 1788084884;         /* any old morning */
    d->temp10     = 187;
    d->feels10    = 172;
    d->hum        = 64;
    d->wind10     = 143;
    d->code       = (code > 1) ? code : 2;
    d->is_day     = 1;

    d->days = WX_DAYS;
    for (int i = 0; i < WX_DAYS; i++) {
        d->d_t[i]       = d->now_t - 3600 * 8 + i * 86400;
        d->d_code[i]    = codes[i % 8];
        d->d_max10[i]   = 150 + (i * 47) % 190;
        d->d_min10[i]   = 40 + (i * 31) % 90;
        d->d_pop[i]     = (i * 17) % 100;
        d->d_sunrise[i] = d->d_t[i] + 7 * 3600;
        d->d_sunset[i]  = d->d_t[i] + 19 * 3600;
    }

    d->hours = WX_HOURS;
    for (int i = 0; i < WX_HOURS; i++) {
        d->h_t[i]      = d->now_t + i * 3600;
        d->h_code[i]   = codes[(i + 1) % 8];
        d->h_temp10[i] = 120 + (i * 23) % 110;
        d->h_pop[i]    = (i * 13) % 100;
    }
}

/* -------------------------------------------------------------------------- */
/* Life cycle                                                                  */
/* -------------------------------------------------------------------------- */

static void clima_tick(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (!s_ctx) {
        return;
    }
    if (s_ctx->pending_pick) {
        apply_pending_pick();
        return;
    }
    poll_weather();
    poll_search();
    watch_prefs();

    /* The retry for the missing time. It goes up here and not in the footer's
     * block below because that one only runs when there already is data, which
     * is precisely what there is not in this case. */
    if (s_ctx->retry_ms &&
        (int32_t)((uint32_t)aos_hal_uptime_ms() - s_ctx->retry_ms) >= 0) {
        s_ctx->retry_ms = 0;
        start_fetch(true);
    }

    /* The footer ages on its own; it is repainted once a minute, not on every
     * tick. */
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    if (s_ctx->view == VIEW_MAIN && s_ctx->data.ok && now - s_ctx->foot_ms > 60000) {
        s_ctx->foot_ms = now;
        refresh_foot();
        start_fetch(false);
    }
}

static void *clima_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    clima_ctx_t *ctx = lv_malloc_zeroed(sizeof(clima_ctx_t));
    if (!ctx) {
        return NULL;
    }
    s_ctx = ctx;

    /* Without this, foot_ms is zero and the first tick believes the footer is
     * as old as the system's whole uptime: it fires one extra full repaint
     * every time the app opens. */
    ctx->foot_ms = (uint32_t)aos_hal_uptime_ms();
    ctx->pref_ms = ctx->foot_ms;

    lv_obj_set_style_bg_color(root, AOS_C_BG, 0);
    lv_obj_set_style_pad_all(root, 0, 0);

    build_main(root);
    build_search(root);

    place_load();

    /* What is stored comes first: something is seen before the network
     * answers. The copy has its coordinates baked in, so if the location
     * changed it discards itself and the previous city's forecast is not
     * shown. */
    if (ctx->have_place) {
        char *cached = malloc(WX_BUF_BYTES);
        if (cached) {
            int n = wx_cache_load(cached, WX_BUF_BYTES, ctx->lat10k, ctx->lon10k);
            if (n > 0 && wx_parse(cached, n, &ctx->data)) {
                ctx->from_cache = true;
                ctx->fetched_ms = aos_hal_uptime_ms();
            }
            free(cached);
        }
    }

    /* More development switches, all harmless on the board:
     *     CLIMA_DEMO=1     invented data walking the eight icons
     *     CLIMA_VIEW=search  opens straight into the search
     *     CLIMA_SCROLL=240   starts with the view scrolled, for screenshots */
    const char *demo = getenv("CLIMA_DEMO");
    if (demo && demo[0] && demo[0] != '0') {
        demo_fill(&ctx->data, atoi(demo));
        ctx->fetched_ms = aos_hal_uptime_ms();
        ctx->have_place = true;
        if (!ctx->city[0]) {
            snprintf(ctx->city, sizeof(ctx->city), "%s", _("Demostracion"));
        }
    }

    const char *view = getenv("CLIMA_VIEW");
    const char *query = getenv("CLIMA_SEARCH");
    show_view((view && view[0] == 's') || (query && query[0]) ? VIEW_SEARCH :
              (ctx->have_place ? VIEW_MAIN : VIEW_SEARCH));

    /* CLIMA_SEARCH="Roma" leaves the search done on opening: it avoids having
     * to type on the on-screen keyboard for every test. */
    if (query && query[0]) {
        lv_textarea_set_text(ctx->input, query);
        search_go();
    }
    refresh_ui();
    if (!demo || !demo[0] || demo[0] == '0') {
        start_fetch(true);
    }

    const char *scroll = getenv("CLIMA_SCROLL");
    if (scroll && atoi(scroll) > 0) {
        lv_obj_scroll_to_y(ctx->main_view, atoi(scroll), LV_ANIM_OFF);
    }
    return ctx;
}

static void clima_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    clima_ctx_t *ctx = inst;
    if (!ctx) {
        return;
    }

    /* In-flight requests are released: the HAL's task finishes on its own and
     * frees its buffer, but the app's context ceases to exist here. */
    if (ctx->req_wx > 0)  aos_hal_http_release(ctx->req_wx);
    if (ctx->req_geo > 0) aos_hal_http_release(ctx->req_geo);

    wx_art_free(&ctx->big);
    for (int i = 0; i < WX_ICON_COUNT; i++) {
        wx_art_free(&ctx->small[i][0]);
        wx_art_free(&ctx->small[i][1]);
    }

    s_ctx = NULL;
    lv_free(ctx);
}

static bool clima_init(aos_app_t *app)
{
    app->desc.id       = "aos.clima";
    app->desc.name     = "Clima";
    app->desc.icon     = LV_SYMBOL_REFRESH;
    app->desc.icon_vec = AOS_ICON_WEATHER;
    app->desc.color_a  = 0x0A84FF;
    app->desc.color_b  = 0x00C2D1;
    app->desc.order    = 60;

    app->create  = clima_create;
    app->destroy = clima_destroy;
    app->back    = clima_back;
    app->tick    = clima_tick;
    return true;
}

AOS_APP_ENTRY(clima_init);
