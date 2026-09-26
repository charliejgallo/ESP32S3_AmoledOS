/*
 * AmoledOS - Radio: the front panel. See radio.h.
 *
 *   y  14..164   the dial: backlit amber glass with the cover, the station,
 *                the song, the stream's format, and the tuning scale with
 *                its needle over the key that plays
 *   y 170..234   mute, previous, play/pause, next, info
 *   y 244..276   volume
 *   y 290..408   nine keys, three rows: the presets
 *   y 418        one line of state, read only (below the touch's reach)
 *
 * Everything is LVGL objects that stand still (APP-GUIDE 6.1) except the
 * needle, moved by a 30 ms timer only while it travels, and the song's
 * title, which scrolls by itself when it does not fit.
 */
#include "radio.h"

#include "aos_fonts.h"
#include "aos_i18n.h"
#include "aos_theme.h"

#include <stdio.h>
#include <string.h>

#define C_BODY_TOP   0x1C1410
#define C_BODY_BOT   0x070504
#define C_GLASS_TOP  0x2E1C08
#define C_GLASS_BOT  0x130B03
#define C_BRASS      0x8A6A36
#define C_AMBER      0xFFB547
#define C_AMBER_DIM  0xA07A3E
#define C_WARM       0xF4E7CF
#define C_WARM_DIM   0xCDBB98
#define C_NEEDLE     0xFF3B30
#define C_METAL_TOP  0x302824
#define C_METAL_BOT  0x151110
#define C_METAL_EDGE 0x5A4632
#define C_IVORY_TOP  0xF1E8D4
#define C_IVORY_BOT  0xCBBD9C
#define C_IVORY_TXT  0x2B2014
#define C_LED_OFF    0x5C4A30
#define C_LED_ON     0xFFB000

#define DIAL_X   12
#define DIAL_Y   14
#define DIAL_W   344
#define DIAL_H   150
#define SCALE_X  12             /* inside the dial */
#define SCALE_Y  112
#define KEY_Y0   290
#define KEY_H    36
#define KEY_GAP  5
#define KEY_W    110

/* x of key i's mark on the scale, in the scale's own pixels */
static int mark_x(int i)
{
    return 20 + i * 35;
}

static lv_obj_t *plain(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                      int x, int y, int w, lv_label_long_mode_t mode)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, mode);
    if (mode != LV_LABEL_LONG_MODE_WRAP) {
        lv_obj_set_height(l, lv_font_get_line_height(font));   /* one line, never two */
    }
    lv_label_set_text(l, "");
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static void grad(lv_obj_t *o, uint32_t top, uint32_t bot)
{
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(top), 0);
    lv_obj_set_style_bg_grad_color(o, lv_color_hex(bot), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
}

/* ---- events ------------------------------------------------------------------ */

static void on_click(lv_event_t *e)
{
    radio_t *r = lv_event_get_user_data(e);
    if (r->closing) {
        return;
    }
    lv_obj_t *t = lv_event_get_current_target(e);
    if (t == r->b_play) {
        radio_on_play(r);
    } else if (t == r->b_prev) {
        radio_on_step(r, -1);
    } else if (t == r->b_next) {
        radio_on_step(r, 1);
    } else if (t == r->b_mute) {
        radio_on_mute(r);
    } else if (t == r->b_info || t == r->dial) {
        radio_ui_info_open(r);
    } else if (t == r->info) {
        radio_ui_info_close(r);
    } else {
        for (int i = 0; i < RADIO_KEYS; i++) {
            if (t == r->key[i]) {
                radio_on_key(r, i);
                break;
            }
        }
    }
}

static void on_volume(lv_event_t *e)
{
    radio_t *r = lv_event_get_user_data(e);
    if (!r->closing) {
        radio_on_volume(r, (int)lv_slider_get_value(r->vol));
    }
}

/* ---- the dial ---------------------------------------------------------------- */

