/*
 * BLE - the radar, and the finder.
 *
 * The radar puts each device heard in the last 30 seconds at its estimated
 * distance (log-distance path loss, bl_distance_m, with the exponent of the
 * surroundings chosen in the settings) on a logarithmic scale from 30 cm to
 * 30 m. Bluetooth says nothing of direction: the angle is the address's
 * hash, so a device keeps its place while the radar is open. The sweep is
 * decoration with a purpose: a dot lights up as it passes and dims after,
 * which shows at a glance who is being heard right now.
 *
 * The finder is one device and nothing else: its signal in big numbers,
 * smoothed, whether it is getting stronger or weaker, the last minute as
 * bars, and a beep that comes faster the closer it is, like a Geiger
 * counter, to walk towards a lost tag.
 */
#include "bl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define DOTS_MAX  96
#define D_MIN     0.3f
#define D_MAX     30.0f

static struct {
    lv_obj_t *canvas, *info, *i_icon, *i_glyph, *i_name, *i_sub, *i_rssi, *btns;
    int32_t cx, cy, R;
    int ndots;
    int16_t dx[DOTS_MAX], dy[DOTS_MAX];
    int16_t ddev[DOTS_MAX];
    uint32_t sweep;                 /* tenths of a degree */
} R;

static struct {
    lv_obj_t *big, *unit, *trend, *dist, *graph, *gone, *snd;
    float smooth;
    bool has;
    uint32_t last_beep;
} F;

static uint32_t hash_addr(const uint8_t a[6])
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) h = (h ^ a[i]) * 16777619u;
    return h;
}

static float radius_of(float d)
{
    if (d < D_MIN) d = D_MIN;
    if (d > D_MAX) d = D_MAX;
    return log10f(d / D_MIN) / log10f(D_MAX / D_MIN);
}

static float dev_distance(const bl_dev_t *d)
{
    int p1m = bl_p1m_guess(&d->ad, d->has_bc ? &d->bc : NULL);
    return bl_distance_m((int)lroundf_safe(d->rssi_avg), p1m, bl_env_n());
}

/* ---- drawing ---- */

