/*
 * MONSTER HOP - the desktop's wide screens (included by mh_ui.c when built
 * with MH_DESKTOP; the watch never sees this)
 *
 * On a computer the frame is 800 x 450 and the menus need not be a watch's
 * column in the middle: the title's map fills the screen and drifts, Tommy
 * turns in what he wears beside the title and in front of his house, the
 * map gets a level card and the zones at its sides, and the pause shows the
 * level's plan big with the counts beside it. Positions are the frame's:
 * the panels' content starts at the watch's column, WX/WY move them back.
 */

#define WX(x) ((x) - (MH_W - AOS_SCREEN_W) / 2)
#define WY(y) ((y) - (MH_H - AOS_SCREEN_H) / 2)

static struct {
    /* title and house */
    lv_obj_t *t_bg, *t_logo, *t_coins, *h_img, *h_coins;
    lv_obj_t *turn[2];          /* Tommy on the title and in the house     */
    mh_uimg_t bg, logo, house;  /* the pictures, scaled for the frame       */
    uint32_t *turn_px;          /* his turntable, ARGB (both canvases)      */
    int8_t    turn_eq[CAT_N];   /* what the loaded turntable wears          */
    bool      turn_ok, turn_asked;
    int       turn_frame;
    uint32_t  turn_ms;
    float     pan;
    /* the map */
    lv_obj_t *m_emb, *m_zone, *m_title, *m_star[3], *m_info, *m_play;
    lv_obj_t *m_zbtn[6], *m_zemb[6], *m_zname[6], *m_zstars[6];
    int       sel;
    /* the pause */
    lv_obj_t *pz, *pz_title, *pz_zone, *pz_info;
    /* stats, settings */
    lv_obj_t *st_num[12], *st_cap[12], *tr_count;
    lv_obj_t *c_full, *c_lang[3], *lang_note;
    uint32_t *pz_px;
} W = { .sel = -1 };

#define TURN_SCALE_TITLE 400    /* 256 = 1:1 */
#define TURN_SCALE_HOUSE 380
#define PZ_W  380               /* the pause's plan: the most it takes      */
#define PZ_H  430

/* the zones on the map, in the order the side list shows them */
static const struct { uint8_t zone, emblem; } s_wz[6] = {
    { ZONE_CITY, 0 }, { ZONE_DINO, 7 }, { ZONE_FOREST, 3 }, { ZONE_BAY, 8 }, { ZONE_CASTLE, 1 }, { ZONE_DESERT, 2 },
};

/* ---- pictures ---- */

/* a smooth resize of an RGB565A8 picture (colour weighted by alpha, so the
 * edges keep no dark fringe) */
static bool wide_scale(mh_uimg_t *d, const mh_uimg_t *s, int dw, int dh)
{
    if (!s->buf || dw <= 0 || dh <= 0 || !uimg_alloc(d, dw, dh)) return false;
    const uint16_t *sc = (const uint16_t *)s->buf;
    const uint8_t *sa = s->buf + (size_t)s->w * s->h * 2;
    uint16_t *dc = (uint16_t *)d->buf;
    uint8_t *da = d->buf + (size_t)dw * dh * 2;
    float kx = (float)s->w / dw, ky = (float)s->h / dh;
    for (int y = 0; y < dh; y++) {
        float fy = (y + 0.5f) * ky - 0.5f;
        int y0 = mh_ifloor(fy);
        float wy = fy - y0;
        int y1 = y0 + 1;
        if (y0 < 0) y0 = 0;
        if (y1 > s->h - 1) y1 = s->h - 1;
        if (y0 > s->h - 1) y0 = s->h - 1;
        for (int x = 0; x < dw; x++) {
            float fx = (x + 0.5f) * kx - 0.5f;
            int x0 = mh_ifloor(fx);
            float wx = fx - x0;
            int x1 = x0 + 1;
            if (x0 < 0) x0 = 0;
            if (x1 > s->w - 1) x1 = s->w - 1;
            if (x0 > s->w - 1) x0 = s->w - 1;
            int idx[4] = { y0 * s->w + x0, y0 * s->w + x1, y1 * s->w + x0, y1 * s->w + x1 };
            float wt[4] = { (1 - wx) * (1 - wy), wx * (1 - wy), (1 - wx) * wy, wx * wy };
            float r = 0, g = 0, b = 0, al = 0;
            for (int k = 0; k < 4; k++) {
                int R, G, B;
                mh_unpack(sc[idx[k]], &R, &G, &B);
                float w = wt[k] * sa[idx[k]];
                r += R * w;
                g += G * w;
                b += B * w;
                al += w;
            }
            size_t o = (size_t)y * dw + x;
            if (al > 0.5f) {
                dc[o] = mh_rgb((int)(r / al), (int)(g / al), (int)(b / al));
                da[o] = (uint8_t)(al > 255 ? 255 : al);
            }
        }
    }
    return true;
}

static void wide_set(lv_obj_t *img, mh_uimg_t *u)
{
    if (!img || !u->buf) return;
    wrap(u);
    lv_image_set_src(img, &u->dsc);
}