static void scale_draw(radio_t *r)
{
    uint32_t *px = (uint32_t *)r->scale_px;
    memset(px, 0, SCALE_W * SCALE_H * 4);
    const uint32_t amber = 0xFF000000u | C_AMBER;
    const uint32_t dim   = 0xB0000000u | C_AMBER_DIM;
    int base = SCALE_H - 3;
    for (int x = 6; x < SCALE_W - 6; x++) {
        px[base * SCALE_W + x] = dim;
    }
    /* minor ticks every 7 px, a major one on every key */
    for (int x = mark_x(0) - 14; x <= mark_x(RADIO_KEYS - 1) + 14; x += 7) {
        bool major = false;
        for (int i = 0; i < RADIO_KEYS; i++) {
            major |= x == mark_x(i);
        }
        int h = major ? 12 : 5;
        for (int y = base - h; y < base; y++) {
            px[y * SCALE_W + x] = major ? amber : dim;
            if (major) {
                px[y * SCALE_W + x + 1] = amber;
            }
        }
    }
}

static void build_dial(radio_t *r, lv_obj_t *root)
{
    r->dial = plain(root, DIAL_X, DIAL_Y, DIAL_W, DIAL_H);
    grad(r->dial, C_GLASS_TOP, C_GLASS_BOT);
    lv_obj_set_style_radius(r->dial, 18, 0);
    lv_obj_set_style_border_width(r->dial, 2, 0);
    lv_obj_set_style_border_color(r->dial, lv_color_hex(C_BRASS), 0);
    lv_obj_add_flag(r->dial, LV_OBJ_FLAG_CLICKABLE);       /* tap: the info card */
    lv_obj_add_event_cb(r->dial, on_click, LV_EVENT_CLICKED, r);

    /* the cover, in a brass frame */
    lv_obj_t *frame = plain(r->dial, 8, 8, DIAL_COVER + 4, DIAL_COVER + 4);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(frame, lv_color_hex(C_BRASS), 0);
    lv_obj_set_style_radius(frame, 6, 0);
    r->cover_px = art_big_alloc(DIAL_COVER * DIAL_COVER * 2);
    r->cover = lv_canvas_create(frame);
    lv_obj_remove_flag(r->cover, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(r->cover, 2, 2);
    if (r->cover_px) {
        lv_canvas_set_buffer(r->cover, r->cover_px, DIAL_COVER, DIAL_COVER, LV_COLOR_FORMAT_RGB565);
    }

    int tx = DIAL_COVER + 22, tw = DIAL_W - tx - 14;
    r->station = text(r->dial, &aos_montserrat_20, C_AMBER, tx, 8, tw - 18, LV_LABEL_LONG_MODE_DOTS);
    r->onair = plain(r->dial, DIAL_W - 24, 14, 10, 10);
    lv_obj_set_style_radius(r->onair, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(r->onair, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r->onair, lv_color_hex(0x401010), 0);
    r->title = text(r->dial, &aos_montserrat_20, C_WARM, tx, 34, tw, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    r->artist = text(r->dial, &aos_montserrat_16, C_WARM_DIM, tx, 60, tw, LV_LABEL_LONG_MODE_DOTS);
    r->meta = text(r->dial, &aos_montserrat_14, C_AMBER_DIM, tx, 83, tw, LV_LABEL_LONG_MODE_DOTS);

    r->scale_px = art_big_alloc(SCALE_W * SCALE_H * 4);
    r->scale = lv_canvas_create(r->dial);
    lv_obj_remove_flag(r->scale, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(r->scale, SCALE_X - 2, SCALE_Y - 8);
    if (r->scale_px) {
        scale_draw(r);
        lv_canvas_set_buffer(r->scale, r->scale_px, SCALE_W, SCALE_H, LV_COLOR_FORMAT_ARGB8888);
    }
    for (int i = 0; i < RADIO_KEYS; i++) {
        char n[4];
        snprintf(n, sizeof(n), "%d", i + 1);
        r->scale_num[i] = text(r->dial, &aos_montserrat_14, C_AMBER_DIM,
                               SCALE_X - 2 + mark_x(i) - 10, SCALE_Y + 18, 22, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(r->scale_num[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(r->scale_num[i], n);
    }
    r->needle = plain(r->dial, SCALE_X, SCALE_Y - 2, 3, 24);
    lv_obj_set_style_bg_opa(r->needle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r->needle, lv_color_hex(C_NEEDLE), 0);
    lv_obj_set_style_radius(r->needle, 1, 0);
    r->needle_x = r->needle_to = (SCALE_X - 2 + 4) * 16;
    lv_obj_set_x(r->needle, r->needle_x / 16);
}

/* ---- the panel ---------------------------------------------------------------- */

static lv_obj_t *knob(radio_t *r, lv_obj_t *root, int cx, int cy, int d, const char *sym,
                      bool accent, lv_obj_t **icon)
{
    lv_obj_t *b = plain(root, cx - d / 2, cy - d / 2, d, d);
    grad(b, C_METAL_TOP, C_METAL_BOT);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b, accent ? 3 : 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent ? 0xE0A040 : C_METAL_EDGE), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x4A3A2C), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_add_event_cb(b, on_click, LV_EVENT_CLICKED, r);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, accent ? &aos_montserrat_28 : &aos_montserrat_20, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(accent ? C_AMBER : C_WARM), 0);
    lv_label_set_text(l, sym);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    if (icon) {
        *icon = l;
    }
    return b;
}

static void build_panel(radio_t *r, lv_obj_t *root)
{
    int cy = 202;
    r->b_mute = knob(r, root, 44, cy, 48, LV_SYMBOL_VOLUME_MAX, false, &r->i_mute);
    r->b_prev = knob(r, root, 114, cy, 52, LV_SYMBOL_PREV, false, NULL);
    r->b_play = knob(r, root, 184, cy, 64, LV_SYMBOL_PLAY, true, &r->i_play);
    r->b_next = knob(r, root, 254, cy, 52, LV_SYMBOL_NEXT, false, NULL);
    r->b_info = knob(r, root, 324, cy, 48, "i", false, NULL);

    /* volume: a fader with a brass knob */
    lv_obj_t *spk = text(root, &aos_montserrat_20, C_AMBER_DIM, 20, 249, 30, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(spk, LV_SYMBOL_VOLUME_MID);
    r->vol = lv_slider_create(root);
    lv_obj_set_pos(r->vol, 60, 254);
    lv_obj_set_size(r->vol, 246, 10);
    lv_slider_set_range(r->vol, 0, 100);
    lv_obj_set_ext_click_area(r->vol, 14);
    lv_obj_set_style_bg_opa(r->vol, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(r->vol, lv_color_hex(0x2A2118), LV_PART_MAIN);
    lv_obj_set_style_radius(r->vol, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(r->vol, lv_color_hex(C_AMBER_DIM), LV_PART_INDICATOR);
    lv_obj_set_style_radius(r->vol, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(r->vol, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(r->vol, lv_color_hex(0xE8C27A), LV_PART_KNOB);
    lv_obj_set_style_bg_grad_color(r->vol, lv_color_hex(0x8A6A36), LV_PART_KNOB);
    lv_obj_set_style_bg_grad_dir(r->vol, LV_GRAD_DIR_VER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(r->vol, 8, LV_PART_KNOB);
    lv_obj_set_style_radius(r->vol, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_add_event_cb(r->vol, on_volume, LV_EVENT_VALUE_CHANGED, r);
    r->vol_lbl = text(root, &aos_montserrat_16, C_AMBER_DIM, 312, 250, 42, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(r->vol_lbl, LV_TEXT_ALIGN_RIGHT, 0);

    /* the keys */
    for (int i = 0; i < RADIO_KEYS; i++) {
        int col = i % 3, row = i / 3;
        lv_obj_t *k = plain(root, 12 + col * (KEY_W + 7), KEY_Y0 + row * (KEY_H + KEY_GAP),
                            KEY_W, KEY_H);
        lv_obj_set_style_radius(k, 6, 0);
        lv_obj_set_style_border_width(k, 1, 0);
        lv_obj_set_style_border_color(k, lv_color_hex(0x8C7B5A), 0);
        lv_obj_add_flag(k, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(k, on_click, LV_EVENT_CLICKED, r);
        r->key[i] = k;
        r->key_led[i] = plain(k, 7, KEY_H / 2 - 4, 8, 8);
        lv_obj_set_style_radius(r->key_led[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(r->key_led[i], LV_OPA_COVER, 0);
        r->key_lbl[i] = text(k, &aos_montserrat_14, C_IVORY_TXT, 19, 0, KEY_W - 22,
                             LV_LABEL_LONG_MODE_DOTS);
        lv_obj_align(r->key_lbl[i], LV_ALIGN_LEFT_MID, 19, 0);
    }

    r->status = text(root, &aos_montserrat_14, C_AMBER_DIM, 34, 418, 300, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(r->status, LV_TEXT_ALIGN_CENTER, 0);
}

void radio_ui_build(radio_t *r, lv_obj_t *root)
{
    r->root = root;
    grad(root, C_BODY_TOP, C_BODY_BOT);
    build_dial(r, root);
    build_panel(r, root);
    radio_ui_cover(r, NULL);
    r->s_active = -2;
    r->s_state = -1;
    r->s_vol = -1;
    r->s_muted = -1;
    r->s_onair = -1;
    r->s_cover_kind = -1;
    r->s_cover_slot = -1;
    r->anim = lv_timer_create(radio_ui_anim, 30, r);
}

/* The keys: a lit LED and a key pushed in for the one on air; a dark key
 * for one with nothing on it. */
void radio_ui_keys(radio_t *r)
{
    for (int i = 0; i < RADIO_KEYS; i++) {
        lv_obj_t *k = r->key[i];
        bool used = r->st[i].url[0] != '\0';
        bool on = i == r->s_active;
        char label[64];
        if (used) {
            snprintf(label, sizeof(label), "%d %s", i + 1, r->st[i].name);
            if (on) {
                grad(k, 0xC7B48C, 0xAE9A72);
            } else {
                grad(k, C_IVORY_TOP, C_IVORY_BOT);
            }
            lv_obj_set_style_text_color(r->key_lbl[i], lv_color_hex(C_IVORY_TXT), 0);
        } else {
            snprintf(label, sizeof(label), "%d -", i + 1);
            grad(k, 0x2A2420, 0x1E1916);
            lv_obj_set_style_text_color(r->key_lbl[i], lv_color_hex(0x6A5A48), 0);
        }
        lv_obj_set_style_translate_y(k, on ? 2 : 0, 0);
        lv_label_set_text(r->key_lbl[i], label);
        lv_obj_set_style_bg_color(r->key_led[i], lv_color_hex(on ? C_LED_ON : (used ? C_LED_OFF : 0x2E2620)), 0);
        lv_obj_set_style_text_color(r->scale_num[i], lv_color_hex(on ? C_AMBER : C_AMBER_DIM), 0);
    }
}

/* The dial's cover from a 160 px square, or the drawn one. */
void radio_ui_cover(radio_t *r, const uint16_t *px160)
{
    if (!r->cover_px) {
        return;
    }
    if (px160) {
        for (int y = 0; y < DIAL_COVER; y++) {
            const uint16_t *row = px160 + (y * ART_PX / DIAL_COVER) * ART_PX;
            for (int x = 0; x < DIAL_COVER; x++) {
                r->cover_px[y * DIAL_COVER + x] = row[x * ART_PX / DIAL_COVER];
            }
        }
    } else {
        /* a speaker grille: warm dark cloth with rows of holes */
        for (int y = 0; y < DIAL_COVER; y++) {
            for (int x = 0; x < DIAL_COVER; x++) {
                int dx = (x % 8) - 4, dy = (y % 8) - 4;
                bool hole = dx * dx + dy * dy <= 5 && x > 6 && x < DIAL_COVER - 6 &&
                            y > 6 && y < DIAL_COVER - 6;
                r->cover_px[y * DIAL_COVER + x] = hole ? 0x1061 : 0x3922;
            }
        }
    }
    lv_obj_invalidate(r->cover);
}

void radio_ui_needle_to(radio_t *r, int key)
{
    int x = key < 0 ? SCALE_X - 2 + 4 : SCALE_X - 2 + mark_x(key) - 1;
    r->needle_to = x * 16;
}

void radio_ui_anim(lv_timer_t *t)
{
    radio_t *r = lv_timer_get_user_data(t);
    if (r->closing || r->needle_x == r->needle_to) {
        return;
    }
    int d = r->needle_to - r->needle_x;
    int step = d / 5;
    if (step == 0) {
        step = d > 0 ? 1 : -1;
    }
    r->needle_x += step;
    lv_obj_set_x(r->needle, r->needle_x / 16);
}

/* ---- the info card -------------------------------------------------------------- */

void radio_ui_info_open(radio_t *r)
{
    if (r->info) {
        return;
    }
    r->info = plain(r->root, 0, 0, 368, 448);
    grad(r->info, 0x120C08, 0x050403);
    lv_obj_add_flag(r->info, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r->info, on_click, LV_EVENT_CLICKED, r);

    lv_obj_t *frame = plain(r->info, (368 - ART_PX - 6) / 2, 22, ART_PX + 6, ART_PX + 6);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(frame, lv_color_hex(C_BRASS), 0);
    lv_obj_set_style_radius(frame, 8, 0);
    if (!r->info_px) {
        r->info_px = art_big_alloc(ART_PX * ART_PX * 2);   /* kept until destroy */
    }
    r->info_cover = lv_canvas_create(frame);
    lv_obj_remove_flag(r->info_cover, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(r->info_cover, 3, 3);
    if (r->info_px) {
        lv_canvas_set_buffer(r->info_cover, r->info_px, ART_PX, ART_PX, LV_COLOR_FORMAT_RGB565);
        /* what the dial shows, at its size: the art if there is art */
        if (r->s_cover_kind == 2 && r->art.have) {
            memcpy(r->info_px, r->art.px, ART_PX * ART_PX * 2);
        } else if (r->s_cover_kind == 1 && art_logo(r->s_cover_slot, r->info_px)) {
            /* decoded again: the dial kept only its 104 px */
        } else {
            for (int y = 0; y < ART_PX; y++) {
                for (int x = 0; x < ART_PX; x++) {
                    r->info_px[y * ART_PX + x] =
                        r->cover_px[(y * DIAL_COVER / ART_PX) * DIAL_COVER + x * DIAL_COVER / ART_PX];
                }
            }
        }
    }
    r->info_title = text(r->info, &aos_montserrat_20, C_AMBER, 20, 196, 328, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(r->info_title, LV_TEXT_ALIGN_CENTER, 0);
    r->info_text = text(r->info, &aos_montserrat_14, C_WARM, 26, 226, 316, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t *hint = text(r->info, &aos_montserrat_14, C_AMBER_DIM, 34, 418, 300, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hint, _("Tocá para volver"));
    r->info_refresh = 0;
}

void radio_ui_info_close(radio_t *r)
{
    if (!r->info) {
        return;
    }
    lv_obj_add_flag(r->info, LV_OBJ_FLAG_HIDDEN);
    lv_obj_delete_async(r->info);           /* we are inside its own event */
    r->info = NULL;
    r->info_cover = NULL;
    r->info_title = NULL;
    r->info_text = NULL;
}
