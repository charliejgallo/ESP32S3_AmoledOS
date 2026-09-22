/*
 * MONSTER HOP - the panels (see mh_ui.h)
 */
#include "mh_app.h"
#include "mh_audio.h"
#include "mh_ui.h"

#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_ui.h"
#include "src/misc/cache/instance/lv_image_cache.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ACCENT   0x9A5AF0
#define GOLD     0xFFC83A
#define PANEL_BG 0x140F1E

/* the album's sticker of each level: a creature's card or a zone emblem */
static const char *sticker_of(int i)
{
    static const char *const n[MH_LEVELS] = {
        "zombie", "zombiedog", "#city", "brute", "vampire", "armor", "bat", "count",
        "mummy", "scarab", "#desert", "pharaoh", "werewolf", "crow", "#forest", "alpha",
    };
    return i >= 0 && i < MH_LEVELS ? n[i] : "";
}

static const char *sticker_title(int i)
{
    switch (i) {
    case 0: return _("Zombi");
    case 1: return _("Perro zombi");
    case 2: return _("Pueblo Zombi");
    case 3: return _("El Grandote");
    case 4: return _("Vampiro");
    case 5: return _("Armadura embrujada");
    case 6: return _("Murciélago");
    case 7: return _("El Conde");
    case 8: return _("Momia");
    case 9: return _("Escarabajo");
    case 10: return _("Desierto de las Momias");
    case 11: return _("El Faraón");
    case 12: return _("Hombre lobo");
    case 13: return _("Cuervo");
    case 14: return _("Bosque Lobizón");
    case 15: return _("El Alfa");
    default: return "";
    }
}

/* --------------------------------------------------------------------------
 * Pictures: the worker's half (no LVGL)
 * -------------------------------------------------------------------------- */

static void uimg_free(mh_uimg_t *u)
{
    free(u->buf);
    memset(u, 0, sizeof(*u));
}

static bool uimg_alloc(mh_uimg_t *u, int w, int h)
{
    uimg_free(u);
    if (w <= 0 || h <= 0) return false;
    u->buf = (uint8_t *)mh_calloc((size_t)w * h, 3);
    if (!u->buf) return false;
    u->w = (int16_t)w;
    u->h = (int16_t)h;
    return true;
}

/* a sprite over the picture at (x, y) (its frame's top-left), alpha-over */
static void uimg_put(mh_uimg_t *u, const mh_spr_t *s, int fmt, const mh_lut_t *lut, int x, int y)
{
    if (!u->buf || !s) return;
    uint16_t *col = (uint16_t *)u->buf;
    uint8_t *al = u->buf + (size_t)u->w * u->h * 2;
    int bpp = fmt == MH_PX_COL ? 4 : 3;
    for (int r = 0; r < s->h; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= u->h) continue;
        int x0, x1;
        const uint8_t *p = mh_spr_row(s, r, &x0, &x1);
        for (int k = x0; k < x1; k++, p += bpp) {
            int xx = x + k;
            if (xx < 0 || xx >= u->w) continue;
            uint16_t c;
            int a;
            if (fmt == MH_PX_LID) {
                a = (p[0] & 15) * 17;
                if (!a) continue;
                c = lut ? lut->c[p[0] >> 4][p[1] >> 2] : mh_rgb(p[1], p[1], p[1]);
            } else {
                c = (uint16_t)(p[0] | (p[1] << 8));
                a = p[2];
                if (!a) continue;
            }
            size_t i = (size_t)yy * u->w + xx;
            int da = al[i];
            if (da == 0 || a >= 255) {
                col[i] = c;
                al[i] = (uint8_t)a;
            } else {
                col[i] = mh_blend(col[i], c, a);
                al[i] = (uint8_t)(da + ((255 - da) * a >> 8));
            }
        }
    }
}

/* a sheet's frame alone, as a picture of its own size */
static bool uimg_sheet(mh_uimg_t *u, const char *name, int frame, const mh_lut_t *lut)
{
    mh_anim_t an;
    if (!mh_art_load(name, &an)) {
        uimg_free(u);
        return false;
    }
    const mh_spr_t *s = &an.f[frame % an.n];
    bool ok = uimg_alloc(u, s->w, s->h);
    if (ok) uimg_put(u, s, an.fmt, lut, 0, 0);
    mh_anim_free(&an);
    return ok;
}

/* Tommy as he is, small, for the map */
static void marker_build(app_t *a)
{
    mh_uimg_t *u = &a->ui_marker;
    mh_anim_t body, cap;
    if (!mh_art_load("tommy_idle_s", &body)) {
        uimg_free(u);
        return;
    }
    char nm[40];
    bool has_cap = false;
    if (a->wear.cap >= 0) {
        snprintf(nm, sizeof nm, "cap_%s_idle_s", mh_cap_name(a->wear.cap));
        has_cap = mh_art_load(nm, &cap);
    }
    const mh_spr_t *s = &body.f[0];
    if (uimg_alloc(u, 64, 80)) {
        mh_lut_t lut, clut;
        mh_lut_build(&lut, &a->outfit.body, 0xFFFFFF, 0);
        mh_lut_build(&clut, &a->outfit.cap, 0xFFFFFF, 0);
        int ox = 32, oy = 74;       /* where his feet go */
        uimg_put(u, s, body.fmt, &lut, ox - s->ax, oy - s->ay);
        if (has_cap) uimg_put(u, &cap.f[0], cap.fmt, &clut, ox - cap.f[0].ax, oy - cap.f[0].ay);
    }
    mh_anim_free(&body);
    if (has_cap) mh_anim_free(&cap);
}

static void cards_build(app_t *a)
{
    for (int i = 0; i < MH_LEVELS; i++) {
        if (a->ui_card[i].buf) continue;
        const char *k = sticker_of(i);
        char nm[48];
        if (k[0] == '#') {
            snprintf(nm, sizeof nm, "emblem_%s", k + 1);
            uimg_sheet(&a->ui_card[i], nm, 0, NULL);
        } else {
            mh_pal_t p;
            memset(&p, 0, sizeof p);
            snprintf(nm, sizeof nm, "pal_%s", k);
            mh_pal_load(nm, &p);
            mh_lut_t lut;
            mh_lut_build(&lut, &p, 0xFFFFFF, 1u << 5);
            snprintf(nm, sizeof nm, "card_%s", k);
            uimg_sheet(&a->ui_card[i], nm, 0, &lut);
        }
        mh_yield();
    }
}

static void turn_build(app_t *a)
{
    mh_outfit_t o;
    mh_wear_t w;
    int fx, tr;
    mh_shop_apply(a->try_eq, &o, &w, &fx, &tr);
    char nm[48];
    for (int k = 0; k < 5; k++) mh_anim_free(&a->turn[k]);
    mh_art_load("tommy_turn", &a->turn[0]);
    if (w.back >= 0) { snprintf(nm, sizeof nm, "back_%s_turn", mh_back_name(w.back)); mh_art_load(nm, &a->turn[1]); }
    if (w.hand >= 0) { snprintf(nm, sizeof nm, "hand_%s_turn", mh_hand_name(w.hand)); mh_art_load(nm, &a->turn[2]); }
    if (w.cap >= 0) { snprintf(nm, sizeof nm, "cap_%s_turn", mh_cap_name(w.cap)); mh_art_load(nm, &a->turn[3]); }
    if (w.pet >= 0) { snprintf(nm, sizeof nm, "pet_%s_turn", mh_pet_name(w.pet)); mh_art_load(nm, &a->turn[4]); }
    uint16_t keep = fx == SKIN_FX_LAVA ? (1u << 1) : 0;
    mh_lut_build(&a->turn_lut[0], &o.body, 0xFFFFFF, keep);
    mh_lut_build(&a->turn_lut[1], &o.back, 0xFFFFFF, 1u << 4);
    mh_lut_build(&a->turn_lut[2], &o.hand, 0xFFFFFF, 1u << 4);
    mh_lut_build(&a->turn_lut[3], &o.cap, 0xFFFFFF, 0);
    mh_lut_build(&a->turn_lut[4], &o.pet, 0xFFFFFF, 0);
}