static void radar_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int32_t cx = a.x1 + R.cx, cy = a.y1 + R.cy, rr = R.R;

    /* the rings and their distances */
    static const float RINGS[] = { 1, 2, 5, 10, 20 };
    for (int i = 0; i < 5; i++) {
        int32_t r = (int32_t)(radius_of(RINGS[i]) * rr);
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = cx;
        d.center.y = cy;
        d.radius = (uint16_t)r;
        d.width = 2;
        d.start_angle = 0;
        d.end_angle = 360;
        d.color = BL_C;
        d.opa = LV_OPA_40;
        lv_draw_arc(layer, &d);
        char t[12];
        snprintf(t, sizeof t, "%d m", (int)RINGS[i]);
        bl_draw_text(layer, t, aos_font_tiny, AOS_C_DIM, cx + 4, cy - r - 16, 50, LV_TEXT_ALIGN_LEFT);
    }
    {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = cx;
        d.center.y = cy;
        d.radius = (uint16_t)rr;
        d.width = 3;
        d.start_angle = 0;
        d.end_angle = 360;
        d.color = BL_C;
        d.opa = LV_OPA_70;
        lv_draw_arc(layer, &d);
    }
    bl_draw_line(layer, cx - rr, cy, cx + rr, cy, BL_C, 1, LV_OPA_20);
    bl_draw_line(layer, cx, cy - rr, cx, cy + rr, BL_C, 1, LV_OPA_20);

    /* the sweep: a wedge behind the beam */
    int sweep = (int)(R.sweep / 10);
    for (int k = 0; k < 3; k++) {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = cx;
        d.center.y = cy;
        d.radius = (uint16_t)rr;
        d.width = (uint16_t)rr;
        d.start_angle = (sweep - (k + 1) * 14 + 720) % 360;
        d.end_angle = (sweep - k * 14 + 720) % 360;
        d.color = BL_C;
        d.opa = (lv_opa_t)(54 - k * 16);
        lv_draw_arc(layer, &d);
    }
    float ang = sweep * 3.14159265f / 180.0f;
    bl_draw_line(layer, cx, cy, cx + (int32_t)(cosf(ang) * rr), cy + (int32_t)(sinf(ang) * rr), lv_color_hex(0x93C5FD), 2, LV_OPA_90);

    /* the watch in the middle */
    bl_fill(layer, cx - 5, cy - 5, cx + 5, cy + 5, AOS_C_TEXT, LV_OPA_COVER, LV_RADIUS_CIRCLE);

    /* the devices: the strongest are drawn last, on top */
    static int idx[BL_DEV_MAX];
    int n = bl_sorted(idx, BL_DEV_MAX, BL_FILT_ALL, BL_SORT_RSSI);
    R.ndots = 0;
    int labels = 0;
    for (int k = n - 1; k >= 0; k--) {
        const bl_dev_t *d = &BL.dev[idx[k]];
        if (!bl_alive(d) || R.ndots >= DOTS_MAX) continue;
        float dist = dev_distance(d);
        float rf = radius_of(dist) * rr;
        uint32_t h = hash_addr(d->addr);
        int deg = (int)(h % 360);
        float th = deg * 3.14159265f / 180.0f;
        int32_t x = cx + (int32_t)(cosf(th) * rf), y = cy + (int32_t)(sinf(th) * rf);
        /* afterglow: brightest just after the beam went by */
        int behind = (sweep - deg + 360) % 360;
        int glow = 255 - behind * 150 / 360;
        uint32_t quiet = (uint32_t)aos_hal_uptime_ms() - d->last_ms;
        if (quiet > 5000) glow = glow * 2 / 5;
        bool sel = idx[k] == BL.sel;
        int32_t s = sel ? 8 : d->fav ? 7 : 5;
        lv_color_t col = bl_class_color(d->cls);
        if (sel) bl_fill(layer, x - s - 5, y - s - 5, x + s + 5, y + s + 5, AOS_C_TEXT, LV_OPA_40, LV_RADIUS_CIRCLE);
        bl_fill(layer, x - s, y - s, x + s, y + s, col, (lv_opa_t)glow, LV_RADIUS_CIRCLE);
        R.dx[R.ndots] = (int16_t)(x - a.x1);
        R.dy[R.ndots] = (int16_t)(y - a.y1);
        R.ddev[R.ndots] = (int16_t)idx[k];
        R.ndots++;
        /* names for the nearest few, the favourites and the one picked */
        if (sel || d->fav || (k < 4 && labels < 4)) {
            labels++;
            bl_draw_text(layer, bl_dev_name(d), aos_font_tiny, sel ? AOS_C_TEXT : AOS_C_DIM, x + s + 4, y - 8, 110,
                         LV_TEXT_ALIGN_LEFT);
        }
    }
}

