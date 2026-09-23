/*
 * MILA - the LVGL panels (see ml_ui.h)
 */
#include "ml_app.h"
#include "ml_audio.h"
#include "ml_link.h"
#include "ml_shop.h"
#include "ml_ui.h"

#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ACCENT   0xF07AA0
#define GOLD     0xFFC83A
#define PANEL_BG 0x17121E

/* --------------------------------------------------------------------------
 * Pictures: the worker's half (no LVGL)
 * -------------------------------------------------------------------------- */

static void uimg_free(ml_uimg_t *u)
{
    free(u->buf);
    memset(u, 0, sizeof(*u));
}

static bool uimg_alloc(ml_uimg_t *u, int w, int h)
{
    uimg_free(u);
    if (w <= 0 || h <= 0) return false;
    u->buf = (uint8_t *)ml_calloc((size_t)w * h, 3);
    if (!u->buf) return false;
    u->w = (int16_t)w;
    u->h = (int16_t)h;
    return true;
}

/* a sprite over the picture with its anchor at (x, y), alpha-over */
static void uimg_put(ml_uimg_t *u, const ml_spr_t *s, int fmt, const ml_lut_t *lut, int x, int y)
{
    if (!u->buf || !s) return;
    uint16_t *col = (uint16_t *)u->buf;
    uint8_t *al = u->buf + (size_t)u->w * u->h * 2;
    int bpp = fmt == ML_PX_COL ? 4 : fmt == ML_PX_RGB ? 2 : 3;
    x -= s->ax;
    y -= s->ay;
    for (int r = 0; r < s->h; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= u->h) continue;
        int x0, x1;
        const uint8_t *p = ml_spr_row(s, r, &x0, &x1);
        for (int k = x0; k < x1; k++, p += bpp) {
            int xx = x + k;
            if (xx < 0 || xx >= u->w) continue;
            uint16_t c;
            int a;
            if (fmt == ML_PX_LID) {
                a = (p[0] & 15) * 17;
                if (!a) continue;
                c = lut ? lut->c[p[0] >> 4][p[1] >> 2] : ml_rgb(p[1], p[1], p[1]);
            } else {
                c = (uint16_t)(p[0] | (p[1] << 8));
                a = fmt == ML_PX_RGB ? 255 : p[2];
                if (!a) continue;
            }
            size_t i = (size_t)yy * u->w + xx;
            int da = al[i];
            if (da == 0 || a >= 255) {
                col[i] = c;
                al[i] = (uint8_t)a;
            } else {
                col[i] = ml_blend(col[i], c, a);
                al[i] = (uint8_t)(da + ((255 - da) * a >> 8));
            }
        }
    }
}

/* a sheet's first frame alone, as a picture of its own size */
static bool uimg_sheet(ml_uimg_t *u, const char *name)
{
    ml_anim_t an;
    if (!ml_art_load(name, &an)) {
        uimg_free(u);
        return false;
    }
    const ml_spr_t *s = &an.f[0];
    bool ok = uimg_alloc(u, s->w, s->h);
    if (ok) uimg_put(u, s, an.fmt, NULL, s->ax, s->ay);
    ml_anim_free(&an);
    return ok;
}

static void make_lut(ml_lut_t *l, const char *item, uint32_t col)
{
    ml_pal_t p;
    char nm[32];
    memset(&p, 0, sizeof p);
    snprintf(nm, sizeof nm, "pal_%s", item);
    if (!ml_pal_load(nm, &p))
        for (int i = 0; i < 16; i++) p.c[i] = 0xC8C8C8;
    p.c[4] = 0x2A2430;
    if (col) p.c[1] = col;
    ml_lut_build(l, &p, 0xFFFFFF, 0);
}