void mh_ui_job(app_t *a, int what)
{
    char nm[32];
    switch (what) {
    case UJ_MENU:
        if (!a->ui_map.buf) uimg_sheet(&a->ui_map, "map", 0, NULL);
        if (!a->ui_logo.buf) uimg_sheet(&a->ui_logo, "logo", 0, NULL);
        if (!a->ui_house.buf) uimg_sheet(&a->ui_house, "house", 0, NULL);
        {
            static const char *const em[7] = { "city", "castle", "desert", "forest", "swamp", "graveyard", "lock" };
            for (int i = 0; i < 7; i++) {
                if (a->ui_emblem[i].buf) continue;
                snprintf(nm, sizeof nm, "emblem_%s", em[i]);
                uimg_sheet(&a->ui_emblem[i], nm, 0, NULL);
            }
            static const char *const tr[4] = { "trophy_bronze", "trophy_silver", "trophy_gold", "medal" };
            for (int i = 0; i < 4; i++) if (!a->ui_trophy[i].buf) uimg_sheet(&a->ui_trophy[i], tr[i], 0, NULL);
        }
        marker_build(a);
        if (!a->spots_ok) {
            uint32_t len = 0;
            uint8_t *b = mh_art_blob("map_spots", &len);
            if (b && len >= 2) {
                int n = b[0] | (b[1] << 8);
                for (int i = 0; i < n && i < 19 && 2 + i * 4 + 3 < (int)len; i++) {
                    a->spots[i][0] = (int16_t)(b[2 + i * 4] | (b[3 + i * 4] << 8));
                    a->spots[i][1] = (int16_t)(b[4 + i * 4] | (b[5 + i * 4] << 8));
                }
                a->spots_ok = true;
            }
            free(b);
        }
        a->menu_art = true;
        break;
    case UJ_TURN:
        turn_build(a);
        break;
    case UJ_CARDS:
        cards_build(a);
        break;
    case UJ_FREE_MAP:
        /* a level is about to load: every picture of the menus goes (the
         * map alone is 1 MB, the album's cards as much again), and comes
         * back with UJ_MENU, UJ_CARDS and UJ_TURN when they are shown */
        uimg_free(&a->ui_map);
        uimg_free(&a->ui_logo);
        uimg_free(&a->ui_house);
        for (int i = 0; i < 7; i++) uimg_free(&a->ui_emblem[i]);
        for (int i = 0; i < 4; i++) uimg_free(&a->ui_trophy[i]);
        for (int i = 0; i < MH_LEVELS; i++) uimg_free(&a->ui_card[i]);
        for (int k = 0; k < 5; k++) mh_anim_free(&a->turn[k]);
        a->menu_art = false;
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * The LVGL side
 * -------------------------------------------------------------------------- */

typedef struct {
    app_t    *a;
    lv_obj_t *p[ST_N];              /* the panel of each state, or NULL      */
    /* title */
    lv_obj_t *t_bg, *t_logo, *t_coins;
    /* map */
    lv_obj_t *m_scroll, *m_img, *m_pad[19], *m_star[MH_LEVELS][3], *m_marker, *m_head;
    lv_obj_t *m_pop, *m_pop_title, *m_pop_body, *m_pop_play;
    int       m_pop_level;
    /* house */
    lv_obj_t *h_img, *h_coins;
    /* shop */
    lv_obj_t *s_prev, *s_tabs, *s_list, *s_coins, *s_btn, *s_btn_lbl, *s_title, *s_item;
    uint16_t *s_prev_px;
    int       s_cat, s_sel, s_frame;
    bool      s_buy;                /* the shop, or the wardrobe             */
    uint32_t  s_ms;
    bool      s_turn_ready;
    /* album, trophies, stats */
    lv_obj_t *al_grid, *tr_list, *st_body;
    /* settings */
    lv_obj_t *c_diff[DIFF_N], *c_sfx, *c_music;
    /* boot, loading, pause, result, lobby */
    lv_obj_t *b_bar, *b_lbl, *l_lbl, *pz_map, *r_title, *r_stars[3], *r_body, *r_extra, *r_next, *lb_lbl, *l_bar;
    lv_obj_t *lb_level, *lb_prev, *lb_next, *lb_go;
    bool      r_race;
    uint16_t *pz_px;
    uint8_t   star_on[14 * 14 * 3], star_off[14 * 14 * 3], star_big[30 * 30 * 3], star_big_off[30 * 30 * 3];
    lv_image_dsc_t d_star_on, d_star_off, d_star_big, d_star_big_off;
} ui_t;

static ui_t s_ui;

static app_t *app_of(lv_event_t *e)
{
    return (app_t *)lv_event_get_user_data(e);
}

static void wrap(mh_uimg_t *u)
{
    u->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    u->dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    u->dsc.header.w = (uint32_t)u->w;
    u->dsc.header.h = (uint32_t)u->h;
    u->dsc.header.stride = (uint32_t)u->w * 2;
    u->dsc.data_size = (uint32_t)u->w * u->h * 3;
    u->dsc.data = u->buf;
}

static void wrap_raw(lv_image_dsc_t *d, uint8_t *buf, int w, int h)
{
    memset(d, 0, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565A8;
    d->header.w = (uint32_t)w;
    d->header.h = (uint32_t)h;
    d->header.stride = (uint32_t)w * 2;
    d->data_size = (uint32_t)w * h * 3;
    d->data = buf;
}

/* a five-pointed star, rasterised once */
static void star_make(uint8_t *buf, int n, uint32_t rgb)
{
    uint16_t *c = (uint16_t *)buf;
    uint8_t *al = buf + n * n * 2;
    float cx = n / 2.0f, cy = n / 2.0f + n * 0.04f, R = n * 0.5f, r = R * 0.45f;
    float px[10], py[10];
    for (int i = 0; i < 10; i++) {
        float ang = -1.5708f + i * 0.6283f;
        float rr = (i & 1) ? r : R;
        px[i] = cx + rr * cosf(ang);
        py[i] = cy + rr * sinf(ang);
    }
    uint16_t col = mh_hex(rgb), dark = mh_hex(0x2A1A08);
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            int cov = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    float qx = x + (sx + 0.5f) / 4, qy = y + (sy + 0.5f) / 4;
                    bool in = false;
                    for (int i = 0, j = 9; i < 10; j = i++) {
                        if (((py[i] > qy) != (py[j] > qy)) && (qx < (px[j] - px[i]) * (qy - py[i]) / (py[j] - py[i]) + px[i]))
                            in = !in;
                    }
                    cov += in;
                }
            }
            c[y * n + x] = cov < 16 ? dark : col;
            al[y * n + x] = (uint8_t)(cov * 255 / 16);
        }
    }
}

static lv_obj_t *panel(lv_obj_t *parent, int dim)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    if (dim) {
        lv_obj_set_style_bg_color(p, lv_color_hex(dim > 255 ? PANEL_BG : 0x000000), 0);
        lv_obj_set_style_bg_opa(p, (lv_opa_t)(dim > 255 ? 255 : dim), 0);
    }
    return p;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w);
    lv_obj_set_pos(l, x, y);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h, uint32_t accent,
                        lv_event_cb_t cb, void *data, lv_obj_t **lbl_out)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1C1628), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, h >= 58 ? aos_font_title : aos_font_body, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w - 12);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    if (lbl_out) *lbl_out = l;
    return b;
}

static lv_obj_t *image(lv_obj_t *parent, const void *src, int x, int y)
{
    lv_obj_t *im = lv_image_create(parent);
    if (src) lv_image_set_src(im, src);
    lv_obj_set_pos(im, x, y);
    lv_obj_remove_flag(im, LV_OBJ_FLAG_CLICKABLE);
    return im;
}

static void coins_text(char *b, size_t n, const app_t *a)
{
    snprintf(b, n, "%d %s   %d/48", (int)a->prog.coins, a->prog.coins == 1 ? _("moneda") : _("monedas"),
             mh_prog_stars(&a->prog));
}

/* ---- title ---- */

static void title_play_cb(lv_event_t *e) { mh_snd(SND_SELECT); mha_set_state(app_of(e), ST_MAP); }
static void title_house_cb(lv_event_t *e) { mh_snd(SND_SELECT); mha_set_state(app_of(e), ST_HOUSE); }