/* ---- Tommy's turntable: what he wears, turning ---- */

static void turn_ask(app_t *a)
{
    if (W.turn_ok && !memcmp(W.turn_eq, a->prog.eq, CAT_N)) return;
    memcpy(a->try_eq, a->prog.eq, sizeof a->try_eq);
    memcpy(W.turn_eq, a->prog.eq, CAT_N);
    W.turn_asked = true;
    mha_ui_job(a, UJ_TURN);
}

static void turn_draw(app_t *a)
{
    if (!W.turn_px || !a->turn[0].n) return;
    mh_uimg_t u;
    memset(&u, 0, sizeof u);
    if (!uimg_alloc(&u, PREV_W, PREV_H)) return;
    int f = W.turn_frame;
    int fx = PREV_W / 2 + 18, fy = PREV_H - 8;
    if (a->turn[4].n) {
        const mh_spr_t *s = &a->turn[4].f[f % a->turn[4].n];
        uimg_put(&u, s, a->turn[4].fmt, &a->turn_lut[4], fx - 66 - s->ax, fy - s->ay);
    }
    for (int k = 0; k < 4; k++) {
        if (!a->turn[k].n) continue;
        const mh_spr_t *s = &a->turn[k].f[f % a->turn[k].n];
        uimg_put(&u, s, a->turn[k].fmt, &a->turn_lut[k], fx - s->ax, fy - s->ay);
    }
    const uint16_t *c = (const uint16_t *)u.buf;
    const uint8_t *al = u.buf + (size_t)PREV_W * PREV_H * 2;
    for (int i = 0; i < PREV_W * PREV_H; i++) {
        int r, g, b;
        mh_unpack(c[i], &r, &g, &b);
        W.turn_px[i] = (uint32_t)al[i] << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
    uimg_free(&u);
    for (int k = 0; k < 2; k++) if (W.turn[k]) lv_obj_invalidate(W.turn[k]);
}

/* a turntable canvas whose feet stand at the frame's (fx, fy) */
static lv_obj_t *turn_canvas(lv_obj_t *p, int fx, int fy, int scale)
{
    if (!W.turn_px) {
        W.turn_px = (uint32_t *)mh_calloc((size_t)PREV_W * PREV_H, 4);
        if (!W.turn_px) return NULL;
    }
    lv_obj_t *c = lv_canvas_create(p);
    lv_canvas_set_buffer(c, W.turn_px, PREV_W, PREV_H, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(c, WX(fx - PREV_W / 2), WY(fy - PREV_H));
    lv_image_set_pivot(c, PREV_W / 2, PREV_H);
    lv_image_set_scale(c, (uint32_t)scale);
    lv_image_set_antialias(c, true);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

/* ---- the title ---- */

static void wide_build_title(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_MENU] = panel(root, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x0C0A14), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    W.t_bg = image(p, NULL, WX(0), WY(0));
    /* darker towards the bottom, where the buttons are */
    lv_obj_t *dim = lv_obj_create(p);
    lv_obj_remove_style_all(dim);
    lv_obj_set_size(dim, MH_W, MH_H);
    lv_obj_set_pos(dim, WX(0), WY(0));
    lv_obj_set_style_bg_color(dim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_color(dim, lv_color_hex(0x05030A), 0);
    lv_obj_set_style_bg_grad_dir(dim, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(dim, 40, 0);
    lv_obj_set_style_bg_grad_opa(dim, 190, 0);
    lv_obj_set_style_bg_opa(dim, LV_OPA_COVER, 0);
    lv_obj_remove_flag(dim, LV_OBJ_FLAG_CLICKABLE);
    W.turn[0] = turn_canvas(p, 210, 440, TURN_SCALE_TITLE);
    W.t_logo = image(p, NULL, WX(420), WY(18));
    button(p, _("Jugar"), WX(470), WY(236), 270, 64, ACCENT, title_play_cb, a, NULL);
    button(p, _("La casa de Tommy"), WX(470), WY(312), 270, 52, 0x3AB07A, title_house_cb, a, NULL);
    W.t_coins = label(p, "", aos_font_body, 0xFFD060, WX(440), WY(384), 330);
}

static void wide_title_refresh(app_t *a)
{
    if (a->ui_map.buf && !W.bg.buf) wide_scale(&W.bg, &a->ui_map, MH_W, a->ui_map.h * MH_W / a->ui_map.w);
    wide_set(W.t_bg, &W.bg);
    if (a->ui_logo.buf && !W.logo.buf) {
        int w = 360, h = a->ui_logo.h * 360 / a->ui_logo.w;
        wide_scale(&W.logo, &a->ui_logo, w, h);
    }
    wide_set(W.t_logo, &W.logo);
    char b[64];
    coins_text(b, sizeof b, a);
    lv_label_set_text(W.t_coins, b);
    if (a->menu_art) turn_ask(a);
}

/* ---- the house ---- */

static void wide_build_house(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_HOUSE] = panel(root, 256);
    W.h_img = image(p, NULL, WX(14), WY(24));
    /* Tommy in front of his house, with his pet */
    W.turn[1] = turn_canvas(p, 236, 444, TURN_SCALE_HOUSE);
    label(p, _("La casa de Tommy"), aos_font_title, 0xFFFFFF, WX(460), WY(16), 330);
    W.h_coins = label(p, "", aos_font_body, 0xFFD060, WX(460), WY(58), 330);
    const char *names[8] = { _("Guardarropas"), _("Tienda"), _("Álbum"), _("Trofeos"), _("Estadísticas"),
                             _("Jugar con un amigo"), _("Ajustes"), _("Al mapa") };
    uint32_t cols[8] = { 0x3AB07A, GOLD, 0x5AA0F0, 0xE8A030, 0x9A90B0, 0xF05A8A, 0x5A4A7A, ACCENT };
    for (int i = 0; i < 8; i++) {
        lv_obj_t *b = button(p, names[i], WX(462 + (i & 1) * 166), WY(100 + (i >> 1) * 64), 158, 54, cols[i], house_cb,
                             a, NULL);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
    }
}

static void wide_house_refresh(app_t *a)
{
    if (a->ui_house.buf && !W.house.buf) wide_scale(&W.house, &a->ui_house, 430, a->ui_house.h * 430 / a->ui_house.w);
    wide_set(W.h_img, &W.house);
    char b[64];
    coins_text(b, sizeof b, a);
    lv_label_set_text(W.h_coins, b);
    if (a->menu_art) turn_ask(a);
}

/* ---- the map: a level's card on the left, the zones on the right ---- */

static void wide_select(app_t *a, int i);

static void wide_play_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    if (W.sel < 0 || !mha_level_open(a, W.sel)) return;
    mh_snd(SND_GO);
    mha_level_start(a, W.sel);
}

static void wide_zone_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    int k = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    int first = mh_zone_first(s_wz[k].zone);
    if (first < 0) return;
    mh_snd(SND_SELECT);
    /* the zone's next level to play, else its first */
    int pick = first;
    for (int i = first; i < first + 4; i++) {
        if (mha_level_open(a, i) && !a->prog.stars[i]) {
            pick = i;
            break;
        }
    }
    wide_select(a, pick);
    if (a->spots_ok) {
        int want = a->spots[pick][1] - AOS_SCREEN_H / 2;
        if (want < 0) want = 0;
        if (a->ui_map.h && want > a->ui_map.h - AOS_SCREEN_H) want = a->ui_map.h - AOS_SCREEN_H;
        lv_obj_scroll_to_y(s_ui.m_scroll, want, LV_ANIM_ON);
    }
}

static lv_obj_t *side_box(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, WX(x), WY(y));
    lv_obj_set_style_bg_color(c, lv_color_hex(0x1C1628), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x3A3050), 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static void wide_build_map(app_t *a, lv_obj_t *p)
{
    lv_obj_t *c = side_box(p, 8, 10, 200, 430);
    W.m_emb = image(c, NULL, 52, 10);
    W.m_zone = label(c, "", aos_font_small, 0xC8C0D8, 8, 110, 184);
    W.m_title = label(c, "", aos_font_body, 0xFFFFFF, 8, 134, 184);
    for (int k = 0; k < 3; k++) {
        W.m_star[k] = image(c, &s_ui.d_star_big_off, 46 + k * 38, 196);
    }
    W.m_info = label(c, "", aos_font_small, 0xC8C0D8, 8, 238, 184);
    W.m_play = button(c, _("Jugar"), 18, 352, 164, 58, 0x30C060, wide_play_cb, a, NULL);

    c = side_box(p, 592, 10, 200, 430);
    label(c, _("Zonas"), aos_font_body, 0xFFFFFF, 0, 8, 200);
    for (int k = 0; k < 6; k++) {
        lv_obj_t *b = button(c, "", 8, 40 + k * 64, 184, 58, 0x5A4A7A, wide_zone_cb, a, NULL);
        lv_obj_set_user_data(b, (void *)(intptr_t)k);
        lv_obj_add_flag(lv_obj_get_child(b, 0), LV_OBJ_FLAG_HIDDEN);
        W.m_zbtn[k] = b;
        W.m_zemb[k] = image(b, NULL, -22, -20);
        lv_image_set_scale(W.m_zemb[k], 120);
        lv_image_set_antialias(W.m_zemb[k], true);
        W.m_zname[k] = label(b, mha_zone_title(s_wz[k].zone), aos_font_small, 0xFFFFFF, 52, 5, 128);
        lv_obj_set_style_text_align(W.m_zname[k], LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(W.m_zname[k], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(W.m_zname[k], 20);
        W.m_zstars[k] = label(b, "", aos_font_small, 0xFFD060, 52, 29, 128);
        lv_obj_set_style_text_align(W.m_zstars[k], LV_TEXT_ALIGN_LEFT, 0);
    }
    (void)a;
}

static void wide_select(app_t *a, int i)
{
    if (i < 0 || i >= MH_LEVELS) return;
    W.sel = i;
    int z = mha_level_info(i)->zone;
    for (int k = 0; k < 6; k++) {
        if (s_wz[k].zone != z) continue;
        mh_uimg_t *em = &a->ui_emblem[s_wz[k].emblem];
        if (em->buf) {
            wrap(em);
            lv_image_set_src(W.m_emb, &em->dsc);
        }
    }
    lv_label_set_text(W.m_zone, mha_zone_title(z));
    char b[160];
    snprintf(b, sizeof b, "%d-%d  %s", i / 4 + 1, i % 4 + 1, mha_level_title(i));
    lv_label_set_text(W.m_title, b);
    bool open = mha_level_open(a, i);
    for (int k = 0; k < 3; k++) lv_image_set_src(W.m_star[k], k < a->prog.stars[i] ? &s_ui.d_star_big : &s_ui.d_star_big_off);
    if (open) {
        int ms = a->prog.best_ms[i];
        char best[32] = "-";
        if (ms > 0) snprintf(best, sizeof best, "%d:%02d", ms / 60000, (ms / 1000) % 60);
        snprintf(b, sizeof b, "%s: %s\n%s: %s", _("Récord"), best, _("Figurita"),
                 (a->prog.stickers & (1u << i)) ? _("encontrada") : _("escondida"));
        lv_obj_remove_flag(W.m_play, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (mh_prog_stars(&a->prog) < mha_zone_need(z))
            snprintf(b, sizeof b, _("Hacen falta %d estrellas para abrir esta zona"), mha_zone_need(z));
        else snprintf(b, sizeof b, "%s", _("Termina el nivel anterior"));
        lv_obj_add_flag(W.m_play, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(W.m_info, b);
    /* the pads: the chosen one glows */
    for (int k = 0; k < MH_LEVELS; k++)
        lv_obj_set_style_shadow_width(s_ui.m_pad[k], k == i ? 18 : 0, 0);
    lv_obj_set_style_shadow_color(s_ui.m_pad[i], lv_color_hex(GOLD), 0);
}

static void wide_map_refresh(app_t *a, int next)
{
    for (int k = 0; k < 6; k++) {
        int z = s_wz[k].zone, first = mh_zone_first(z);
        bool open = mh_prog_stars(&a->prog) >= mha_zone_need(z);
        /* a closed zone shows the lock */
        mh_uimg_t *em = &a->ui_emblem[open || !a->ui_emblem[6].buf ? s_wz[k].emblem : 6];
        if (em->buf) {
            wrap(em);
            lv_image_set_src(W.m_zemb[k], &em->dsc);
        }
        int st = 0;
        for (int i = first; i < first + 4; i++) st += a->prog.stars[i];
        char b[48];
        if (open) snprintf(b, sizeof b, "%d/12", st);
        else snprintf(b, sizeof b, _("Faltan %d estrellas"), mha_zone_need(z) - mh_prog_stars(&a->prog));
        lv_label_set_text(W.m_zstars[k], b);
    }
    wide_select(a, W.sel >= 0 && W.sel < MH_LEVELS ? W.sel : next);
}

/* a pad clicked: chosen for the card; clicked again (or Enter again), played */
static bool wide_pad(app_t *a, int i)
{
    if (i < 0 || i >= MH_LEVELS) return false;
    if (W.sel == i && mha_level_open(a, i)) {
        mh_snd(SND_GO);
        mha_level_start(a, i);
        return true;
    }
    wide_select(a, i);
    return true;
}

/* ---- the pause: the plan big, the counts beside it ---- */

static void wide_build_pause(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_PAUSE] = panel(root, 170);
    W.pz_px = (uint32_t *)mh_calloc((size_t)PZ_W * PZ_H, 4);
    if (W.pz_px) {
        W.pz = lv_canvas_create(p);
        lv_canvas_set_buffer(W.pz, W.pz_px, PZ_W, PZ_H, LV_COLOR_FORMAT_ARGB8888);
        lv_obj_set_pos(W.pz, WX(205 - PZ_W / 2), WY(225 - PZ_H / 2));
        lv_obj_remove_flag(W.pz, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *box = side_box(p, 414, 10, 372, 430);
    lv_obj_set_style_bg_opa(box, 200, 0);
    label(p, _("Pausa"), aos_font_title, 0xFFFFFF, WX(420), WY(22), 360);
    W.pz_title = label(p, "", aos_font_body, 0xFFE070, WX(420), WY(64), 360);
    W.pz_zone = label(p, "", aos_font_small, 0xC8C0D8, WX(420), WY(92), 360);
    W.pz_info = label(p, "", aos_font_body, 0xFFFFFF, WX(440), WY(124), 320);
    lv_obj_set_style_text_align(W.pz_info, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(W.pz_info, 6, 0);
    button(p, _("Seguir"), WX(430), WY(290), 340, 56, 0x30C060, resume_cb, a, NULL);
    button(p, _("Reintentar"), WX(430), WY(358), 166, 48, ACCENT, restart_cb, a, NULL);
    button(p, _("Salir al mapa"), WX(604), WY(358), 166, 48, 0x5A4A7A, to_map_cb, a, NULL);
}

static uint32_t argb(uint32_t rgb, int a)
{
    return (uint32_t)a << 24 | (rgb & 0xFFFFFF);
}

static void pz_rect(int x, int y, int w, int h, uint32_t c)
{
    for (int r = y; r < y + h; r++) {
        if (r < 0 || r >= PZ_H) continue;
        for (int k = x; k < x + w; k++)
            if (k >= 0 && k < PZ_W) W.pz_px[r * PZ_W + k] = c;
    }
}

static void pz_disc(float cx, float cy, float rad, uint32_t c)
{
    int x0 = (int)(cx - rad - 1), x1 = (int)(cx + rad + 1), y0 = (int)(cy - rad - 1), y1 = (int)(cy + rad + 1);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || y < 0 || x >= PZ_W || y >= PZ_H) continue;
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy <= rad * rad) W.pz_px[y * PZ_W + x] = c;
        }
    }
}

/* a tile's colour on the plan: the average of the top of its first frame */
static uint32_t tile_colour(const mh_anim_t *an, uint32_t fallback)
{
    if (!an || !an->n) return fallback;
    const mh_spr_t *s = &an->f[0];
    int bpp = an->fmt == MH_PX_COL ? 4 : 0;
    if (!bpp) return fallback;
    uint32_t r = 0, g = 0, b = 0, n = 0;
    for (int y = 0; y < s->h * 2 / 5; y++) {
        int x0, x1;
        const uint8_t *px = mh_spr_row(s, y, &x0, &x1);
        for (int x = x0; x < x1; x++, px += 4) {
            if (px[2] < 200) continue;
            int R, G, B;
            mh_unpack((uint16_t)(px[0] | (px[1] << 8)), &R, &G, &B);
            r += (uint32_t)R;
            g += (uint32_t)G;
            b += (uint32_t)B;
            n++;
        }
    }
    if (!n) return fallback;
    return (r / n) << 16 | (g / n) << 8 | (b / n);
}

static void wide_pause_fill(app_t *a)
{
    const mh_level_t *lv = &a->lv;
    const mh_game_t *g = &a->game;
    if (!W.pz_px || !a->level_ok) return;
    memset(W.pz_px, 0, (size_t)PZ_W * PZ_H * 4);
    /* the zone's colours: ground by floor, water, the solid things */
    static const uint32_t ground[ZONE_N] = { 0x5A6070, 0x6A5A8A, 0xC8A064, 0x4A7A4A, 0x5A6070, 0xA0603A, 0x8A8C80 };
    static const uint32_t water[ZONE_N] = { 0x3A5A4A, 0x3A2A6A, 0x3AB0B0, 0x2A6A6A, 0x2A4A7A, 0x2A7A6A, 0x1A3A6A };
    int z = lv->zone < ZONE_N ? lv->zone : ZONE_TEST;
    /* as big as the level lets it */
    int PZ_C = PZ_W / lv->w < PZ_H / lv->h ? PZ_W / lv->w : PZ_H / lv->h;
    if (PZ_C > 18) PZ_C = 18;
    int ox = (PZ_W - lv->w * PZ_C) / 2, oy = (PZ_H - lv->h * PZ_C) / 2;
    static uint32_t tint[MH_LV_MAXASSET];
    static int tint_level = -99;
    if (tint_level != a->loaded_level) {
        tint_level = a->loaded_level;
        for (int i = 0; i < MH_LV_MAXASSET; i++) tint[i] = tile_colour(&a->world.art[i], 0xFF000000u);
    }
    for (int y = 0; y < lv->h; y++) {
        for (int x = 0; x < lv->w; x++) {
            const mh_cell_t *c = mh_cell(lv, x, y);
            uint32_t col;
            int al = 235;
            switch (c->kind) {
            case CK_PIT: col = 0x000000; al = 120; break;
            case CK_WATER: col = c->surf && tint[c->surf] >> 24 == 0 ? tint[c->surf] : water[z]; break;
            case CK_QUICK: col = c->surf && tint[c->surf] >> 24 == 0 ? tint[c->surf] : mh_mix(ground[z], 0x201008, 150);
                break;
            case CK_BRIDGE: col = c->deck && tint[c->deck] >> 24 == 0 ? tint[c->deck] : 0x8A6A40; break;
            default:
                col = c->top && tint[c->top] >> 24 == 0 ? tint[c->top] : ground[z];
                /* higher ground a little lighter */
                col = mh_mix(col, 0xFFFFFF, c->h * 28);
                if (c->flags & (CF_SOLID | CF_HIGH)) col = mh_mix(col, 0x000000, 150);
                break;
            }
            int sx = ox + x * PZ_C, sy = oy + (lv->h - 1 - y) * PZ_C;
            pz_rect(sx, sy, PZ_C - 1, PZ_C - 1, argb(col, al));
        }
    }
    float half = PZ_C * 0.5f;
#define PX(cx) (ox + (cx) * PZ_C)
#define PY(cy) (oy + (lv->h - (cy)) * PZ_C)
    for (int i = 0; i < g->n_cp; i++)
        pz_disc(PX(g->cpt[i].x + 0.5f), PY(g->cpt[i].y + 0.5f), half * 0.55f, argb(g->cpt[i].lit ? 0xFF9A30 : 0x805020, 255));
    for (int i = 0; i < g->n_pick; i++) {
        const mh_pick_t *p = &g->pick[i];
        if (p->type == ENT_KEY && !p->taken) {
            pz_disc(PX(p->x + 0.5f), PY(p->y + 0.5f), half * 0.95f, argb(0x5A3A00, 255));
            pz_disc(PX(p->x + 0.5f), PY(p->y + 0.5f), half * 0.75f, argb(0xFFD040, 255));
        }
    }
    if (g->exit_x >= 0) {
        uint32_t ec = g->exit_open ? 0x60F070 : 0x3A7A40;
        pz_rect(PX(g->exit_x) + 1, (int)PY(g->exit_y + 1.0f) + 1, PZ_C - 3, PZ_C - 3, argb(ec, 255));
    }
    for (int i = 0; i < g->n_mon; i++) pz_disc(PX(g->mon[i].x), PY(g->mon[i].y), half * 0.6f, argb(0xFF3A4A, 255));
    pz_disc(PX(g->h.x), PY(g->h.y), half * 1.1f, argb(0xFFFFFF, 255));
    pz_disc(PX(g->h.x), PY(g->h.y), half * 0.8f, argb(0x40C8FF, 255));
#undef PX
#undef PY
    lv_obj_invalidate(W.pz);
    /* the counts */
    lv_label_set_text(W.pz_title, a->level >= 0 ? mha_level_title(a->level) : "");
    lv_label_set_text(W.pz_zone, mha_zone_title(lv->zone));
    char b[200];
    int n = snprintf(b, sizeof b, "%s: %d/%d\n%s: %d", _("Llaves"), g->keys, MH_KEYS, _("Monedas"), g->coins);
    if (!g->link) n += snprintf(b + n, sizeof b - (size_t)n, "\n%s: %d", _("Vidas"), g->lives);
    if (g->timer || g->link) {
        int t = (int)g->time_left;
        if (t < 0) t = 0;
        snprintf(b + n, sizeof b - (size_t)n, "\n%s: %d:%02d", _("Tiempo"), t / 60, t % 60);
    }
    lv_label_set_text(W.pz_info, b);
}

/* ---- the shop: Tommy big on the left, the catalogue on the right ---- */

static void wide_shop_layout(app_t *a)
{
    (void)a;
    lv_obj_set_pos(s_ui.s_title, WX(390), WY(10));
    lv_obj_set_width(s_ui.s_title, 400);
    lv_obj_set_style_text_font(s_ui.s_title, aos_font_title, 0);
    lv_obj_set_pos(s_ui.s_coins, WX(390), WY(48));
    lv_obj_set_width(s_ui.s_coins, 400);
    lv_obj_set_style_text_align(s_ui.s_coins, LV_TEXT_ALIGN_CENTER, 0);
    if (s_ui.s_prev) {
        /* the turntable twice as big, around its middle */
        lv_obj_set_pos(s_ui.s_prev, WX(190 - PREV_W / 2), WY(205 - PREV_H / 2));
        lv_image_set_scale(s_ui.s_prev, 490);
        lv_image_set_antialias(s_ui.s_prev, true);
    }
    lv_obj_set_pos(s_ui.s_item, WX(0), WY(400));
    lv_obj_set_width(s_ui.s_item, 380);
    lv_obj_set_style_text_font(s_ui.s_item, aos_font_body, 0);
    /* every category in view: two rows of four */
    lv_obj_t *t = s_ui.s_tabs;
    lv_obj_set_pos(t, WX(390), WY(80));
    lv_obj_set_size(t, 400, 84);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_left(t, 0, 0);
    lv_obj_set_style_pad_row(t, 6, 0);
    lv_obj_set_style_pad_column(t, 6, 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    uint32_t n = lv_obj_get_child_count(t);
    for (uint32_t k = 0; k < n; k++) {
        lv_obj_t *b = lv_obj_get_child(t, (int32_t)k);
        lv_obj_set_width(b, 95);
        lv_obj_t *l = lv_obj_get_child(b, 0);
        if (l) {
            lv_obj_set_width(l, 89);
            lv_obj_set_style_text_font(l, aos_font_small, 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_height(l, 20);
            lv_obj_center(l);
        }
    }
    lv_obj_set_pos(s_ui.s_list, WX(390), WY(172));
    lv_obj_set_size(s_ui.s_list, 400, 214);
    lv_obj_set_pos(s_ui.s_btn, WX(390), WY(394));
    lv_obj_set_size(s_ui.s_btn, 400, 48);
    lv_obj_t *bl = lv_obj_get_child(s_ui.s_btn, 0);
    if (bl) {
        lv_obj_set_width(bl, 388);
        lv_obj_center(bl);
    }
}

/* ---- the album: every sticker in view, two zones a row ---- */

static void wide_album_layout(app_t *a)
{
    (void)a;
    lv_obj_t *g = s_ui.al_grid;
    lv_obj_set_pos(g, WX(20), WY(52));
    lv_obj_set_size(g, 760, 3 * CARD_H + 2 * 10);
    lv_obj_set_style_pad_row(g, 10, 0);
    lv_obj_set_style_pad_column(g, 8, 0);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
    /* the title over the whole width */
    lv_obj_t *title = lv_obj_get_child(s_ui.p[ST_ALBUM], 0);
    if (title) {
        lv_obj_set_pos(title, WX(0), WY(12));
        lv_obj_set_width(title, MH_W);
        lv_obj_set_style_text_font(title, aos_font_title, 0);
    }
}

/* ---- the trophies: three columns ---- */

static void wide_trophies_layout(app_t *a)
{
    (void)a;
    lv_obj_t *l = s_ui.tr_list;
    lv_obj_set_pos(l, WX(18), WY(56));
    lv_obj_set_size(l, 764, 388);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(l, 8, 0);
    lv_obj_set_style_pad_row(l, 8, 0);
    lv_obj_t *title = lv_obj_get_child(s_ui.p[ST_TROPHIES], 0);
    if (title) {
        lv_obj_set_pos(title, WX(0), WY(10));
        lv_obj_set_width(title, MH_W);
        lv_obj_set_style_text_font(title, aos_font_title, 0);
    }
}

/* ---- the stats: tiles, a number each ---- */

static void wide_build_stats(app_t *a)
{
    (void)a;
    lv_obj_t *p = s_ui.p[ST_STATS];
    lv_obj_add_flag(s_ui.st_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *title = lv_obj_get_child(p, 0);
    if (title) {
        lv_obj_set_pos(title, WX(0), WY(10));
        lv_obj_set_width(title, MH_W);
        lv_obj_set_style_text_font(title, aos_font_title, 0);
    }
    for (int i = 0; i < 12; i++) {
        lv_obj_t *c = side_box(p, 20 + (i % 4) * 192, 60 + (i / 4) * 128, 184, 118);
        W.st_num[i] = label(c, "", aos_font_title, 0xFFD060, 0, 18, 184);
        W.st_cap[i] = label(c, "", aos_font_small, 0xC8C0D8, 6, 64, 172);
    }
}

static void wide_stats_fill(app_t *a)
{
    const int32_t *s = a->prog.stat;
    int t = s[SX_PLAY_S];
    char v[12][32];
    const char *cap[12] = { _("Niveles terminados"), _("Estrellas"), _("Figuritas"), _("Trofeos"),
                            _("Llaves juntadas"), _("Monedas juntadas"), _("Saltos"), _("Tiempo jugado"),
                            _("Atrapado por monstruos"), _("Al agua"), _("Caídas"), _("Carreras ganadas") };
    snprintf(v[0], 32, "%d", (int)s[SX_LEVELS]);
    snprintf(v[1], 32, "%d/%d", mh_prog_stars(&a->prog), MH_LEVELS * 3);
    snprintf(v[2], 32, "%d/%d", bits(a->prog.stickers), MH_LEVELS);
    snprintf(v[3], 32, "%d/%d", bits(a->prog.trophies), TR_N);
    snprintf(v[4], 32, "%d", (int)s[SX_KEYS]);
    snprintf(v[5], 32, "%d", (int)s[SX_COINS]);
    snprintf(v[6], 32, "%d", (int)s[SX_HOPS]);
    snprintf(v[7], 32, "%dh %02dm", t / 3600, (t / 60) % 60);
    snprintf(v[8], 32, "%d", (int)s[SX_CAUGHT]);
    snprintf(v[9], 32, "%d", (int)s[SX_DROWNED]);
    snprintf(v[10], 32, "%d", (int)s[SX_FELL]);
    snprintf(v[11], 32, "%d/%d", (int)s[SX_RACES_WON], (int)s[SX_RACES]);
    for (int i = 0; i < 12; i++) {
        if (!W.st_num[i]) continue;
        lv_label_set_text(W.st_num[i], v[i]);
        lv_label_set_text(W.st_cap[i], cap[i]);
    }
}

/* ---- the settings: the game on the left, the computer on the right ---- */

static void full_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    mh_snd(SND_SELECT);
    mh_desktop_fullscreen(true);
    wide_settings_refresh(a);
}

static const char *const s_lang_code[3] = { "es", "en", "de" };
static int s_lang_pick = -1;

static void lang_cb(lv_event_t *e)
{
    app_t *a = app_of(e);
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    mh_snd(SND_SELECT);
    /* the whole interface is built in one language: the new one comes with
     * the next start (switching now would free the texts on screen) */
    aos_hal_pref_set_str("lang", s_lang_code[i]);
    s_lang_pick = i;
    wide_settings_refresh(a);
}

static void wide_build_settings(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = s_ui.p[ST_SETTINGS] = panel(root, 256);
    label(p, _("Ajustes"), aos_font_title, 0xFFFFFF, WX(0), WY(10), MH_W);
    /* the game */
    lv_obj_t *c = side_box(p, 16, 58, 376, 380);
    label(c, _("Dificultad"), aos_font_body, 0xC8C0D8, 0, 12, 376);
    const char *dn[DIFF_N] = { _("Fácil"), _("Normal"), _("Difícil") };
    for (int i = 0; i < DIFF_N; i++) {
        s_ui.c_diff[i] = button(c, dn[i], 14 + i * 118, 46, 110, 48, ACCENT, diff_cb, a, NULL);
        lv_obj_set_user_data(s_ui.c_diff[i], (void *)(intptr_t)i);
    }
    label(c, _("Fácil: 5 vidas y sin reloj. Normal: 3 vidas y reloj. Difícil: 1 vida y un poco menos de tiempo."),
          aos_font_small, 0x9A90B0, 16, 106, 344);
    label(c, _("Sonido"), aos_font_body, 0xC8C0D8, 0, 214, 376);
    s_ui.c_sfx = button(c, _("Efectos"), 24, 250, 156, 52, ACCENT, sound_cb, a, NULL);
    s_ui.c_music = button(c, _("Música"), 196, 250, 156, 52, ACCENT, sound_cb, a, NULL);
    /* the computer */
    c = side_box(p, 408, 58, 376, 380);
    W.c_full = button(c, _("Pantalla completa"), 24, 14, 328, 48, ACCENT, full_cb, a, NULL);
    label(c, _("Idioma"), aos_font_body, 0xC8C0D8, 0, 76, 376);
    static const char *const ln[3] = { "Español", "English", "Deutsch" };
    for (int i = 0; i < 3; i++) {
        W.c_lang[i] = button(c, ln[i], 14 + i * 118, 108, 110, 44, ACCENT, lang_cb, a, NULL);
        lv_obj_set_user_data(W.c_lang[i], (void *)(intptr_t)i);
    }
    W.lang_note = label(c, "", aos_font_small, 0xFFD060, 10, 158, 356);
    label(c, _("Controles"), aos_font_body, 0xC8C0D8, 0, 196, 376);
    lv_obj_t *k = label(c, _("Flechas o WASD: saltar\nEspacio o Enter: la acción\nEsc o P: pausa\nF11: pantalla completa\nUn joystick también sirve"),
                        aos_font_small, 0xE0D8F0, 24, 228, 330);
    lv_obj_set_style_text_align(k, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(k, 4, 0);
}

static void wide_settings_refresh(app_t *a)
{
    (void)a;
    if (W.c_full) chip_style(W.c_full, mh_desktop_fullscreen(false));
    const char *cur = aos_i18n_current();
    int on = s_lang_pick;
    if (on < 0) {
        for (int i = 0; i < 3; i++) if (!strcmp(cur, s_lang_code[i])) on = i;
    }
    for (int i = 0; i < 3; i++) if (W.c_lang[i]) chip_style(W.c_lang[i], i == on);
    if (W.lang_note) {
        bool later = on >= 0 && strcmp(cur, s_lang_code[on]);
        lv_label_set_text(W.lang_note, later ? _("El idioma cambia al volver a abrir el juego") : "");
    }
}

/* ---- the clock, the jobs ---- */

static void wide_tick(app_t *a, int dt_ms)
{
    /* the title's map drifts slowly up and back, over the zones */
    if (a->state == ST_MENU && W.bg.buf) {
        W.pan += dt_ms / 1000.0f;
        int span = W.bg.h - MH_H;
        float k = 0.5f - 0.5f * cosf(W.pan * 0.06f);
        lv_obj_set_y(W.t_bg, WY(-span + (int)(k * span * 0.8f)));
    }
    if ((a->state == ST_MENU || a->state == ST_HOUSE) && W.turn_ok) {
        W.turn_ms += (uint32_t)dt_ms;
        if (W.turn_ms >= 150) {
            W.turn_ms = 0;
            W.turn_frame = (W.turn_frame + 1) % 12;
            turn_draw(a);
        }
    }
}

static void wide_before_job(app_t *a, int what)
{
    (void)a;
    if (what == UJ_TURN || what == UJ_FREE_MAP) W.turn_ok = false;
    if (what == UJ_FREE_MAP) {
        /* the turntable and the emblems go with the menus; the scaled
         * pictures are the desktop's own and stay */
        memset(W.turn_eq, -2, CAT_N);
        if (W.m_emb) lv_image_set_src(W.m_emb, NULL);
        for (int k = 0; k < 6; k++) if (W.m_zemb[k]) lv_image_set_src(W.m_zemb[k], NULL);
    }
}

static void wide_job_done(app_t *a, int what)
{
    if (what != UJ_TURN) return;
    W.turn_ok = true;
    W.turn_asked = false;
    /* the shop builds it too, with what it tries on */
    memcpy(W.turn_eq, a->try_eq, CAT_N);
    turn_draw(a);
}

static void wide_free(app_t *a)
{
    (void)a;
    uimg_free(&W.bg);
    uimg_free(&W.logo);
    uimg_free(&W.house);
    free(W.turn_px);
    free(W.pz_px);
    W.turn_px = NULL;
    W.pz_px = NULL;
}