/* what the shop shows: Mila turning with what she would wear, or a toy */
static void preview_build(app_t *a)
{
    ml_uimg_t *u = &a->ui_preview;
    const ml_item_t *it = ml_shop_item(a->shop_tab, a->shop_item);
    if (!it) {
        uimg_free(u);
        return;
    }
    char nm[48];
    if (it->cat == CAT_TOY) {
        snprintf(nm, sizeof nm, "icon_%s", it->id);
        if (!uimg_sheet(u, nm)) uimg_free(u);
        return;
    }
    const char *hat = it->cat == CAT_HAT ? it->id : a->prog.hat;
    const char *neck = it->cat == CAT_NECK ? it->id : a->prog.neck;
    uint32_t hc = it->cat == CAT_HAT ? (a->shop_col >= 0 && it->colour ? ml_shop_colour(a->shop_col) : 0)
                                     : (uint32_t)a->prog.hat_col;
    uint32_t nc = it->cat == CAT_NECK ? (a->shop_col >= 0 && it->colour ? ml_shop_colour(a->shop_col) : 0)
                                      : (uint32_t)a->prog.neck_col;
    ml_anim_t body = { 0 }, lh = { 0 }, ln = { 0 };
    snprintf(nm, sizeof nm, "mila_turn_%02d", a->shop_turn);
    ml_art_load(nm, &body);
    if (hat[0]) {
        snprintf(nm, sizeof nm, "%s_turn_%02d", hat, a->shop_turn);
        ml_art_load(nm, &lh);
    }
    if (neck[0]) {
        snprintf(nm, sizeof nm, "%s_turn_%02d", neck, a->shop_turn);
        ml_art_load(nm, &ln);
    }
    static ml_lut_t hl, nl;
    make_lut(&hl, hat, hc);
    make_lut(&nl, neck, nc);
    int w = 230, h = 250;
    if (uimg_alloc(u, w, h)) {
        int ax = w / 2, ay = h - 20;
        if (body.n) uimg_put(u, &body.f[0], body.fmt, NULL, ax, ay);
        if (ln.n) uimg_put(u, &ln.f[0], ln.fmt, &nl, ax, ay);
        if (lh.n) uimg_put(u, &lh.f[0], lh.fmt, &hl, ax, ay);
    }
    ml_anim_free(&body);
    ml_anim_free(&lh);
    ml_anim_free(&ln);
}