static void build_title(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_MENU] = panel(root, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x0C0A14), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    s_ui.t_bg = image(p, NULL, 0, 0);
    lv_obj_t *dim = lv_obj_create(p);
    lv_obj_remove_style_all(dim);
    lv_obj_set_size(dim, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_style_bg_color(dim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(dim, 110, 0);
    lv_obj_remove_flag(dim, LV_OBJ_FLAG_CLICKABLE);
    s_ui.t_logo = image(p, NULL, 14, 44);
    button(p, _("Jugar"), 64, 226, 240, 60, ACCENT, title_play_cb, a, NULL);
    button(p, _("La casa de Tommy"), 64, 300, 240, 48, 0x3AB07A, title_house_cb, a, NULL);
    s_ui.t_coins = label(p, "", aos_font_body, 0xFFD060, 0, 380, AOS_SCREEN_W);
}

static void title_refresh(app_t *a)
{
    if (a->ui_map.buf) {
        wrap(&a->ui_map);
        lv_image_set_src(s_ui.t_bg, &a->ui_map.dsc);
        /* the bottom of the map: the house and the town */
        lv_obj_set_y(s_ui.t_bg, AOS_SCREEN_H - a->ui_map.h);
    }
    if (a->ui_logo.buf) {
        wrap(&a->ui_logo);
        lv_image_set_src(s_ui.t_logo, &a->ui_logo.dsc);
        lv_obj_set_x(s_ui.t_logo, (AOS_SCREEN_W - a->ui_logo.w) / 2);
    }
    char b[64];
    coins_text(b, sizeof b, a);
    lv_label_set_text(s_ui.t_coins, b);
}

/* ---- the map ---- */

static void pop_close(void)
{
    lv_obj_add_flag(s_ui.m_pop, LV_OBJ_FLAG_HIDDEN);
}

static void pop_play_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    pop_close();
    mh_snd(SND_GO);
    mha_level_start(a, s_ui.m_pop_level);
}

static void pop_close_cb(lv_event_t *e) { (void)e; pop_close(); }

static void pad_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    mh_snd(SND_SELECT);
    if (i == 16) {
        mha_set_state(a, ST_HOUSE);
        return;
    }
    if (i > 16) {
        aos_ui_toast(_("Próximamente: una zona nueva"), 1600);
        return;
    }
    char b[200];
    if (!mha_level_open(a, i)) {
        int z = i / 4;
        if (mh_prog_stars(&a->prog) < mha_zone_need(z))
            snprintf(b, sizeof b, _("Hacen falta %d estrellas para abrir esta zona"), mha_zone_need(z));
        else snprintf(b, sizeof b, "%s", _("Termina el nivel anterior"));
        aos_ui_toast(b, 1800);
        return;
    }
    s_ui.m_pop_level = i;
    char t[96];
    snprintf(t, sizeof t, "%d-%d  %s", i / 4 + 1, i % 4 + 1, mha_level_title(i));
    lv_label_set_text(s_ui.m_pop_title, t);
    int ms = a->prog.best_ms[i];
    char best[32] = "-";
    if (ms > 0) snprintf(best, sizeof best, "%d:%02d", ms / 60000, (ms / 1000) % 60);
    snprintf(b, sizeof b, "%s\n%s: %d/3   %s: %s\n%s: %s", mha_zone_title(i / 4), _("Estrellas"), a->prog.stars[i],
             _("Récord"), best, _("Figurita"), (a->prog.stickers & (1u << i)) ? _("encontrada") : _("escondida"));
    lv_label_set_text(s_ui.m_pop_body, b);
    lv_obj_remove_flag(s_ui.m_pop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.m_pop);
}