static void info_refresh(void)
{
    if (!R.info) return;
    if (BL.sel < 0 || BL.sel >= BL.ndev) {
        lv_label_set_text(R.i_name, _("Tocá un punto"));
        lv_label_set_text(R.i_sub, _("Distancia estimada por la señal; el ángulo no dice nada."));
        lv_label_set_text(R.i_rssi, "");
        lv_obj_add_flag(R.btns, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(R.i_icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const bl_dev_t *d = &BL.dev[BL.sel];
    lv_obj_remove_flag(R.btns, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(R.i_icon, LV_OBJ_FLAG_HIDDEN);
    lv_color_t cc = bl_class_color(d->cls);
    lv_obj_set_style_bg_color(R.i_icon, cc, 0);
    lv_obj_set_style_text_color(R.i_glyph, cc, 0);
    lv_label_set_text(R.i_glyph, bl_class_glyph(d->cls));
    lv_label_set_text(R.i_name, bl_dev_name(d));
    char sub[140], dist[16];
    bl_fmt_num(dist, sizeof dist, dev_distance(d), dev_distance(d) < 10 ? 1 : 0);
    char s2[100];
    bl_dev_sub(d, s2, sizeof s2);
    snprintf(sub, sizeof sub, _("~%s m · %s"), dist, s2);
    lv_label_set_text(R.i_sub, sub);
    char r[16];
    snprintf(r, sizeof r, "%d", bl_alive(d) ? d->rssi : 0);
    lv_label_set_text(R.i_rssi, bl_alive(d) ? r : "--");
    lv_obj_set_style_text_color(R.i_rssi, bl_rssi_color(d->rssi), 0);
}

void bl_radar_refresh(void)
{
    if (!R.canvas) return;
    /* five frames a second on the watch, where a full redraw of the radar
     * costs what twenty do in the simulator: 12 degrees a frame, a turn in 6 s */
    if (BL.ticks % 2) return;
    R.sweep = (R.sweep + 120) % 3600;
    lv_obj_invalidate(R.canvas);
    if (BL.ticks % 10 == 0) info_refresh();
}

static void radar_tap(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    int32_t x = p.x - a.x1, y = p.y - a.y1;
    int best = -1;
    int32_t bd = 34 * 34;
    for (int i = 0; i < R.ndots; i++) {
        int32_t ddx = R.dx[i] - x, ddy = R.dy[i] - y;
        int32_t d2 = ddx * ddx + ddy * ddy;
        if (d2 < bd) {
            bd = d2;
            best = R.ddev[i];
        }
    }
    BL.sel = best;
    if (best >= 0) memcpy(BL.sel_addr, BL.dev[best].addr, 6);
    info_refresh();
    lv_obj_invalidate(R.canvas);
}

static void find_cb(lv_event_t *e) { (void)e; if (BL.sel >= 0) bl_open_finder(BL.sel); }

static void info_tap(lv_event_t *e) { (void)e; if (BL.sel >= 0) bl_open_detail(BL.sel); }

/* The radar on top, and under it a card for the one picked: tapping the card
 * opens its detail, the magnifier its finder. */
void bl_radar_build(lv_obj_t *page)
{
    memset(&R, 0, sizeof R);
    int32_t w = BL.cw, h = lv_obj_get_height(page);
    int32_t info_h = 72, rh = h - info_h - 8;
    R.canvas = bl_box(page, w, rh);
    lv_obj_add_flag(R.canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(R.canvas, radar_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(R.canvas, radar_tap, LV_EVENT_CLICKED, NULL);
    R.cx = w / 2;
    R.cy = rh / 2;
    R.R = (w < rh ? w : rh) / 2 - 6;

    R.info = bl_card(page, w, info_h);
    lv_obj_set_style_radius(R.info, 16, 0);
    lv_obj_set_y(R.info, rh + 8);
    lv_obj_add_flag(R.info, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(R.info, AOS_C_CARD2, LV_STATE_PRESSED);
    lv_obj_add_event_cb(R.info, info_tap, LV_EVENT_CLICKED, NULL);
    R.i_icon = bl_box(R.info, 40, 40);
    lv_obj_set_style_radius(R.i_icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(R.i_icon, LV_OPA_30, 0);
    lv_obj_align(R.i_icon, LV_ALIGN_LEFT_MID, 10, 0);
    R.i_glyph = aos_label(R.i_icon, "", &bl_sym_s, AOS_C_TEXT);
    lv_obj_center(R.i_glyph);
    int32_t tx = 60, tw = w - tx - 112;
    R.i_name = aos_label(R.info, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_size(R.i_name, tw, lv_font_get_line_height(aos_font_body));
    lv_label_set_long_mode(R.i_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(R.i_name, tx, 8);
    R.i_rssi = aos_label(R.info, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(R.i_rssi, 46);
    lv_obj_set_style_text_align(R.i_rssi, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(R.i_rssi, tx + tw, 8);
    R.i_sub = aos_label(R.info, "", aos_font_caption, AOS_C_DIM);
    lv_obj_set_size(R.i_sub, w - tx - 60, lv_font_get_line_height(aos_font_caption));
    lv_label_set_long_mode(R.i_sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(R.i_sub, tx, 36);
    aos_make_decorative(R.i_icon);
    aos_make_decorative(R.i_name);
    aos_make_decorative(R.i_rssi);
    aos_make_decorative(R.i_sub);
    /* the finder's button, round, on the right */
    R.btns = bl_box(R.info, 46, 46);
    lv_obj_set_style_radius(R.btns, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(R.btns, BL_C, 0);
    lv_obj_set_style_bg_opa(R.btns, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(R.btns, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_flag(R.btns, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(R.btns, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_center(aos_label(R.btns, AOS_SYM_MAGNIFY, &bl_sym_s, lv_color_white()));
    lv_obj_add_event_cb(R.btns, find_cb, LV_EVENT_CLICKED, NULL);
    info_refresh();
}

/* -------------------------------------------------------------------------- */
/* The finder                                                                  */
/* -------------------------------------------------------------------------- */

static void graph_draw(lv_event_t *e)
{
    if (BL.sel < 0 || BL.sel >= BL.ndev) return;
    const bl_dev_t *d = &BL.dev[BL.sel];
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e), &a);
    lv_layer_t *layer = lv_event_get_layer(e);
    /* guides at -50, -70 and -90 dBm */
    static const int G[] = { -50, -70, -90 };
    for (int i = 0; i < 3; i++) {
        int32_t y = a.y2 - (int32_t)((G[i] + 100) / 70.0f * lv_area_get_height(&a));
        bl_draw_line(layer, a.x1, y, a.x2, y, AOS_C_DIM, 1, LV_OPA_30);
        char t[8];
        snprintf(t, sizeof t, "%d", G[i]);
        bl_draw_text(layer, t, aos_font_tiny, AOS_C_DIM, a.x1 + 2, y - 15, 40, LV_TEXT_ALIGN_LEFT);
    }
    uint32_t now_s = (uint32_t)(aos_hal_uptime_ms() / 1000);
    bl_spark(layer, &a, d->hist, 60, (int)now_s, bl_rssi_color(F.has ? (int)F.smooth : -100));
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    BL.page = BL.sel >= 0 ? BL_PAGE_DETAIL : BL_PAGE_TAB;
    bl_rebuild();
}

static void snd_cb(lv_event_t *e)
{
    (void)e;
    BL.sound = !BL.sound;
    bl_settings_save();
    bl_pill_set(F.snd, BL.sound ? AOS_SYM_VOLUME_HIGH : AOS_SYM_VOLUME_OFF, BL.sound ? _("Con sonido") : _("Sin sonido"),
                BL.sound ? BL_C : AOS_C_CARD2);
}

static void fdetail_cb(lv_event_t *e) { (void)e; BL.page = BL_PAGE_DETAIL; bl_rebuild(); }

/* Held up in front, walked about with: the number big, whether it grows,
 * the last minute, and the sound on or off. */
void bl_finder_build(lv_obj_t *page)
{
    memset(&F, 0, sizeof F);
    if (BL.sel < 0) return;
    const bl_dev_t *d = &BL.dev[BL.sel];
    int32_t w = BL.cw;
    bl_back_bar(page, w, bl_dev_name(d), back_cb);

    /* the number right-aligned against a fixed point, the unit after it:
     * "-5" and "-95" do not move the unit around */
    F.big = aos_label(page, "--", aos_font_huge, AOS_C_TEXT);
    lv_obj_set_width(F.big, 140);
    lv_obj_set_style_text_align(F.big, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(F.big, w / 2 - 140 + 24, 50);
    F.unit = aos_label(page, "dBm", aos_font_body, AOS_C_DIM);
    lv_obj_set_pos(F.unit, w / 2 + 30, 74);
    F.trend = aos_label(page, "", aos_font_title, AOS_C_TEXT);
    lv_obj_align(F.trend, LV_ALIGN_TOP_MID, 0, 112);
    F.dist = aos_label(page, "", aos_font_body, AOS_C_DIM);
    lv_obj_set_width(F.dist, w);
    lv_obj_set_style_text_align(F.dist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(F.dist, LV_ALIGN_TOP_MID, 0, 148);
    F.gone = aos_label(page, "", aos_font_caption, AOS_C_ORANGE);
    lv_obj_set_width(F.gone, w);
    lv_obj_set_style_text_align(F.gone, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(F.gone, LV_ALIGN_TOP_MID, 0, 174);

    lv_obj_t *gc = bl_card(page, w, 74);
    lv_obj_set_style_radius(gc, 16, 0);
    lv_obj_set_y(gc, 198);
    lv_obj_set_style_pad_all(gc, 10, 0);
    F.graph = bl_box(gc, w - 20, 54);
    lv_obj_add_event_cb(F.graph, graph_draw, LV_EVENT_DRAW_MAIN, NULL);

    lv_obj_t *r = bl_row(page, w, 48, 8);
    lv_obj_set_y(r, 280);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    F.snd = bl_pill(r, BL.sound ? AOS_SYM_VOLUME_HIGH : AOS_SYM_VOLUME_OFF, BL.sound ? _("Con sonido") : _("Sin sonido"),
                    BL.sound ? BL_C : AOS_C_CARD2, snd_cb, NULL);
    bl_pill(r, AOS_SYM_INFORMATION_OUTLINE, _("Detalle"), AOS_C_CARD2, fdetail_cb, NULL);
    bl_finder_refresh();
}

void bl_finder_refresh(void)
{
    if (!F.big || BL.sel < 0) return;
    const bl_dev_t *d = &BL.dev[BL.sel];
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    uint32_t quiet = now - d->last_ms;
    /* the strongest of the last two seconds: a single packet lies, the
     * maximum of a few is what the radio can do from here */
    int r2 = bl_rssi_recent(d, 2);
    if (r2 != BL_NO_RSSI) {
        if (!F.has) F.smooth = r2;
        F.smooth += (r2 - F.smooth) * 0.35f;
        F.has = true;
    }
    char t[24];
    if (F.has && quiet < 10000) snprintf(t, sizeof t, "%d", (int)lroundf_safe(F.smooth));
    else snprintf(t, sizeof t, "--");
    lv_label_set_text(F.big, t);
    lv_obj_set_style_text_color(F.big, F.has && quiet < 10000 ? bl_rssi_color((int)F.smooth) : AOS_C_DIM, 0);

    /* the trend: the last 3 seconds against the 5 before them */
    uint32_t s = now / 1000;
    float a = 0, b = 0;
    int na = 0, nb = 0;
    for (int k = 0; k < 8; k++) {
        int v = d->hist[(s - k + BL_HIST * 100) % BL_HIST];
        if (v == BL_NO_RSSI || s - k > d->hist_sec) continue;
        if (k < 3) { a += v; na++; }
        else { b += v; nb++; }
    }
    const char *tr = "";
    lv_color_t tc = AOS_C_DIM;
    if (na && nb) {
        float diff = a / na - b / nb;
        if (diff > 2.5f) { tr = _("Más cerca"); tc = AOS_C_GREEN; }
        else if (diff < -2.5f) { tr = _("Más lejos"); tc = AOS_C_ORANGE; }
        else tr = _("Igual");
    }
    lv_label_set_text(F.trend, tr);
    lv_obj_set_style_text_color(F.trend, tc, 0);
    if (F.has) {
        int p1m = bl_p1m_guess(&d->ad, d->has_bc ? &d->bc : NULL);
        float m = bl_distance_m((int)F.smooth, p1m, bl_env_n());
        char dist[16], line[48];
        bl_fmt_num(dist, sizeof dist, m, m < 10 ? 1 : 0);
        snprintf(line, sizeof line, _("~%s m"), dist);
        lv_label_set_text(F.dist, line);
    }
    if (quiet > 4000) {
        char age[24], line[64];
        bl_fmt_age(age, sizeof age, quiet);
        snprintf(line, sizeof line, _("Sin señal hace %s"), age);
        lv_label_set_text(F.gone, line);
    } else {
        lv_label_set_text(F.gone, "");
    }
    lv_obj_invalidate(F.graph);
}

/* Like a Geiger counter: every 1.5 s far away, ten a second right on top. */
void bl_finder_beep(void)
{
    if (!BL.sound || !F.has || BL.sel < 0 || BL.sel >= BL.ndev) return;
    const bl_dev_t *d = &BL.dev[BL.sel];
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    if (now - d->last_ms > 4000) return;
    float f = (F.smooth + 95) / 60.0f;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    uint32_t period = (uint32_t)(1500 - f * 1400);
    if (now - F.last_beep < period) return;
    F.last_beep = now;
    aos_hal_beep(900 + (int)(f * 1500), 30);
}

void bl_radar_gone(void);
void bl_radar_gone(void)
{
    memset(&R, 0, sizeof R);
    uint32_t keep = F.last_beep;
    memset(&F, 0, sizeof F);
    F.last_beep = keep;
}