void ml_ui_job(app_t *a, int what)
{
    switch (what) {
    case UJ_ICONS:
        uimg_sheet(&a->ui_logo, "ui_logo");
        uimg_sheet(&a->ui_coin, "icon_coin");
        uimg_sheet(&a->ui_star, "icon_star");
        uimg_sheet(&a->ui_star_off, "icon_star_empty");
        break;
    case UJ_PREVIEW:
        preview_build(a);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * The LVGL side
 * -------------------------------------------------------------------------- */

static struct {
    lv_obj_t *boot, *b_logo, *b_title, *b_lbl, *b_track, *b_fill, *b_tip;
    bool      loader_on;
    lv_obj_t *pause, *p_title;
    lv_obj_t *result, *r_title, *r_stars[3], *r_line1, *r_line2, *r_gift, *r_next;
    lv_obj_t *shop, *s_coins, *s_tab[CAT_N], *s_img, *s_name, *s_note, *s_btn, *s_btn_lbl, *s_dots[ML_NCOLOURS];
    lv_obj_t *settings, *t_fx, *t_mus, *t_stats;
    lv_obj_t *lobby, *y_lbl, *y_visit, *y_race;
    int       result_next_w, result_next_l;
} s_ui;

static app_t *app_of(lv_event_t *e)
{
    return (app_t *)lv_event_get_user_data(e);
}

static void wrap(ml_uimg_t *u)
{
    u->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    u->dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    u->dsc.header.w = (uint32_t)u->w;
    u->dsc.header.h = (uint32_t)u->h;
    u->dsc.header.stride = (uint32_t)u->w * 2;
    u->dsc.data_size = (uint32_t)u->w * u->h * 3;
    u->dsc.data = u->buf;
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
    lv_obj_set_style_bg_color(b, lv_color_hex(0x241C30), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, aos_font_body, 0);
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

/* ---- boot and loading ---- */

/* the loader: one colourful screen for the boot, the casita, the map and
 * the levels, up until the scene's first frame is ready */
static const char *tip_of(int i)
{
    switch (i) {
    case 0: return _("Mantené el dedo sobre Mila para ver todo el nivel.");
    case 1: return _("Tocá una casilla y Mila camina sola hasta ahí.");
    case 2: return _("El botón BOOT deshace el último paso.");
    case 3: return _("Arrastrá el ratoncito y Mila lo va a perseguir.");
    case 4: return _("Terminar un mundo le regala algo a Mila.");
    default: return _("Cada día hay un regalito junto a la cucha.");
    }
}

#define BAR_W 240

static void build_boot(app_t *a, lv_obj_t *root)
{
    (void)a;
    s_ui.boot = panel(root, 0);
    lv_obj_set_style_bg_color(s_ui.boot, lv_color_hex(0xF6E8FF), 0);
    lv_obj_set_style_bg_grad_color(s_ui.boot, lv_color_hex(0x6B4CB8), 0);
    lv_obj_set_style_bg_grad_dir(s_ui.boot, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_ui.boot, LV_OPA_COVER, 0);
    s_ui.b_logo = image(s_ui.boot, NULL, 34, 70);
    s_ui.b_title = label(s_ui.boot, "", aos_font_title, 0xFFFFFF, 20, 226, AOS_SCREEN_W - 40);
    s_ui.b_track = lv_obj_create(s_ui.boot);
    lv_obj_remove_style_all(s_ui.b_track);
    lv_obj_set_size(s_ui.b_track, BAR_W, 14);
    lv_obj_set_pos(s_ui.b_track, (AOS_SCREEN_W - BAR_W) / 2, 286);
    lv_obj_set_style_radius(s_ui.b_track, 7, 0);
    lv_obj_set_style_bg_color(s_ui.b_track, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_ui.b_track, LV_OPA_30, 0);
    s_ui.b_fill = lv_obj_create(s_ui.b_track);
    lv_obj_remove_style_all(s_ui.b_fill);
    lv_obj_set_size(s_ui.b_fill, 14, 14);
    lv_obj_set_pos(s_ui.b_fill, 0, 0);
    lv_obj_set_style_radius(s_ui.b_fill, 7, 0);
    lv_obj_set_style_bg_color(s_ui.b_fill, lv_color_hex(0xFFC83A), 0);
    lv_obj_set_style_bg_grad_color(s_ui.b_fill, lv_color_hex(0xFF7AA8), 0);
    lv_obj_set_style_bg_grad_dir(s_ui.b_fill, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(s_ui.b_fill, LV_OPA_COVER, 0);
    s_ui.b_lbl = label(s_ui.boot, _("Cargando..."), aos_font_body, 0xF2E8FF, 20, 308, AOS_SCREEN_W - 40);
    s_ui.b_tip = label(s_ui.boot, "", aos_font_small, 0xFFFFFF, 30, 372, AOS_SCREEN_W - 60);
    lv_obj_set_style_text_opa(s_ui.b_tip, LV_OPA_80, 0);
}

void ml_ui_boot_text(app_t *a, const char *txt)
{
    (void)a;
    lv_label_set_text(s_ui.b_lbl, txt);
}

void ml_ui_loading_text(app_t *a, const char *txt)
{
    (void)a;
    lv_label_set_text(s_ui.b_title, txt);
}

void ml_ui_loader(app_t *a, bool show, int pct)
{
    (void)a;
    if (show && !s_ui.loader_on) {
        s_ui.loader_on = true;
        lv_label_set_text(s_ui.b_tip, tip_of((int)(lv_tick_get() / 7 % 6)));
        lv_obj_set_width(s_ui.b_fill, 14);
        lv_obj_remove_flag(s_ui.boot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_ui.boot);
    } else if (!show && s_ui.loader_on) {
        s_ui.loader_on = false;
        lv_obj_add_flag(s_ui.boot, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_ui.b_title, "");
    }
    if (show) {
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        int w = 14 + (BAR_W - 14) * pct / 100;
        lv_obj_set_width(s_ui.b_fill, w);
    }
}

bool ml_ui_loader_on(void)
{
    return s_ui.loader_on;
}

/* ---- pause ---- */

static void resume_cb(lv_event_t *e) { ml_snd(SND_SELECT); mla_resume(app_of(e)); }

static void restart_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    ml_snd(SND_SELECT);
    ml_play_restart(&a->play);
    mla_resume(a);
}

static void to_map_cb(lv_event_t *e) { ml_snd(SND_SELECT); mla_set_state(app_of(e), ST_MAP); }
static void to_home_cb(lv_event_t *e) { ml_snd(SND_SELECT); mla_set_state(app_of(e), ST_CASITA); }

static void build_pause(app_t *a, lv_obj_t *root)
{
    s_ui.pause = panel(root, 200);
    s_ui.p_title = label(s_ui.pause, "", aos_font_title, 0xFFFFFF, 20, 50, AOS_SCREEN_W - 40);
    int w = 230, x = (AOS_SCREEN_W - w) / 2;
    button(s_ui.pause, _("Seguir"), x, 130, w, 56, GOLD, resume_cb, a, NULL);
    button(s_ui.pause, _("Empezar de nuevo"), x, 200, w, 56, ACCENT, restart_cb, a, NULL);
    button(s_ui.pause, _("Mapa"), x, 270, w, 56, ACCENT, to_map_cb, a, NULL);
    button(s_ui.pause, _("Casita"), x, 340, w, 56, ACCENT, to_home_cb, a, NULL);
}

/* ---- results ---- */

static void next_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    ml_snd(SND_SELECT);
    if (a->race) {
        ml_link_end(a);
        mla_set_state(a, ST_CASITA);
        return;
    }
    if (s_ui.result_next_w >= 0) mla_level_start(a, s_ui.result_next_w, s_ui.result_next_l);
    else mla_set_state(a, ST_MAP);
}

static void again_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    ml_snd(SND_SELECT);
    mla_level_start(a, a->world, a->level);
}

static void build_result(app_t *a, lv_obj_t *root)
{
    s_ui.result = panel(root, 232);
    s_ui.r_title = label(s_ui.result, _("¡Nivel completo!"), aos_font_title, GOLD, 20, 40, AOS_SCREEN_W - 40);
    for (int i = 0; i < 3; i++) s_ui.r_stars[i] = image(s_ui.result, NULL, 84 + i * 70, 96);
    s_ui.r_line1 = label(s_ui.result, "", aos_font_body, 0xFFFFFF, 20, 172, AOS_SCREEN_W - 40);
    s_ui.r_line2 = label(s_ui.result, "", aos_font_body, 0xFFE08A, 20, 200, AOS_SCREEN_W - 40);
    s_ui.r_gift = label(s_ui.result, "", aos_font_body, 0xFFA8C8, 20, 232, AOS_SCREEN_W - 40);
    int w = 230, x = (AOS_SCREEN_W - w) / 2;
    s_ui.r_next = button(s_ui.result, _("Siguiente"), x, 280, w, 54, GOLD, next_cb, a, NULL);
    button(s_ui.result, _("Otra vez"), x, 342, 110, 48, ACCENT, again_cb, a, NULL);
    button(s_ui.result, _("Mapa"), x + 120, 342, 110, 48, ACCENT, to_map_cb, a, NULL);
}

void ml_ui_result_fill(app_t *a, int stars, int earned, int moves, int par, const char *gift)
{
    char b[96];
    lv_label_set_text(s_ui.r_title, _("¡Nivel completo!"));
    for (int i = 0; i < 3; i++)
        lv_image_set_src(s_ui.r_stars[i], i < stars ? (a->ui_star.buf ? (const void *)&a->ui_star.dsc : NULL)
                                                    : (a->ui_star_off.buf ? (const void *)&a->ui_star_off.dsc : NULL));
    snprintf(b, sizeof b, _("Movimientos: %d   (par %d)"), moves, par);
    lv_label_set_text(s_ui.r_line1, b);
    if (earned > 0) snprintf(b, sizeof b, _("+%d monedas"), earned);
    else snprintf(b, sizeof b, "%s", stars == 3 ? _("¡Perfecto!") : _("Probá llegar al par"));
    lv_label_set_text(s_ui.r_line2, b);
    b[0] = 0;
    if (gift) {
        const ml_item_t *it = ml_shop_find(gift);
        snprintf(b, sizeof b, _("¡Regalo: %s!"), it ? _(it->name) : gift);
    }
    lv_label_set_text(s_ui.r_gift, b);
    /* the next level: in this world, else the next world if open */
    s_ui.result_next_w = -1;
    int w = a->world, l = a->level + 1;
    if (l >= a->worlds.w[w].nlevels) {
        w++;
        l = 0;
    }
    if (w < a->worlds.nworlds && mla_level_open(a, w, l)) {
        s_ui.result_next_w = w;
        s_ui.result_next_l = l;
    }
    lv_obj_t *lbl = lv_obj_get_child(s_ui.r_next, 0);
    lv_label_set_text(lbl, s_ui.result_next_w >= 0 ? _("Siguiente") : _("Mapa"));
}

void ml_ui_race_fill(app_t *a, bool won, int me, int them, int earned)
{
    char b[96];
    lv_label_set_text(s_ui.r_title, won ? _("¡Ganaste la carrera!") : _("Ganó tu amigo"));
    for (int i = 0; i < 3; i++)
        lv_image_set_src(s_ui.r_stars[i], won && a->ui_star.buf ? (const void *)&a->ui_star.dsc : NULL);
    snprintf(b, sizeof b, _("Vos: %d movimientos   %s: %d"), me, a->partner[0] ? a->partner : "?", them);
    lv_label_set_text(s_ui.r_line1, b);
    snprintf(b, sizeof b, _("+%d monedas"), earned);
    lv_label_set_text(s_ui.r_line2, b);
    lv_label_set_text(s_ui.r_gift, "");
    s_ui.result_next_w = -1;
    lv_label_set_text(lv_obj_get_child(s_ui.r_next, 0), _("Casita"));
}

/* ---- the shop ---- */

static bool is_gift(const app_t *a, const char *id, int *world)
{
    for (int i = 0; i < a->worlds.nworlds; i++)
        if (!strcmp(a->worlds.w[i].gift, id)) {
            if (world) *world = i;
            return true;
        }
    return false;
}

static bool worn(const app_t *a, const ml_item_t *it)
{
    return (it->cat == CAT_HAT && !strcmp(a->prog.hat, it->id)) || (it->cat == CAT_NECK && !strcmp(a->prog.neck, it->id));
}

static void shop_refresh(app_t *a)
{
    char b[96];
    const ml_item_t *it = ml_shop_item(a->shop_tab, a->shop_item);
    snprintf(b, sizeof b, "%d", (int)a->prog.coins);
    lv_label_set_text(s_ui.s_coins, b);
    for (int i = 0; i < CAT_N; i++) {
        bool on = i == a->shop_tab;
        lv_obj_set_style_bg_color(s_ui.s_tab[i], lv_color_hex(on ? 0x7A4FC8 : 0xFFFFFF), 0);
        lv_obj_set_style_border_color(s_ui.s_tab[i], lv_color_hex(0x7A4FC8), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_ui.s_tab[i], 0), lv_color_hex(on ? 0xFFFFFF : 0x3A2450), 0);
    }
    if (!it) return;
    lv_label_set_text(s_ui.s_name, _(it->name));
    bool own = ml_prog_owns(&a->prog, it->id);
    int gw = -1;
    bool gift = is_gift(a, it->id, &gw);
    const char *act;
    b[0] = 0;
    if (own && it->cat == CAT_TOY) act = _("En la casita");
    else if (own && worn(a, it)) act = _("Sacar");
    else if (own) act = _("Ponérselo");
    else if (gift) {
        char wn[48];
        snprintf(b, sizeof b, _("Premio por terminar %s"), mla_world_name(a, gw, wn, sizeof wn));
        act = _("Bloqueado");
    } else {
        static char price[32];
        snprintf(price, sizeof price, _("Comprar  %d"), it->price);
        act = price;
    }
    lv_label_set_text(s_ui.s_btn_lbl, act);
    lv_label_set_text(s_ui.s_note, b);
    bool dots = own && it->colour;
    for (int i = 0; i < ML_NCOLOURS; i++) {
        if (dots) lv_obj_remove_flag(s_ui.s_dots[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_ui.s_dots[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_width(s_ui.s_dots[i], i == a->shop_col ? 3 : 0, 0);
    }
    if (a->ui_preview.buf) {
        wrap(&a->ui_preview);
        lv_image_set_src(s_ui.s_img, &a->ui_preview.dsc);
        /* Mila turning is drawn 1.3 x (the toys' icons 2 x) */
        bool mila = a->shop_tab != CAT_TOY;
        lv_image_set_scale(s_ui.s_img, mila ? 333 : 512);
        lv_image_set_pivot(s_ui.s_img, a->ui_preview.w / 2, a->ui_preview.h / 2);
        lv_obj_set_pos(s_ui.s_img, (AOS_SCREEN_W - a->ui_preview.w) / 2, (mila ? -6 : 80) + (250 - a->ui_preview.h) / 2);
    } else {
        lv_image_set_src(s_ui.s_img, NULL);
    }
}

static void shop_preview(app_t *a)
{
    /* the colour it has now, when worn */
    const ml_item_t *it = ml_shop_item(a->shop_tab, a->shop_item);
    a->shop_col = -1;
    if (it && worn(a, it)) {
        uint32_t cur = (uint32_t)(it->cat == CAT_HAT ? a->prog.hat_col : a->prog.neck_col);
        for (int i = 0; i < ML_NCOLOURS; i++)
            if (cur && ml_shop_colour(i) == cur) a->shop_col = i;
    }
    mla_ui_job(a, UJ_PREVIEW);
}

static void tab_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    lv_obj_t *t = lv_event_get_target_obj(e);
    for (int i = 0; i < CAT_N; i++)
        if (t == s_ui.s_tab[i]) a->shop_tab = i;
    a->shop_item = 0;
    ml_snd(SND_SELECT);
    shop_preview(a);
    shop_refresh(a);
}

static void arrow_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    int d = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    int n = ml_shop_count(a->shop_tab);
    a->shop_item = (a->shop_item + d + n) % n;
    ml_snd(SND_SELECT);
    shop_preview(a);
    shop_refresh(a);
}

static void turn_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    if (a->shop_tab == CAT_TOY) return;
    a->shop_turn = (a->shop_turn + 1) % 12;
    mla_ui_job(a, UJ_PREVIEW);
}