static void build_map(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_MAP] = panel(root, 256);
    lv_obj_t *sc = s_ui.m_scroll = lv_obj_create(p);
    lv_obj_remove_style_all(sc);
    lv_obj_set_size(sc, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_scroll_dir(sc, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(sc, LV_OBJ_FLAG_SCROLLABLE);
    s_ui.m_img = image(sc, NULL, 0, 0);
    for (int i = 0; i < 19; i++) {
        lv_obj_t *b = lv_obj_create(sc);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 44, 44);
        lv_obj_set_style_radius(b, 22, 0);
        lv_obj_set_style_border_width(b, i < 16 ? 3 : 0, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(GOLD), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(b, 90, LV_STATE_PRESSED);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, pad_cb, LV_EVENT_CLICKED, a);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        s_ui.m_pad[i] = b;
        if (i < 16) {
            char n[4];
            snprintf(n, sizeof n, "%d", i % 4 + 1);
            lv_obj_t *l = lv_label_create(b);
            lv_label_set_text(l, n);
            lv_obj_set_style_text_font(l, aos_font_body, 0);
            lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
            lv_obj_center(l);
            for (int k = 0; k < 3; k++) {
                s_ui.m_star[i][k] = image(sc, &s_ui.d_star_off, 0, 0);
                lv_obj_add_flag(s_ui.m_star[i][k], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    s_ui.m_marker = image(sc, NULL, 0, 0);
    /* the bar on top: coins and stars, it does not scroll */
    lv_obj_t *hd = s_ui.m_head = lv_obj_create(p);
    lv_obj_remove_style_all(hd);
    lv_obj_set_size(hd, AOS_SCREEN_W, 34);
    lv_obj_set_style_bg_color(hd, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(hd, 150, 0);
    lv_obj_remove_flag(hd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *hl = label(hd, "", aos_font_body, 0xFFD060, 0, 6, AOS_SCREEN_W);
    lv_obj_set_user_data(hd, hl);
    /* the level's card */
    lv_obj_t *pop = s_ui.m_pop = lv_obj_create(p);
    lv_obj_remove_style_all(pop);
    lv_obj_set_size(pop, 320, 240);
    lv_obj_set_pos(pop, 24, 110);
    lv_obj_set_style_bg_color(pop, lv_color_hex(PANEL_BG), 0);
    lv_obj_set_style_bg_opa(pop, 245, 0);
    lv_obj_set_style_radius(pop, 20, 0);
    lv_obj_set_style_border_color(pop, lv_color_hex(GOLD), 0);
    lv_obj_set_style_border_width(pop, 2, 0);
    lv_obj_add_flag(pop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(pop, LV_OBJ_FLAG_HIDDEN);
    s_ui.m_pop_title = label(pop, "", aos_font_body, 0xFFFFFF, 10, 16, 300);
    s_ui.m_pop_body = label(pop, "", aos_font_small, 0xC8C0D8, 10, 60, 300);
    s_ui.m_pop_play = button(pop, _("Jugar"), 20, 170, 170, 52, 0x30C060, pop_play_cb, a, NULL);
    button(pop, _("Cerrar"), 200, 170, 100, 52, 0x5A4A7A, pop_close_cb, a, NULL);
}

/* the next level to play: the first open one without stars */
static int next_level(const app_t *a)
{
    int last = 0;
    for (int i = 0; i < MH_LEVELS; i++) {
        if (!mha_level_open(a, i)) break;
        last = i;
        if (!a->prog.stars[i]) return i;
    }
    return last;
}

static void map_refresh(app_t *a)
{
    char b[64];
    coins_text(b, sizeof b, a);
    lv_label_set_text((lv_obj_t *)lv_obj_get_user_data(s_ui.m_head), b);
    pop_close();
    if (!a->ui_map.buf) return;
    wrap(&a->ui_map);
    lv_image_set_src(s_ui.m_img, &a->ui_map.dsc);
    if (!a->spots_ok) return;
    for (int i = 0; i < 19; i++) {
        lv_obj_t *pd = s_ui.m_pad[i];
        int x = a->spots[i][0], y = a->spots[i][1];
        if (x == 0 && y == 0) {
            lv_obj_add_flag(pd, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_pos(pd, x - 22, y - 22);
        lv_obj_remove_flag(pd, LV_OBJ_FLAG_HIDDEN);
        if (i >= 16) continue;
        bool open = mha_level_open(a, i);
        lv_obj_set_style_border_color(pd, lv_color_hex(open ? GOLD : 0x606070), 0);
        lv_obj_set_style_bg_color(pd, lv_color_hex(open ? 0x2A1A40 : 0x202028), 0);
        lv_obj_set_style_bg_opa(pd, open ? 200 : 150, 0);
        for (int k = 0; k < 3; k++) {
            lv_obj_t *st = s_ui.m_star[i][k];
            if (!open) {
                lv_obj_add_flag(st, LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_image_set_src(st, k < a->prog.stars[i] ? &s_ui.d_star_on : &s_ui.d_star_off);
            lv_obj_set_pos(st, x - 23 + k * 16, y + 20);
            lv_obj_remove_flag(st, LV_OBJ_FLAG_HIDDEN);
        }
    }
    int nl = next_level(a);
    if (a->ui_marker.buf) {
        wrap(&a->ui_marker);
        lv_image_set_src(s_ui.m_marker, &a->ui_marker.dsc);
        lv_obj_set_pos(s_ui.m_marker, a->spots[nl][0] - 32, a->spots[nl][1] - 84);
        lv_obj_move_foreground(s_ui.m_marker);
    }
    /* scroll so the next level is in view */
    int want = a->spots[nl][1] - AOS_SCREEN_H / 2;
    if (want < 0) want = 0;
    if (want > a->ui_map.h - AOS_SCREEN_H) want = a->ui_map.h - AOS_SCREEN_H;
    lv_obj_scroll_to_y(s_ui.m_scroll, want, LV_ANIM_OFF);
}

/* ---- the house ---- */

static void house_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    int what = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    mh_snd(SND_SELECT);
    switch (what) {
    case 0: s_ui.s_buy = false; mha_set_state(a, ST_SHOP); break;
    case 1: s_ui.s_buy = true; mha_set_state(a, ST_SHOP); break;
    case 2: mha_set_state(a, ST_ALBUM); break;
    case 3: mha_set_state(a, ST_TROPHIES); break;
    case 4: mha_set_state(a, ST_STATS); break;
    case 5: {
        char name[32];
        if (mha_link_available(a, name, sizeof name)) mha_link_begin(a);
        else aos_ui_toast(_("Primero aparea otro reloj en Enlace"), 2200);
        break;
    }
    case 6: mha_set_state(a, ST_SETTINGS); break;
    case 7: mha_set_state(a, ST_MAP); break;
    default: break;
    }
}

static void build_house(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_HOUSE] = panel(root, 256);
    s_ui.h_img = image(p, NULL, 0, 0);
    s_ui.h_coins = label(p, "", aos_font_small, 0xFFD060, 0, 184, AOS_SCREEN_W);
    const char *names[6] = { _("Guardarropas"), _("Tienda"), _("Álbum"), _("Trofeos"), _("Estadísticas"),
                             _("Jugar con un amigo") };
    uint32_t cols[6] = { 0x3AB07A, GOLD, 0x5AA0F0, 0xE8A030, 0x9A90B0, 0xF05A8A };
    for (int i = 0; i < 6; i++) {
        lv_obj_t *b = button(p, names[i], 16 + (i & 1) * 172, 206 + (i >> 1) * 60, 164, 52, cols[i], house_cb, a, NULL);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
    }
    lv_obj_t *b = button(p, _("Ajustes"), 16, 390, 164, 44, 0x5A4A7A, house_cb, a, NULL);
    lv_obj_set_user_data(b, (void *)(intptr_t)6);
    b = button(p, _("Al mapa"), 188, 390, 164, 44, ACCENT, house_cb, a, NULL);
    lv_obj_set_user_data(b, (void *)(intptr_t)7);
}

static void house_refresh(app_t *a)
{
    if (a->ui_house.buf) {
        wrap(&a->ui_house);
        lv_image_set_src(s_ui.h_img, &a->ui_house.dsc);
        lv_obj_set_x(s_ui.h_img, (AOS_SCREEN_W - a->ui_house.w) / 2);
    }
    char b[64];
    coins_text(b, sizeof b, a);
    lv_label_set_text(s_ui.h_coins, b);
}

/* ---- the wardrobe and the shop ---- */

#define PREV_W 180
#define PREV_H 176

static void shop_list(app_t *a);

static void shop_preview_job(app_t *a)
{
    s_ui.s_turn_ready = false;
    mha_ui_job(a, UJ_TURN);
}

static void shop_btn_refresh(app_t *a)
{
    int cat = s_ui.s_cat, i = s_ui.s_sel;
    const mh_item_t *it = mh_shop_item(cat, i);
    char b[80];
    bool optional = cat == CAT_BACK || cat == CAT_HAND || cat == CAT_PET || cat == CAT_TRAIL;
    if (!it) {
        lv_obj_add_flag(s_ui.s_btn, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_ui.s_item, "");
        return;
    }
    lv_label_set_text(s_ui.s_item, _(it->name));
    lv_obj_remove_flag(s_ui.s_btn, LV_OBJ_FLAG_HIDDEN);
    if (!mh_prog_owns(&a->prog, cat, i)) snprintf(b, sizeof b, "%s  %d", _("Comprar"), it->price);
    else if (a->prog.eq[cat] == i) snprintf(b, sizeof b, "%s", optional ? _("Sacárselo") : _("Puesto"));
    else snprintf(b, sizeof b, "%s", _("Ponérselo"));
    lv_label_set_text(s_ui.s_btn_lbl, b);
}

static void shop_btn_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    int cat = s_ui.s_cat, i = s_ui.s_sel;
    const mh_item_t *it = mh_shop_item(cat, i);
    if (!it) return;
    bool optional = cat == CAT_BACK || cat == CAT_HAND || cat == CAT_PET || cat == CAT_TRAIL;
    if (!mh_prog_owns(&a->prog, cat, i)) {
        if (a->prog.coins < it->price) {
            mh_snd(SND_BUMP);
            aos_ui_toast(_("Te faltan monedas"), 1400);
            return;
        }
        a->prog.coins -= it->price;
        a->prog.own[cat] |= 1u << i;
        a->prog.stat[SX_BOUGHT]++;
        mh_prog_trophies(&a->prog);
        a->prog.eq[cat] = (int8_t)i;
        mh_snd(SND_COIN);
    } else if (a->prog.eq[cat] == i) {
        if (!optional) return;
        a->prog.eq[cat] = -1;
    } else {
        a->prog.eq[cat] = (int8_t)i;
        mh_snd(SND_SELECT);
    }
    memcpy(a->try_eq, a->prog.eq, sizeof a->try_eq);
    mha_save(a);
    mha_outfit(a);
    mha_ui_job(a, UJ_MENU);         /* the map's little Tommy, dressed anew */
    char b[64];
    coins_text(b, sizeof b, a);
    lv_label_set_text(s_ui.s_coins, b);
    shop_btn_refresh(a);
    shop_list(a);
    shop_preview_job(a);
}

static void item_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    s_ui.s_sel = i;
    /* try it on */
    memcpy(a->try_eq, a->prog.eq, sizeof a->try_eq);
    a->try_eq[s_ui.s_cat] = (int8_t)i;
    mh_snd(SND_SELECT);
    shop_btn_refresh(a);
    shop_list(a);
    shop_preview_job(a);
}

static void shop_list(app_t *a)
{
    lv_obj_clean(s_ui.s_list);
    int cat = s_ui.s_cat;
    for (int i = 0; i < mh_shop_count(cat); i++) {
        const mh_item_t *it = mh_shop_item(cat, i);
        bool own = mh_prog_owns(&a->prog, cat, i);
        if (!s_ui.s_buy && !own) continue;
        lv_obj_t *r = lv_obj_create(s_ui.s_list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 316, 40);
        lv_obj_set_style_radius(r, 12, 0);
        bool sel = s_ui.s_sel == i;
        lv_obj_set_style_bg_color(r, lv_color_hex(sel ? 0x3A2A5A : 0x1C1628), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(r, lv_color_hex(a->prog.eq[cat] == i ? 0x30C060 : 0x3A3050), 0);
        lv_obj_set_style_border_width(r, 2, 0);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(r, (void *)(intptr_t)i);
        lv_obj_add_event_cb(r, item_cb, LV_EVENT_CLICKED, a);
        for (int k = 0; k < 3; k++) {
            uint32_t c = it->c[k];
            if (k > 0 && (cat == CAT_SKIN || cat == CAT_HAIR || cat == CAT_TRAIL) && !c) break;
            if (k > 0 && cat != CAT_CAP && cat != CAT_SHIRT && cat != CAT_PET && cat != CAT_TRAIL) break;
            lv_obj_t *sw = lv_obj_create(r);
            lv_obj_remove_style_all(sw);
            lv_obj_set_size(sw, 18, 18);
            lv_obj_set_pos(sw, 10 + k * 14, 11);
            lv_obj_set_style_radius(sw, 9, 0);
            lv_obj_set_style_bg_color(sw, lv_color_hex(c), 0);
            lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(sw, lv_color_hex(0x000000), 0);
            lv_obj_set_style_border_width(sw, 1, 0);
            lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_t *l = lv_label_create(r);
        lv_label_set_text(l, _(it->name));
        lv_obj_set_style_text_font(l, aos_font_small, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_width(l, 190);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(l, 58, 11);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *pr = lv_label_create(r);
        char b[24];
        if (own) snprintf(b, sizeof b, "%s", a->prog.eq[cat] == i ? LV_SYMBOL_OK : "");
        else snprintf(b, sizeof b, "%d", it->price);
        lv_label_set_text(pr, b);
        lv_obj_set_style_text_font(pr, aos_font_small, 0);
        lv_obj_set_style_text_color(pr, lv_color_hex(own ? 0x30C060 : 0xFFD060), 0);
        lv_obj_align(pr, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_remove_flag(pr, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void tab_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    s_ui.s_cat = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    s_ui.s_sel = a->prog.eq[s_ui.s_cat];
    memcpy(a->try_eq, a->prog.eq, sizeof a->try_eq);
    uint32_t n = lv_obj_get_child_count(s_ui.s_tabs);
    for (uint32_t k = 0; k < n; k++) {
        lv_obj_t *t = lv_obj_get_child(s_ui.s_tabs, (int32_t)k);
        bool on = (int)(intptr_t)lv_obj_get_user_data(t) == s_ui.s_cat;
        lv_obj_set_style_bg_color(t, lv_color_hex(on ? ACCENT : 0x1C1628), 0);
    }
    mh_snd(SND_SELECT);
    shop_btn_refresh(a);
    shop_list(a);
    shop_preview_job(a);
}

static void build_shop(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_SHOP] = panel(root, 256);
    s_ui.s_title = label(p, "", aos_font_body, 0xFFFFFF, 0, 6, AOS_SCREEN_W);
    s_ui.s_prev_px = (uint16_t *)mh_malloc((size_t)PREV_W * PREV_H * 2);
    if (s_ui.s_prev_px) {
        for (int i = 0; i < PREV_W * PREV_H; i++) s_ui.s_prev_px[i] = mh_hex(PANEL_BG);
        s_ui.s_prev = lv_canvas_create(p);
        lv_canvas_set_buffer(s_ui.s_prev, s_ui.s_prev_px, PREV_W, PREV_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(s_ui.s_prev, (AOS_SCREEN_W - PREV_W) / 2, 30);
        lv_obj_remove_flag(s_ui.s_prev, LV_OBJ_FLAG_CLICKABLE);
    }
    s_ui.s_coins = label(p, "", aos_font_small, 0xFFD060, 0, 30, AOS_SCREEN_W - 12);
    lv_obj_set_style_text_align(s_ui.s_coins, LV_TEXT_ALIGN_RIGHT, 0);
    s_ui.s_item = label(p, "", aos_font_small, 0xC8C0D8, 0, 204, AOS_SCREEN_W);
    lv_obj_t *tabs = s_ui.s_tabs = lv_obj_create(p);
    lv_obj_remove_style_all(tabs);
    lv_obj_set_size(tabs, AOS_SCREEN_W, 40);
    lv_obj_set_pos(tabs, 0, 226);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabs, 6, 0);
    lv_obj_set_style_pad_left(tabs, 10, 0);
    lv_obj_set_scroll_dir(tabs, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(tabs, LV_SCROLLBAR_MODE_OFF);
    for (int c = 0; c < CAT_N; c++) {
        lv_obj_t *t = button(tabs, mh_shop_cat_name(c), 0, 0, 106, 36, ACCENT, tab_cb, a, NULL);
        lv_obj_set_user_data(t, (void *)(intptr_t)c);
    }
    lv_obj_t *list = s_ui.s_list = lv_obj_create(p);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 330, 126);
    lv_obj_set_pos(list, 19, 270);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    s_ui.s_btn = button(p, "", 64, 400, 240, 42, 0x30C060, shop_btn_cb, a, &s_ui.s_btn_lbl);
}

static void shop_refresh(app_t *a)
{
    lv_label_set_text(s_ui.s_title, s_ui.s_buy ? _("Tienda") : _("Guardarropas"));
    char b[64];
    coins_text(b, sizeof b, a);
    lv_label_set_text(s_ui.s_coins, b);
    memcpy(a->try_eq, a->prog.eq, sizeof a->try_eq);
    s_ui.s_sel = a->prog.eq[s_ui.s_cat];
    uint32_t n = lv_obj_get_child_count(s_ui.s_tabs);
    for (uint32_t k = 0; k < n; k++) {
        lv_obj_t *t = lv_obj_get_child(s_ui.s_tabs, (int32_t)k);
        bool on = (int)(intptr_t)lv_obj_get_user_data(t) == s_ui.s_cat;
        lv_obj_set_style_bg_color(t, lv_color_hex(on ? ACCENT : 0x1C1628), 0);
    }
    shop_btn_refresh(a);
    shop_list(a);
    shop_preview_job(a);
}

/* the turntable: Tommy with what is tried on, one frame every 140 ms */
static void preview_draw(app_t *a)
{
    if (!s_ui.s_prev_px || !a->turn[0].n) return;
    uint16_t bg = mh_hex(PANEL_BG);
    for (int i = 0; i < PREV_W * PREV_H; i++) s_ui.s_prev_px[i] = bg;
    mh_uimg_t u;
    memset(&u, 0, sizeof u);
    if (!uimg_alloc(&u, PREV_W, PREV_H)) return;
    int f = s_ui.s_frame;
    int fx = PREV_W / 2 + 18, fy = PREV_H - 8;
    /* the pet at his feet, to the left */
    if (a->turn[4].n) {
        const mh_spr_t *s = &a->turn[4].f[f % a->turn[4].n];
        uimg_put(&u, s, a->turn[4].fmt, &a->turn_lut[4], fx - 66 - s->ax, fy - s->ay);
    }
    for (int k = 0; k < 4; k++) {
        if (!a->turn[k].n) continue;
        const mh_spr_t *s = &a->turn[k].f[f % a->turn[k].n];
        uimg_put(&u, s, a->turn[k].fmt, &a->turn_lut[k], fx - s->ax, fy - s->ay);
    }
    uint16_t *c = (uint16_t *)u.buf;
    uint8_t *al = u.buf + (size_t)PREV_W * PREV_H * 2;
    for (int i = 0; i < PREV_W * PREV_H; i++) {
        if (al[i]) s_ui.s_prev_px[i] = mh_blend(bg, c[i], al[i]);
    }
    uimg_free(&u);
    lv_obj_invalidate(s_ui.s_prev);
}

/* ---- album, trophies, stats ---- */

static void card_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    char b[96];
    if (a->prog.stickers & (1u << i)) snprintf(b, sizeof b, "%s", sticker_title(i));
    else snprintf(b, sizeof b, _("Escondida en %s"), mha_level_title(i));
    aos_ui_toast(b, 1600);
}

static void build_album(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_ALBUM] = panel(root, 256);
    label(p, _("Álbum de figuritas"), aos_font_body, 0xFFFFFF, 0, 8, AOS_SCREEN_W);
    lv_obj_t *g = s_ui.al_grid = lv_obj_create(p);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, 352, 404);
    lv_obj_set_pos(g, 8, 40);
    lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(g, 6, 0);
    lv_obj_set_style_pad_column(g, 6, 0);
    lv_obj_add_flag(g, LV_OBJ_FLAG_SCROLLABLE);
    (void)a;
}

static void album_refresh(app_t *a)
{
    lv_obj_clean(s_ui.al_grid);
    for (int i = 0; i < MH_LEVELS; i++) {
        bool got = (a->prog.stickers & (1u << i)) != 0;
        lv_obj_t *c = lv_obj_create(s_ui.al_grid);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 82, 96);
        lv_obj_set_style_radius(c, 10, 0);
        static const uint32_t zc[4] = { 0x3A4050, 0x3A2A5A, 0x5A4028, 0x1E4A34 };
        lv_obj_set_style_bg_color(c, lv_color_hex(got ? zc[i / 4] : 0x18141E), 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(got ? GOLD : 0x3A3448), 0);
        lv_obj_set_style_border_width(c, 2, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(c, (void *)(intptr_t)i);
        lv_obj_add_event_cb(c, card_cb, LV_EVENT_CLICKED, a);
        mh_uimg_t *u = &a->ui_card[i];
        if (got && u->buf) {
            wrap(u);
            lv_obj_t *im = image(c, &u->dsc, 0, 0);
            int sw = u->w, sh = u->h;
            int scale = 256;
            if (sw > 76 || sh > 88) {
                int s1 = 76 * 256 / sw, s2 = 88 * 256 / sh;
                scale = s1 < s2 ? s1 : s2;
            }
            lv_image_set_scale(im, (uint32_t)scale);
            lv_obj_center(im);
        } else {
            lv_obj_t *l = lv_label_create(c);
            lv_label_set_text(l, "?");
            lv_obj_set_style_text_font(l, aos_font_title, 0);
            lv_obj_set_style_text_color(l, lv_color_hex(0x5A5068), 0);
            lv_obj_center(l);
        }
        char n[8];
        snprintf(n, sizeof n, "%d-%d", i / 4 + 1, i % 4 + 1);
        lv_obj_t *l = lv_label_create(c);
        lv_label_set_text(l, n);
        lv_obj_set_style_text_font(l, aos_font_small, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xC8C0D8), 0);
        lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -2);
    }
}

static void build_trophies(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_TROPHIES] = panel(root, 256);
    label(p, _("Trofeos"), aos_font_body, 0xFFFFFF, 0, 8, AOS_SCREEN_W);
    lv_obj_t *l = s_ui.tr_list = lv_obj_create(p);
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, 340, 404);
    lv_obj_set_pos(l, 14, 40);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l, 6, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    (void)a;
}

static void trophies_refresh(app_t *a)
{
    lv_obj_clean(s_ui.tr_list);
    for (int t = 0; t < TR_N; t++) {
        bool got = (a->prog.trophies & (1u << t)) != 0;
        lv_obj_t *r = lv_obj_create(s_ui.tr_list);
        lv_obj_remove_style_all(r);
        /* as tall as its description needs: some take two lines */
        lv_obj_set_size(r, 336, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(r, 60, 0);
        lv_obj_set_style_pad_bottom(r, 8, 0);
        lv_obj_set_style_radius(r, 12, 0);
        lv_obj_set_style_bg_color(r, lv_color_hex(got ? 0x2A2040 : 0x18141E), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
        mh_uimg_t *u = &a->ui_trophy[mh_trophy_tier(t)];
        if (u->buf) {
            wrap(u);
            lv_obj_t *im = image(r, &u->dsc, 0, 0);
            lv_image_set_scale(im, 128);
            /* its box at the drawn size, or the row grows to the unscaled one */
            lv_obj_set_size(im, 56, 56);
            lv_image_set_inner_align(im, LV_IMAGE_ALIGN_CENTER);
            lv_obj_align(im, LV_ALIGN_LEFT_MID, 2, 0);
            if (!got) lv_obj_set_style_image_opa(im, 60, 0);
        }
        lv_obj_t *n = lv_label_create(r);
        lv_label_set_text(n, mh_trophy_name(t));
        lv_obj_set_style_text_font(n, aos_font_small, 0);
        lv_obj_set_style_text_color(n, lv_color_hex(got ? 0xFFE070 : 0x8A8098), 0);
        lv_obj_set_pos(n, 64, 8);
        lv_obj_t *d = lv_label_create(r);
        lv_label_set_text(d, mh_trophy_desc(t));
        lv_obj_set_style_text_font(d, aos_font_small, 0);
        lv_obj_set_style_text_color(d, lv_color_hex(0xA8A0B8), 0);
        lv_obj_set_width(d, 264);
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_pos(d, 64, 30);
    }
}

static void build_stats(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_STATS] = panel(root, 256);
    label(p, _("Estadísticas"), aos_font_body, 0xFFFFFF, 0, 8, AOS_SCREEN_W);
    s_ui.st_body = label(p, "", aos_font_small, 0xE0D8F0, 24, 50, AOS_SCREEN_W - 48);
    lv_obj_set_style_text_align(s_ui.st_body, LV_TEXT_ALIGN_LEFT, 0);
    (void)a;
}

static int bits(uint32_t v)
{
    int n = 0;
    while (v) { n += (int)(v & 1); v >>= 1; }
    return n;
}

static void stats_refresh(app_t *a)
{
    const int32_t *s = a->prog.stat;
    char b[640];
    int t = s[SX_PLAY_S];
    snprintf(b, sizeof b,
             "%s: %d\n%s: %d/48\n%s: %d/16\n%s: %d\n%s: %d\n%s: %d\n%s: %d\n%s: %d\n%s: %d\n%s: %d / %d\n%s: %dh %02dm",
             _("Niveles terminados"), (int)s[SX_LEVELS], _("Estrellas"), mh_prog_stars(&a->prog),
             _("Figuritas"), bits(a->prog.stickers), _("Llaves juntadas"), (int)s[SX_KEYS],
             _("Monedas juntadas"), (int)s[SX_COINS], _("Saltos"), (int)s[SX_HOPS],
             _("Atrapado por monstruos"), (int)s[SX_CAUGHT], _("Al agua"), (int)s[SX_DROWNED],
             _("Caídas"), (int)s[SX_FELL], _("Carreras ganadas"), (int)s[SX_RACES_WON], (int)s[SX_RACES],
             _("Tiempo jugado"), t / 3600, (t / 60) % 60);
    lv_label_set_text(s_ui.st_body, b);
}

/* ---- settings ---- */

static void chip_style(lv_obj_t *c, bool on)
{
    lv_obj_set_style_bg_color(c, lv_color_hex(on ? ACCENT : 0x1C1628), 0);
}

static void settings_refresh(app_t *a)
{
    for (int i = 0; i < DIFF_N; i++) chip_style(s_ui.c_diff[i], a->prog.diff == i);
    chip_style(s_ui.c_sfx, a->prog.sfx);
    chip_style(s_ui.c_music, a->prog.music);
}

static void diff_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    a->prog.diff = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    mha_save(a);
    settings_refresh(a);
}

static void sound_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    lv_obj_t *t = lv_event_get_target(e);
    if (t == s_ui.c_sfx) a->prog.sfx = !a->prog.sfx;
    else a->prog.music = !a->prog.music;
    mh_audio_enable(a->prog.sfx, a->prog.music);
    mha_save(a);
    settings_refresh(a);
}

static void build_settings(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_SETTINGS] = panel(root, 256);
    label(p, _("Ajustes"), aos_font_title, 0xFFFFFF, 0, 40, AOS_SCREEN_W);
    label(p, _("Dificultad"), aos_font_body, 0xC8C0D8, 0, 96, AOS_SCREEN_W);
    const char *dn[DIFF_N] = { _("Fácil"), _("Normal"), _("Difícil") };
    for (int i = 0; i < DIFF_N; i++) {
        s_ui.c_diff[i] = button(p, dn[i], 20 + i * 112, 128, 104, 44, ACCENT, diff_cb, a, NULL);
        lv_obj_set_user_data(s_ui.c_diff[i], (void *)(intptr_t)i);
    }
    label(p, _("Fácil: 5 vidas y sin reloj. Normal: 3 vidas y reloj. Difícil: 1 vida y un poco menos de tiempo."),
          aos_font_small, 0x9A90B0, 24, 180, AOS_SCREEN_W - 48);
    s_ui.c_sfx = button(p, _("Efectos"), 40, 260, 136, 48, ACCENT, sound_cb, a, NULL);
    s_ui.c_music = button(p, _("Música"), 192, 260, 136, 48, ACCENT, sound_cb, a, NULL);
}

/* ---- boot, loading, pause, result, lobby ---- */

static void resume_cb(lv_event_t *e) { mha_resume(app_of(e)); }
static void restart_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    mha_level_start(a, a->level);
}
static void to_map_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    if (a->link_on) mhl_end(a);
    bool paused = a->state == ST_PAUSE;
    mha_set_state(a, s_ui.r_race && !paused ? ST_HOUSE : ST_MAP);
    if (paused) mha_level_leave(a);
}
static void next_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    if (s_ui.r_race) {
        /* another race: back to the lobby (the link is still up if the
         * other did not leave) */
        char name[32];
        if (mha_link_available(a, name, sizeof name)) mha_link_begin(a);
        else mha_set_state(a, ST_HOUSE);
        return;
    }
    int n = a->level + 1;
    if (a->game.state == GS_WON && n < MH_LEVELS && mha_level_open(a, n)) mha_level_start(a, n);
    else if (a->game.state != GS_WON) mha_level_start(a, a->level);
    else mha_set_state(a, ST_MAP);
}
static void lobby_cancel_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    mhl_end(a);
    mha_set_state(a, ST_HOUSE);
}
static void lobby_prev_cb(lv_event_t *e) { mh_snd(SND_SELECT); mhl_pick(app_of(e), -1); }
static void lobby_next_cb(lv_event_t *e) { mh_snd(SND_SELECT); mhl_pick(app_of(e), 1); }
static void lobby_go_cb(lv_event_t *e) { mh_snd(SND_SELECT); mhl_go(app_of(e)); }

#define PZ_CELL 8

static void build_misc(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_BOOT] = panel(root, 255);
    label(p, "MONSTER HOP", aos_font_title, 0xB88AFF, 0, 170, AOS_SCREEN_W);
    s_ui.b_bar = lv_bar_create(p);
    lv_obj_set_size(s_ui.b_bar, 220, 10);
    lv_obj_set_pos(s_ui.b_bar, 74, 230);
    lv_obj_set_style_bg_color(s_ui.b_bar, lv_color_hex(0x2A2238), 0);
    lv_obj_set_style_bg_color(s_ui.b_bar, lv_color_hex(ACCENT), LV_PART_INDICATOR);
    lv_obj_add_flag(s_ui.b_bar, LV_OBJ_FLAG_HIDDEN);
    s_ui.b_lbl = label(p, _("Cargando..."), aos_font_body, 0xC8C0D8, 20, 252, AOS_SCREEN_W - 40);

    p = s_ui.p[ST_LOADING] = panel(root, 230);
    s_ui.l_lbl = label(p, "", aos_font_title, 0xFFFFFF, 10, 180, AOS_SCREEN_W - 20);
    s_ui.l_bar = lv_bar_create(p);
    lv_obj_set_size(s_ui.l_bar, 220, 10);
    lv_obj_set_pos(s_ui.l_bar, 74, 290);
    lv_obj_set_style_bg_color(s_ui.l_bar, lv_color_hex(0x2A2238), 0);
    lv_obj_set_style_bg_color(s_ui.l_bar, lv_color_hex(ACCENT), LV_PART_INDICATOR);
    lv_obj_add_flag(s_ui.l_bar, LV_OBJ_FLAG_HIDDEN);
    label(p, _("Cargando..."), aos_font_body, 0xC8C0D8, 0, 250, AOS_SCREEN_W);

    p = s_ui.p[ST_PAUSE] = panel(root, 180);
    label(p, _("Pausa"), aos_font_title, 0xFFFFFF, 0, 10, AOS_SCREEN_W);
    s_ui.pz_px = (uint16_t *)mh_malloc((size_t)MH_LV_MAXW * PZ_CELL * MH_LV_MAXH * PZ_CELL * 2);
    if (s_ui.pz_px) {
        s_ui.pz_map = lv_canvas_create(p);
        lv_canvas_set_buffer(s_ui.pz_map, s_ui.pz_px, MH_LV_MAXW * PZ_CELL, MH_LV_MAXH * PZ_CELL, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(s_ui.pz_map, (AOS_SCREEN_W - MH_LV_MAXW * PZ_CELL) / 2, 44);
        lv_image_set_scale(s_ui.pz_map, 200);
        lv_obj_remove_flag(s_ui.pz_map, LV_OBJ_FLAG_CLICKABLE);
    }
    button(p, _("Seguir"), 24, 330, 150, 48, 0x30C060, resume_cb, a, NULL);
    button(p, _("Reintentar"), 194, 330, 150, 48, ACCENT, restart_cb, a, NULL);
    button(p, _("Salir al mapa"), 64, 388, 240, 44, 0x5A4A7A, to_map_cb, a, NULL);

    p = s_ui.p[ST_RESULT] = panel(root, 200);
    s_ui.r_title = label(p, "", aos_font_title, 0xFFE070, 0, 60, AOS_SCREEN_W);
    for (int k = 0; k < 3; k++) s_ui.r_stars[k] = image(p, &s_ui.d_star_big_off, 124 + k * 42, 110);
    s_ui.r_body = label(p, "", aos_font_body, 0xFFFFFF, 0, 156, AOS_SCREEN_W);
    s_ui.r_extra = label(p, "", aos_font_small, 0x8AF59A, 20, 250, AOS_SCREEN_W - 40);
    lv_obj_t *nb = button(p, _("Seguir"), 64, 316, 240, 52, 0x30C060, next_cb, a, NULL);
    s_ui.r_next = lv_obj_get_child(nb, 0);
    button(p, _("Al mapa"), 64, 378, 240, 44, 0x5A4A7A, to_map_cb, a, NULL);

    p = s_ui.p[ST_LOBBY] = panel(root, 230);
    label(p, _("Carrera de llaves"), aos_font_title, 0xFFFFFF, 0, 70, AOS_SCREEN_W);
    s_ui.lb_lbl = label(p, "", aos_font_body, 0xC8C0D8, 20, 118, AOS_SCREEN_W - 40);
    s_ui.lb_level = label(p, "", aos_font_title, 0xFFE070, 60, 200, AOS_SCREEN_W - 120);
    s_ui.lb_prev = button(p, "<", 10, 190, 48, 48, 0x3A3050, lobby_prev_cb, a, NULL);
    s_ui.lb_next = button(p, ">", AOS_SCREEN_W - 58, 190, 48, 48, 0x3A3050, lobby_next_cb, a, NULL);
    /* the rule takes two lines in English and German: the buttons go under it */
    label(p, _("Una llave, un punto. El primero en salir suma dos."), aos_font_small, 0x8A80A0, 20, 250,
          AOS_SCREEN_W - 40);
    s_ui.lb_go = button(p, _("¡A correr!"), 64, 298, 240, 50, 0x30C060, lobby_go_cb, a, NULL);
    button(p, _("Cancelar"), 84, 360, 200, 46, 0x5A4A7A, lobby_cancel_cb, a, NULL);
}