static void dot_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    const ml_item_t *it = ml_shop_item(a->shop_tab, a->shop_item);
    if (!it) return;
    a->shop_col = i;
    if (worn(a, it)) {
        if (it->cat == CAT_HAT) a->prog.hat_col = (int32_t)ml_shop_colour(i);
        else a->prog.neck_col = (int32_t)ml_shop_colour(i);
        mla_save(a);
        mla_outfit(a);
    }
    ml_snd(SND_SELECT);
    mla_ui_job(a, UJ_PREVIEW);
    shop_refresh(a);
}

static void buy_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    const ml_item_t *it = ml_shop_item(a->shop_tab, a->shop_item);
    if (!it) return;
    bool own = ml_prog_owns(&a->prog, it->id);
    if (!own) {
        if (is_gift(a, it->id, NULL)) {
            ml_snd(SND_BUMP);
            return;
        }
        if (a->prog.coins < it->price) {
            ml_snd(SND_BUMP);
            aos_ui_toast(_("Te faltan monedas"), 1400);
            return;
        }
        a->prog.coins -= it->price;
        ml_prog_add(&a->prog, it->id);
        ml_snd(SND_BUY);
        own = true;
        if (it->cat == CAT_TOY) {
            mla_save(a);
            shop_refresh(a);
            return;
        }
    }
    if (it->cat == CAT_TOY) return;
    /* put it on, or take it off */
    char *slot = it->cat == CAT_HAT ? a->prog.hat : a->prog.neck;
    int32_t *col = it->cat == CAT_HAT ? &a->prog.hat_col : &a->prog.neck_col;
    if (!strcmp(slot, it->id)) {
        slot[0] = 0;
    } else {
        snprintf(slot, 20, "%s", it->id);
        *col = a->shop_col >= 0 && it->colour ? (int32_t)ml_shop_colour(a->shop_col) : 0;
    }
    ml_snd(SND_SELECT);
    mla_save(a);
    mla_outfit(a);
    shop_refresh(a);
}

static void build_shop(app_t *a, lv_obj_t *root)
{
    /* a light room: a black kitten on black loses every detail */
    s_ui.shop = panel(root, 0);
    lv_obj_set_style_bg_color(s_ui.shop, lv_color_hex(0xFFF4EC), 0);
    lv_obj_set_style_bg_grad_color(s_ui.shop, lv_color_hex(0xF3CFE0), 0);
    lv_obj_set_style_bg_grad_dir(s_ui.shop, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_ui.shop, LV_OPA_COVER, 0);
    /* a round spotlight where she stands */
    lv_obj_t *spot = lv_obj_create(s_ui.shop);
    lv_obj_remove_style_all(spot);
    lv_obj_set_size(spot, 236, 236);
    lv_obj_set_pos(spot, (AOS_SCREEN_W - 236) / 2, 86);
    lv_obj_set_style_radius(spot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(spot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_color(spot, lv_color_hex(0xFBE3EE), 0);
    lv_obj_set_style_bg_grad_dir(spot, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(spot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(spot, LV_OBJ_FLAG_CLICKABLE);
    label(s_ui.shop, _("Tienda"), aos_font_title, 0x3A2450, 20, 8, AOS_SCREEN_W - 40);
    s_ui.s_coins = label(s_ui.shop, "0", aos_font_body, 0xA86A00, AOS_SCREEN_W - 96, 14, 80);
    static const char *const tabs[CAT_N] = { N_("Gorros"), N_("Cuello"), N_("Juguetes") };
    for (int i = 0; i < CAT_N; i++) {
        s_ui.s_tab[i] = button(s_ui.shop, _(tabs[i]), 16 + i * 114, 44, 108, 34, 0xFFFFFF, tab_cb, a, NULL);
    }
    s_ui.s_img = image(s_ui.shop, NULL, 70, 80);
    lv_obj_add_flag(s_ui.s_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.s_img, turn_cb, LV_EVENT_CLICKED, a);
    lv_obj_t *l = button(s_ui.shop, "<", 8, 180, 40, 60, ACCENT, arrow_cb, a, NULL);
    lv_obj_set_user_data(l, (void *)(intptr_t)-1);
    lv_obj_t *r = button(s_ui.shop, ">", AOS_SCREEN_W - 48, 180, 40, 60, ACCENT, arrow_cb, a, NULL);
    lv_obj_set_user_data(r, (void *)(intptr_t)1);
    s_ui.s_name = label(s_ui.shop, "", aos_font_title, 0x3A2450, 20, 324, AOS_SCREEN_W - 40);
    s_ui.s_note = label(s_ui.shop, "", aos_font_small, 0x7A6488, 20, 354, AOS_SCREEN_W - 40);
    for (int i = 0; i < ML_NCOLOURS; i++) {
        lv_obj_t *d = lv_obj_create(s_ui.shop);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 26, 26);
        lv_obj_set_pos(d, AOS_SCREEN_W / 2 - ML_NCOLOURS * 17 + i * 34, 358);
        lv_obj_set_style_radius(d, 13, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(ml_shop_colour(i)), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(d, lv_color_hex(0x3A2450), 0);
        lv_obj_add_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(d, (void *)(intptr_t)i);
        lv_obj_add_event_cb(d, dot_cb, LV_EVENT_CLICKED, a);
        s_ui.s_dots[i] = d;
    }
    s_ui.s_btn = button(s_ui.shop, "", (AOS_SCREEN_W - 220) / 2, 392, 220, 46, GOLD, buy_cb, a, &s_ui.s_btn_lbl);
}

/* ---- settings ---- */

static void settings_refresh(app_t *a)
{
    lv_obj_set_style_bg_color(s_ui.t_fx, lv_color_hex((a->prog.snd & 1) ? 0x3A9A6A : 0x3A2A3A), 0);
    lv_obj_set_style_bg_color(s_ui.t_mus, lv_color_hex((a->prog.snd & 2) ? 0x3A9A6A : 0x3A2A3A), 0);
    char b[240];
    snprintf(b, sizeof b, _("Niveles resueltos: %d\nMovimientos: %d\nCaricias: %d\nEstrellas: %d"),
             (int)a->prog.stat[SX_SOLVED], (int)a->prog.stat[SX_MOVES], (int)a->prog.stat[SX_PETS],
             ml_prog_total_stars(&a->prog, &a->worlds));
    lv_label_set_text(s_ui.t_stats, b);
}

static void sound_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    int bit = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    a->prog.snd ^= bit;
    ml_audio_enable(a->prog.snd & 1, (a->prog.snd >> 1) & 1);
    mla_save(a);
    ml_snd(SND_SELECT);
    settings_refresh(a);
}

static void build_settings(app_t *a, lv_obj_t *root)
{
    s_ui.settings = panel(root, 256);
    label(s_ui.settings, _("Ajustes"), aos_font_title, 0xFFFFFF, 20, 16, AOS_SCREEN_W - 40);
    s_ui.t_fx = button(s_ui.settings, _("Efectos"), 34, 70, 140, 50, ACCENT, sound_cb, a, NULL);
    lv_obj_set_user_data(s_ui.t_fx, (void *)(intptr_t)1);
    s_ui.t_mus = button(s_ui.settings, _("Música"), AOS_SCREEN_W - 174, 70, 140, 50, ACCENT, sound_cb, a, NULL);
    lv_obj_set_user_data(s_ui.t_mus, (void *)(intptr_t)2);
    s_ui.t_stats = label(s_ui.settings, "", aos_font_body, 0xE8DCE8, 20, 150, AOS_SCREEN_W - 40);
    button(s_ui.settings, _("Volver"), (AOS_SCREEN_W - 180) / 2, 370, 180, 50, GOLD, to_home_cb, a, NULL);
}

/* ---- lobby (ml_link.c talks) ---- */

static void lobby_cancel_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    ml_link_end(a);
    mla_set_state(a, ST_CASITA);
}

static void visit_cb(lv_event_t *e) { ml_link_choose(app_of(e), LINK_VISIT); }
static void race_cb(lv_event_t *e) { ml_link_choose(app_of(e), LINK_RACE); }

static void build_lobby(app_t *a, lv_obj_t *root)
{
    s_ui.lobby = panel(root, 256);
    label(s_ui.lobby, _("Con un amigo"), aos_font_title, 0xFFFFFF, 20, 20, AOS_SCREEN_W - 40);
    s_ui.y_lbl = label(s_ui.lobby, "", aos_font_body, 0xE8DCE8, 20, 90, AOS_SCREEN_W - 40);
    int w = 250, x = (AOS_SCREEN_W - w) / 2;
    s_ui.y_visit = button(s_ui.lobby, _("Visitar su casita"), x, 190, w, 54, GOLD, visit_cb, a, NULL);
    s_ui.y_race = button(s_ui.lobby, _("Carrera en un nivel"), x, 256, w, 54, ACCENT, race_cb, a, NULL);
    button(s_ui.lobby, _("Cancelar"), x, 360, w, 48, ACCENT, lobby_cancel_cb, a, NULL);
}

void ml_ui_lobby_fill(app_t *a, const char *txt, bool host)
{
    (void)a;
    lv_label_set_text(s_ui.y_lbl, txt);
    if (host) {
        lv_obj_remove_flag(s_ui.y_visit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_ui.y_race, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ui.y_visit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.y_race, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- the whole ---- */

void ml_ui_build(app_t *a, lv_obj_t *root)
{
    memset(&s_ui, 0, sizeof s_ui);
    build_boot(a, root);
    build_pause(a, root);
    build_result(a, root);
    build_shop(a, root);
    build_settings(a, root);
    build_lobby(a, root);
}

void ml_ui_show(app_t *a, int st)
{
    lv_obj_t *all[] = { s_ui.pause, s_ui.result, s_ui.shop, s_ui.settings, s_ui.lobby };
    lv_obj_t *want = NULL;
    switch (st) {
    case ST_BOOT:
    case ST_LOADING:
        ml_ui_loader(a, true, 0);
        break;
    case ST_PAUSE: {
        char nm[64];
        lv_label_set_text(s_ui.p_title, mla_level_name(a, a->world, a->level, nm, sizeof nm));
        want = s_ui.pause;
        break;
    }
    case ST_RESULT: want = s_ui.result; break;
    case ST_SHOP:
        a->shop_turn = 0;
        shop_preview(a);
        shop_refresh(a);
        want = s_ui.shop;
        break;
    case ST_SETTINGS:
        settings_refresh(a);
        want = s_ui.settings;
        break;
    case ST_LOBBY: want = s_ui.lobby; break;
    default: break;
    }
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        if (!all[i]) continue;
        if (all[i] == want) lv_obj_remove_flag(all[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(all[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void ml_ui_job_done(app_t *a, int what)
{
    switch (what) {
    case UJ_ICONS:
        if (a->ui_logo.buf) {
            wrap(&a->ui_logo);
            lv_image_set_src(s_ui.b_logo, &a->ui_logo.dsc);
            lv_obj_set_pos(s_ui.b_logo, (AOS_SCREEN_W - a->ui_logo.w) / 2, 80);
        }
        if (a->ui_star.buf) wrap(&a->ui_star);
        if (a->ui_star_off.buf) wrap(&a->ui_star_off);
        if (a->ui_coin.buf) wrap(&a->ui_coin);
        break;
    case UJ_PREVIEW:
        if (a->state == ST_SHOP) shop_refresh(a);
        break;
    default:
        break;
    }
}

void ml_ui_tick(app_t *a, int dt_ms)
{
    (void)a;
    (void)dt_ms;
}

void ml_ui_free(app_t *a)
{
    uimg_free(&a->ui_logo);
    uimg_free(&a->ui_coin);
    uimg_free(&a->ui_star);
    uimg_free(&a->ui_star_off);
    uimg_free(&a->ui_preview);
}