void mh_ui_boot_text(app_t *a, const char *txt) { (void)a; lv_label_set_text(s_ui.b_lbl, txt); }
void mh_ui_loading_text(app_t *a, const char *txt) { (void)a; lv_label_set_text(s_ui.l_lbl, txt); }

void mh_ui_open_shop(app_t *a, bool buy)
{
    s_ui.s_buy = buy;
    mha_set_state(a, ST_SHOP);
}

void mh_ui_load_bar(app_t *a, bool show, int pct)
{
    lv_obj_t *bar = a->state == ST_BOOT ? s_ui.b_bar : s_ui.l_bar;
    if (!bar) return;
    if (!show) {
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(bar, pct < 0 ? 0 : pct > 100 ? 100 : pct, LV_ANIM_OFF);
}
void mh_ui_lobby_fill(app_t *a, const char *txt, const char *level, bool host)
{
    (void)a;
    lv_label_set_text(s_ui.lb_lbl, txt);
    lv_label_set_text(s_ui.lb_level, level);
    lv_obj_t *b[3] = { s_ui.lb_prev, s_ui.lb_next, s_ui.lb_go };
    for (int i = 0; i < 3; i++) {
        if (host) lv_obj_remove_flag(b[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(b[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void mh_ui_pause_fill(app_t *a)
{
    const mh_level_t *lv = &a->lv;
    const mh_game_t *g = &a->game;
    if (!s_ui.pz_px || !a->level_ok) return;
    int W = MH_LV_MAXW * PZ_CELL, H = MH_LV_MAXH * PZ_CELL;
    uint16_t *px = s_ui.pz_px;
    for (int i = 0; i < W * H; i++) px[i] = 0;
    int ox = (W - lv->w * PZ_CELL) / 2, oy = (H - lv->h * PZ_CELL) / 2;
    for (int y = 0; y < lv->h; y++) {
        for (int x = 0; x < lv->w; x++) {
            const mh_cell_t *c = mh_cell(lv, x, y);
            uint32_t col;
            switch (c->kind) {
            case CK_PIT: col = 0x000000; break;
            case CK_WATER: col = 0x1E4A7A; break;
            case CK_QUICK: col = 0xA08040; break;
            case CK_BRIDGE: col = 0x8A6A40; break;
            default: {
                static const uint32_t lvl[4] = { 0x3A4A3A, 0x506250, 0x6A806A, 0x88A088 };
                col = lvl[c->h & 3];
                if (c->flags & (CF_SOLID | CF_HIGH)) col = 0x24282C;
                break;
            }
            }
            uint16_t c16 = mh_hex(col);
            int sx = ox + x * PZ_CELL, sy = oy + (lv->h - 1 - y) * PZ_CELL;
            for (int r = 0; r < PZ_CELL - 1; r++)
                for (int k = 0; k < PZ_CELL - 1; k++) px[(sy + r) * W + sx + k] = c16;
        }
    }
#define DOT(cx, cy, col, rad) do { \
        int _x = ox + (int)((cx) * PZ_CELL), _y = oy + (int)((lv->h - (cy)) * PZ_CELL); \
        for (int r = -(rad); r <= (rad); r++) for (int k = -(rad); k <= (rad); k++) \
            if (r * r + k * k <= (rad) * (rad) + 1 && _x + k >= 0 && _x + k < W && _y + r >= 0 && _y + r < H) \
                px[(_y + r) * W + _x + k] = mh_hex(col); } while (0)
    for (int i = 0; i < g->n_pick; i++) {
        const mh_pick_t *p = &g->pick[i];
        if (p->type == ENT_KEY && !p->taken) DOT(p->x + 0.5f, p->y + 0.5f, 0xFFD040, 3);
    }
    for (int i = 0; i < g->n_cp; i++) DOT(g->cpt[i].x + 0.5f, g->cpt[i].y + 0.5f, g->cpt[i].lit ? 0xFF9A30 : 0x805020, 2);
    if (g->exit_x >= 0) DOT(g->exit_x + 0.5f, g->exit_y + 0.5f, g->exit_open ? 0x60F070 : 0x3A7A40, 3);
    for (int i = 0; i < g->n_mon; i++) DOT(g->mon[i].x, g->mon[i].y, 0xFF3A4A, 2);
    DOT(g->h.x, g->h.y, 0x60D0FF, 3);
#undef DOT
    lv_obj_invalidate(s_ui.pz_map);
}

static void trophy_lines(char *x, size_t n, uint32_t new_tr)
{
    for (int t = 0; t < TR_N; t++) {
        if (!(new_tr & (1u << t))) continue;
        size_t l = strlen(x);
        snprintf(x + l, n - l, "%s%s: %s", l ? "\n" : "", _("Trofeo"), mh_trophy_name(t));
    }
}

void mh_ui_race_fill(app_t *a, int outcome, int me, int them, int earned, uint32_t new_tr)
{
    s_ui.r_race = true;
    const char *who = a->partner[0] ? a->partner : "?";
    lv_label_set_text(s_ui.r_title, outcome > 0 ? _("¡Ganaste la carrera!") : outcome == 0 ? _("Perdiste la carrera")
                                                                                          : _("Dejaste la carrera"));
    for (int k = 0; k < 3; k++) lv_obj_add_flag(s_ui.r_stars[k], LV_OBJ_FLAG_HIDDEN);
    char b[200];
    if (outcome < 0) snprintf(b, sizeof b, "%s: +%d", _("Monedas"), earned);
    else snprintf(b, sizeof b, "%s %d  -  %d %s\n%s: +%d", _("Tú"), me, them, who, _("Monedas"), earned);
    lv_label_set_text(s_ui.r_body, b);
    char x[200] = "";
    trophy_lines(x, sizeof x, new_tr);
    lv_label_set_text(s_ui.r_extra, x);
    lv_label_set_text(s_ui.r_next, _("Otra carrera"));
}

void mh_ui_result_fill(app_t *a, bool won, int stars, int earned, bool best, bool sticker, uint32_t new_tr)
{
    const mh_game_t *g = &a->game;
    s_ui.r_race = false;
    lv_label_set_text(s_ui.r_title, won ? _("¡Nivel completo!") : _("Fin del juego"));
    for (int k = 0; k < 3; k++) {
        lv_image_set_src(s_ui.r_stars[k], k < stars ? &s_ui.d_star_big : &s_ui.d_star_big_off);
        if (won) lv_obj_remove_flag(s_ui.r_stars[k], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_ui.r_stars[k], LV_OBJ_FLAG_HIDDEN);
    }
    char b[200];
    int secs = (int)g->t;
    snprintf(b, sizeof b, "%s %d:%02d%s\n%s: %d\n%s: +%d", _("Tiempo"), secs / 60, secs % 60,
             best ? _("  ¡récord!") : "", _("Vidas perdidas"), g->lost, _("Monedas"), earned);
    lv_label_set_text(s_ui.r_body, b);
    char x[200] = "";
    if (sticker) snprintf(x, sizeof x, "%s", _("¡Encontraste la figurita!"));
    trophy_lines(x, sizeof x, new_tr);
    lv_label_set_text(s_ui.r_extra, x);
    int n = a->level + 1;
    lv_label_set_text(s_ui.r_next, !won ? _("Reintentar") : (n < MH_LEVELS && mha_level_open(a, n)) ? _("Siguiente nivel")
                                                                                                    : _("Al mapa"));
}

/* ---- the whole thing ---- */

void mh_ui_build(app_t *a, lv_obj_t *root)
{
    memset(&s_ui, 0, sizeof s_ui);
    s_ui.a = a;
    star_make(s_ui.star_on, 14, GOLD);
    star_make(s_ui.star_off, 14, 0x4A4458);
    star_make(s_ui.star_big, 30, GOLD);
    star_make(s_ui.star_big_off, 30, 0x4A4458);
    wrap_raw(&s_ui.d_star_on, s_ui.star_on, 14, 14);
    wrap_raw(&s_ui.d_star_off, s_ui.star_off, 14, 14);
    wrap_raw(&s_ui.d_star_big, s_ui.star_big, 30, 30);
    wrap_raw(&s_ui.d_star_big_off, s_ui.star_big_off, 30, 30);
    build_title(a, root);
    build_map(a, root);
    build_house(a, root);
    build_shop(a, root);
    build_album(a, root);
    build_trophies(a, root);
    build_stats(a, root);
    build_settings(a, root);
    build_misc(a, root);
}

void mh_ui_show(app_t *a, int st)
{
    for (int i = 0; i < ST_N; i++) {
        if (s_ui.p[i] && i != st) lv_obj_add_flag(s_ui.p[i], LV_OBJ_FLAG_HIDDEN);
    }
    switch (st) {
    case ST_MENU: title_refresh(a); break;
    case ST_MAP: map_refresh(a); break;
    case ST_HOUSE: house_refresh(a); break;
    case ST_SHOP: shop_refresh(a); break;
    case ST_ALBUM:
        album_refresh(a);
        if (!a->ui_card[0].buf) mha_ui_job(a, UJ_CARDS);
        break;
    case ST_TROPHIES: trophies_refresh(a); break;
    case ST_STATS: stats_refresh(a); break;
    case ST_SETTINGS: settings_refresh(a); break;
    default: break;
    }
    if (st >= 0 && st < ST_N && s_ui.p[st]) {
        lv_obj_remove_flag(s_ui.p[st], LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_ui.p[st]);
    }
}

/* before the worker rewrites pictures, LVGL lets go of them */
void mh_ui_before_job(app_t *a, int what)
{
    if (what == UJ_MENU || what == UJ_FREE_MAP) {
        lv_image_set_src(s_ui.m_marker, NULL);
        if (a->ui_marker.buf) lv_image_cache_drop(&a->ui_marker.dsc);
    }
    if (what == UJ_FREE_MAP) {
        lv_image_set_src(s_ui.t_bg, NULL);
        lv_image_set_src(s_ui.m_img, NULL);
        lv_image_set_src(s_ui.t_logo, NULL);
        lv_image_set_src(s_ui.h_img, NULL);
        /* the album and the trophies are rebuilt each time they are shown */
        lv_obj_clean(s_ui.al_grid);
        lv_obj_clean(s_ui.tr_list);
        mh_uimg_t *all[] = { &a->ui_map, &a->ui_logo, &a->ui_house };
        for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
            if (all[i]->buf) lv_image_cache_drop(&all[i]->dsc);
        for (int i = 0; i < 7; i++) if (a->ui_emblem[i].buf) lv_image_cache_drop(&a->ui_emblem[i].dsc);
        for (int i = 0; i < 4; i++) if (a->ui_trophy[i].buf) lv_image_cache_drop(&a->ui_trophy[i].dsc);
        for (int i = 0; i < MH_LEVELS; i++) if (a->ui_card[i].buf) lv_image_cache_drop(&a->ui_card[i].dsc);
        s_ui.s_turn_ready = false;
    }
    if (what == UJ_TURN) s_ui.s_turn_ready = false;
}

void mh_ui_job_done(app_t *a, int what)
{
    switch (what) {
    case UJ_MENU:
        if (a->state == ST_MENU) title_refresh(a);
        else if (a->state == ST_MAP) map_refresh(a);
        else if (a->state == ST_HOUSE) house_refresh(a);
        break;
    case UJ_TURN:
        s_ui.s_turn_ready = true;
        preview_draw(a);
        break;
    case UJ_CARDS:
        if (a->state == ST_ALBUM) album_refresh(a);
        break;
    default:
        break;
    }
}

void mh_ui_tick(app_t *a, int dt_ms)
{
    if (a->state == ST_SHOP && s_ui.s_turn_ready) {
        s_ui.s_ms += (uint32_t)dt_ms;
        if (s_ui.s_ms >= 140) {
            s_ui.s_ms = 0;
            s_ui.s_frame = (s_ui.s_frame + 1) % 12;
            preview_draw(a);
        }
    }
}

void mh_ui_free(app_t *a)
{
    uimg_free(&a->ui_map);
    uimg_free(&a->ui_logo);
    uimg_free(&a->ui_house);
    uimg_free(&a->ui_marker);
    for (int i = 0; i < 7; i++) uimg_free(&a->ui_emblem[i]);
    for (int i = 0; i < 4; i++) uimg_free(&a->ui_trophy[i]);
    for (int i = 0; i < MH_LEVELS; i++) uimg_free(&a->ui_card[i]);
    for (int k = 0; k < 5; k++) mh_anim_free(&a->turn[k]);
    free(s_ui.s_prev_px);
    free(s_ui.pz_px);
    s_ui.s_prev_px = NULL;
    s_ui.pz_px = NULL;
}
