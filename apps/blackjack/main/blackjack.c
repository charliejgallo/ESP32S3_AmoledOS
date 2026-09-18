/*
 * BLACKJACK - the app
 *
 * The only file that sees LVGL, the HAL and the preferences; the rules
 * (bj_game.c) and the art (bj_art.c, bj_court.c) know nothing of them.
 *
 * How the screen is made (APP-GUIDE 6.4): the table is one opaque RGB565
 * canvas drawn once, printed words included; every card is an ARGB8888
 * canvas of its own, drawn once when dealt, and from then on an object that
 * moves - LVGL repaints only the area it passes over. The motion is ours:
 * the app's timer moves each card along its tween and turns the hole card
 * by its horizontal scale, so there is no animation callback to outlive the
 * app.
 *
 * The game advances one event at a time (bj_step()): a card, the reveal, a
 * result. After each the app waits for its animation before asking for the
 * next, which is what makes the deal look dealt.
 *
 * Every visible word is an LVGL label in _(), or drawn on the table with
 * the real fonts, so it is translated like the rest of the system.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_ui.h"

#include "bj_art.h"
#include "bj_game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Preferences, with a prefix of our own
 * -------------------------------------------------------------------------- */
#define KEY_BANK    "bj_bank"
#define KEY_BET     "bj_bet"
#define KEY_BEST    "bj_best"
#define KEY_ROUNDS  "bj_rounds"
#define KEY_WINS    "bj_wins"
#define KEY_PUSH    "bj_push"
#define KEY_BJS     "bj_bjs"
#define KEY_SFX     "bj_sfx"
#define KEY_HINT    "bj_hint"

#define FRAME_MS    30

/* --------------------------------------------------------------------------
 * The table's geometry, in screen pixels. Card positions are the card's
 * top-left corner; the canvas sits BJ_CARD_PAD further out for the shadow.
 * -------------------------------------------------------------------------- */
#define DEALER_Y    46
#define PLAYER_Y    246
#define DEALER_CX   184
#define PLAYER_CX   196         /* a little right: the bet circle is left   */
#define SHOE_X      262         /* where cards come from, above the screen  */
#define SHOE_Y      (-130)
#define STEP_MAX    30          /* the most a card shows of the one under   */
#define PILL_Y_D    (DEALER_Y + BJ_CARD_H - 12)
#define PILL_Y_P    (PLAYER_Y + BJ_CARD_H - 12)
#define ROW_Y       366         /* the buttons: y 366..408, inside the touch */
#define ROW_H       42
#define ARC_CX      184         /* the printed band's centre, as bj_felt_draw */
#define ARC_CY      14

#define DEAL_MS     230
#define FLIP_MS     110
#define SLOTS       30

typedef struct {
    lv_obj_t *cv;
    bj_img_t  img;
    int16_t   shown;            /* what is drawn: -1 back, 0..51, -2 nothing */
    bool      used;
    bool      leaving;          /* being collected: free when it arrives    */
    int16_t   x, y;             /* where it is                              */
    int16_t   x0, y0, x1, y1;   /* the tween                                */
    int16_t   t, dur, delay;
    int16_t   flip_to;          /* the face at the flip's midpoint, or -3   */
    int16_t   ft;
} slot_t;

typedef struct {
    bj_game_t   g;
    lv_obj_t   *root;
    lv_obj_t   *felt;
    uint16_t   *feltmem;
    lv_obj_t   *table;          /* the cards' layer                         */
    slot_t      slot[SLOTS];
    int8_t      dslot[BJ_HAND_MAX];
    int8_t      hslot[BJ_HANDS_MAX][BJ_HAND_MAX];
    uint8_t     dn, hn[BJ_HANDS_MAX], nh;

    lv_obj_t   *pile;
    bj_img_t    pileimg;
    int32_t     pile_amount;
    lv_obj_t   *lbl_stake;

    lv_obj_t   *lbl_bank;
    lv_obj_t   *pill_d, *pill_p[BJ_HANDS_MAX];
    lv_obj_t   *banner;
    uint16_t    banner_ms;

    lv_obj_t   *row_bet, *row_chips, *row_ins, *row_play, *row_over;
    lv_obj_t   *btn_deal, *btn_clear, *lbl_bet;
    lv_obj_t   *btn_ins, *btn_hit, *btn_stand, *btn_double, *btn_split;
    lv_obj_t   *btn_again, *btn_rebet, *btn_refill;
    bj_img_t    chipimg[BJ_NCHIPS];
    int         last_state;     /* the controls shown, to rebuild only then */
    char        last_hint;

    lv_obj_t   *menu, *lbl_stats, *chip_sfx, *chip_hint;

    bool        sfx, hint;
    bool        want_exit, leaving, closing;
    int32_t     wait;
    int32_t     round_net;      /* this round's result, for the banner      */
    int32_t     round_stake;
    uint64_t    prev_ms;
    bool        autoplay;
    const char *acts;           /* BJ_ACTS in the simulator                 */
    lv_timer_t *timer;
} app_t;

/* --------------------------------------------------------------------------
 * Small things
 * -------------------------------------------------------------------------- */

static void beep(app_t *a, int hz, int ms)
{
    if (a->sfx) {
        aos_hal_beep(hz, ms);
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color, int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w);
    lv_obj_set_pos(l, x, y);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *make_row(lv_obj_t *parent, int y, int h)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, AOS_SCREEN_W, h);
    lv_obj_set_pos(r, 0, y);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
    return r;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                             uint32_t fill, const lv_font_t *font, lv_event_cb_t cb, void *data)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_40, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);

    /* One line always: a word that does not fit at this size gets the small
     * font before it gets dots (German is the one that needs it). */
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x > w - 14) {
        font = &aos_montserrat_14;
    }
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_height(l, lv_font_get_line_height(font));
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), LV_STATE_PRESSED);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w - 6);         /* the border is 2 a side */
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static void set_enabled(lv_obj_t *b, bool on)
{
    bool now = !lv_obj_has_state(b, LV_STATE_DISABLED);
    if (now != on) {
        if (on) {
            lv_obj_remove_state(b, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(b, LV_STATE_DISABLED);
        }
    }
}

/* The strategy hint is a gold ring on the button basic strategy would press. */
static void set_hinted(lv_obj_t *b, bool on)
{
    lv_obj_set_style_border_color(b, lv_color_hex(on ? 0xFFD35A : 0x000000), 0);
    lv_obj_set_style_border_opa(b, on ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_border_width(b, on ? 3 : 2, 0);
}

static void show(lv_obj_t *o, bool on)
{
    if (!o) {
        return;
    }
    if (on == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        if (on) {
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void banner(app_t *a, const char *text, uint32_t color, int ms)
{
    lv_label_set_text(a->banner, text);
    lv_obj_set_style_text_color(a->banner, lv_color_hex(color), 0);
    show(a->banner, true);
    a->banner_ms = (uint16_t)ms;
}

/* --------------------------------------------------------------------------
 * The printed table: bj_felt_draw() gives cloth, rail and gold lines; the
 * words go on top here, once, letter by letter along the arc with the
 * system's fonts. Text on the bottom of a circle reads left to right as the
 * angle goes down from 90 degrees; each letter is turned to face the centre.
 * -------------------------------------------------------------------------- */

static uint32_t utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t             c = *p;
    if (c < 0x80) {
        *s += 1;
    } else if ((c & 0xE0) == 0xC0 && p[1]) {
        c = ((c & 0x1F) << 6) | (p[1] & 0x3F);
        *s += 2;
    } else if ((c & 0xF0) == 0xE0 && p[1] && p[2]) {
        c = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        *s += 3;
    } else {
        c = '?';
        *s += 1;
    }
    return c;
}

static void arc_text(lv_layer_t *layer, const char *text, const lv_font_t *font,
                     float radius, uint32_t color, lv_opa_t opa, int spacing)
{
    float total = 0;
    for (const char *s = text; *s;) {
        uint32_t c = utf8_next(&s);
        total += (float)lv_font_get_glyph_width(font, c, 0) + (float)spacing;
    }
    total -= (float)spacing;

    const float deg = 180.f / 3.14159265f;
    float       run = 0;
    for (const char *s = text; *s;) {
        uint32_t c = utf8_next(&s);
        float    w = (float)lv_font_get_glyph_width(font, c, 0);
        float    at = run + w * 0.5f - total * 0.5f;       /* from the middle */
        float    ang = 90.f - at / radius * deg;
        float    px = ARC_CX + radius * cosf(ang / deg);
        float    py = ARC_CY + radius * sinf(ang / deg);
        run += w + (float)spacing;
        if (c == ' ') {
            continue;
        }
        lv_draw_letter_dsc_t d;
        lv_draw_letter_dsc_init(&d);
        d.font     = font;
        d.unicode  = c;
        d.color    = lv_color_hex(color);
        d.opa      = opa;
        d.rotation = (int32_t)((90.f - ang) * -10.f);
        /* lv_draw_letter() moves the glyph back by its own pivot, the middle
         * of the baseline, and turns about it: the point IS that pivot */
        lv_point_t pt = { (int32_t)(px + 0.5f), (int32_t)(py + 0.5f) };
        lv_draw_letter(layer, &d, &pt);
    }
}

static void print_table(app_t *a)
{
    lv_layer_t layer;
    lv_canvas_init_layer(a->felt, &layer);
    arc_text(&layer, _("BLACKJACK PAGA 3 A 2"), &aos_montserrat_16, 196.f, 0xF0CB62, LV_OPA_COVER, 1);
    arc_text(&layer, _("LA BANCA SE PLANTA EN 17"), &aos_montserrat_14, 215.f, 0xFFFFFF, LV_OPA_60, 1);
    lv_canvas_finish_layer(a->felt, &layer);
}

/* --------------------------------------------------------------------------
 * Cards
 * -------------------------------------------------------------------------- */

static int slot_alloc(app_t *a)
{
    for (int i = 0; i < SLOTS; i++) {
        slot_t *s = &a->slot[i];
        if (s->used) {
            continue;
        }
        if (!s->cv) {
            s->img.w  = BJ_CV_W;
            s->img.h  = BJ_CV_H;
            s->img.px = (uint32_t *)malloc((size_t)BJ_CV_W * BJ_CV_H * 4);   /* PSRAM */
            if (!s->img.px) {
                return -1;
            }
            s->cv = lv_canvas_create(a->table);
            lv_canvas_set_buffer(s->cv, s->img.px, BJ_CV_W, BJ_CV_H, LV_COLOR_FORMAT_ARGB8888);
            lv_obj_set_size(s->cv, BJ_CV_W, BJ_CV_H);
            lv_obj_set_style_radius(s->cv, 0, 0);
            lv_image_set_antialias(s->cv, true);
            lv_image_set_pivot(s->cv, BJ_CV_W / 2, BJ_CV_H / 2);
            lv_obj_remove_flag(s->cv, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(s->cv, LV_OBJ_FLAG_SCROLLABLE);
            s->shown = -2;
        }
        s->used    = true;
        s->leaving = false;
        s->dur     = 0;
        s->flip_to = -3;
        lv_image_set_scale_x(s->cv, LV_SCALE_NONE);
        /* the newest card on top of the ones already dealt */
        lv_obj_move_to_index(s->cv, -1);
        return i;
    }
    return -1;
}

static void slot_paint(slot_t *s, int face)
{
    if (s->shown != face) {
        bj_card_draw(&s->img, face);
        s->shown = (int16_t)face;
        lv_obj_invalidate(s->cv);
    }
}

static void slot_place(slot_t *s, int x, int y)
{
    s->x = (int16_t)x;
    s->y = (int16_t)y;
    lv_obj_set_pos(s->cv, x - BJ_CARD_PAD, y - BJ_CARD_PAD);
}

static void slot_move(slot_t *s, int x, int y, int ms, int delay)
{
    if (s->x == x && s->y == y && !s->dur) {
        return;
    }
    s->x0    = s->x;
    s->y0    = s->y;
    s->x1    = (int16_t)x;
    s->y1    = (int16_t)y;
    s->t     = 0;
    s->dur   = (int16_t)(ms > 0 ? ms : 1);
    s->delay = (int16_t)delay;
}

/* Where card i of n goes, for a hand centred on cx with room w. */
static int card_x(int i, int n, int cx, int w)
{
    int step = STEP_MAX;
    if (n > 1 && BJ_CARD_W + step * (n - 1) > w) {
        step = (w - BJ_CARD_W) / (n - 1);
    }
    int total = BJ_CARD_W + step * (n - 1);
    return cx - total / 2 + i * step;
}

static void hand_geom(const app_t *a, int h, int *cx, int *w)
{
    if (h < 0) {
        *cx = DEALER_CX;
        *w  = 300;
    } else if (a->nh < 2) {
        *cx = PLAYER_CX;
        *w  = 250;
    } else {
        *cx = h == 0 ? 100 : 272;
        *w  = 166;
    }
}

static void pills_place(app_t *a);

/* Every card of hand h (-1 the dealer) to where it belongs now. */
static void layout(app_t *a, int h)
{
    int           cx, w;
    int           n  = h < 0 ? a->dn : a->hn[h];
    const int8_t *sl = h < 0 ? a->dslot : a->hslot[h];
    int           y  = h < 0 ? DEALER_Y : PLAYER_Y;
    hand_geom(a, h, &cx, &w);
    for (int i = 0; i < n; i++) {
        slot_t *s  = &a->slot[sl[i]];
        int     tx = card_x(i, n, cx, w);
        if (s->dur && s->x1 == tx && s->y1 == y) {
            continue;                   /* already on its way there */
        }
        slot_move(s, tx, y, s->dur ? s->dur - s->t : 200, 0);
    }
    pills_place(a);
}

static void deal_card(app_t *a, const bj_ev_t *e)
{
    int i = slot_alloc(a);
    if (i < 0) {
        return;
    }
    slot_t *s = &a->slot[i];
    slot_paint(s, e->up ? e->card : -1);
    slot_place(s, SHOE_X, SHOE_Y);
    if (e->hand < 0) {
        a->dslot[a->dn++] = (int8_t)i;
    } else {
        a->hslot[e->hand][a->hn[e->hand]++] = (int8_t)i;
    }
    layout(a, e->hand);
    s->dur = DEAL_MS;                    /* the new one flies from the shoe */
}

static void collect(app_t *a)
{
    int k = 0;
    for (int i = 0; i < SLOTS; i++) {
        slot_t *s = &a->slot[i];
        if (s->used && !s->leaving) {
            s->leaving = true;
            slot_move(s, SHOE_X - 60, SHOE_Y, 260, k++ * 14);
        }
    }
    a->dn = 0;
    a->hn[0] = a->hn[1] = 0;
    a->nh = 1;
}

static void tweens(app_t *a, int dt)
{
    for (int i = 0; i < SLOTS; i++) {
        slot_t *s = &a->slot[i];
        if (!s->used) {
            continue;
        }
        if (s->dur) {
            if (s->delay > 0) {
                s->delay = (int16_t)(s->delay - dt);
            } else {
                s->t = (int16_t)(s->t + dt);
                if (s->t >= s->dur) {
                    s->dur = 0;
                    slot_place(s, s->x1, s->y1);
                    if (s->leaving) {
                        s->used = false;
                        slot_place(s, SHOE_X, SHOE_Y);
                    }
                } else {
                    /* ease out: fast from the shoe, settling on the cloth */
                    float p = (float)s->t / (float)s->dur;
                    p = 1.f - (1.f - p) * (1.f - p) * (1.f - p);
                    slot_place(s, s->x0 + (int)((float)(s->x1 - s->x0) * p),
                               s->y0 + (int)((float)(s->y1 - s->y0) * p));
                }
            }
        }
        if (s->flip_to != -3) {
            /* turning over: squeeze to an edge, change face, open again */
            s->ft = (int16_t)(s->ft + dt);
            int sc;
            if (s->ft < FLIP_MS) {
                sc = 256 - s->ft * 256 / FLIP_MS;
            } else {
                slot_paint(s, s->flip_to);
                sc = (s->ft - FLIP_MS) * 256 / FLIP_MS;
            }
            if (s->ft >= FLIP_MS * 2) {
                sc = LV_SCALE_NONE;
                s->flip_to = -3;
            }
            lv_image_set_scale_x(s->cv, (uint32_t)(sc < 12 ? 12 : sc));
        }
    }
}

/* --------------------------------------------------------------------------
 * Totals, the pile, the bank
 * -------------------------------------------------------------------------- */

static void pill_style(lv_obj_t *p, uint32_t bg, uint32_t fg, uint32_t border)
{
    lv_obj_set_style_bg_color(p, lv_color_hex(bg), 0);
    lv_obj_set_style_text_color(p, lv_color_hex(fg), 0);
    lv_obj_set_style_border_color(p, lv_color_hex(border), 0);
}

static lv_obj_t *make_pill(lv_obj_t *parent)
{
    lv_obj_t *p = lv_label_create(parent);
    lv_label_set_text(p, "");
    lv_obj_set_style_text_font(p, &aos_montserrat_16, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 12, 0);
    lv_obj_set_style_pad_hor(p, 10, 0);
    lv_obj_set_style_pad_ver(p, 2, 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_text_align(p, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    pill_style(p, 0x101418, 0xFFFFFF, 0x101418);
    return p;
}

static void pill_at(lv_obj_t *p, int cx, int y)
{
    lv_obj_update_layout(p);
    int w = lv_obj_get_width(p);
    lv_obj_set_pos(p, cx - w / 2, y);
}

static void pills_place(app_t *a)
{
    int cx, w;
    hand_geom(a, -1, &cx, &w);
    pill_at(a->pill_d, cx, PILL_Y_D);
    for (int h = 0; h < BJ_HANDS_MAX; h++) {
        hand_geom(a, h, &cx, &w);
        pill_at(a->pill_p[h], cx, PILL_Y_P);
    }
}

/* "7/17" for a soft total, the number otherwise; only the cards dealt so
 * far on our side, since the game already knows the next ones. */
static void total_text(const uint8_t *cards, int n, char *buf, size_t len)
{
    bj_hand_t h;
    memset(&h, 0, sizeof h);
    for (int i = 0; i < n && i < BJ_HAND_MAX; i++) {
        h.card[h.n++] = cards[i];
    }
    bool soft;
    int  t = bj_total(&h, &soft);
    if (n == 2 && t == 21) {
        snprintf(buf, len, "BJ");
    } else if (soft && t < 21) {
        snprintf(buf, len, "%d/%d", t - 10, t);
    } else {
        snprintf(buf, len, "%d", t);
    }
}

static void pills_refresh(app_t *a)
{
    const bj_game_t *g = &a->g;
    char             buf[40];

    /* The dealer: what is face up. */
    if (a->dn) {
        uint8_t vis[BJ_HAND_MAX];
        int     n = 0;
        for (int i = 0; i < a->dn; i++) {
            if (i != 1 || g->hole_up) {
                vis[n++] = g->dealer.card[i];
            }
        }
        total_text(vis, n, buf, sizeof buf);
        int t = bj_total(&g->dealer, NULL);
        if (g->hole_up && t > 21) {
            pill_style(a->pill_d, 0x7A1420, 0xFFFFFF, 0x7A1420);
        } else {
            pill_style(a->pill_d, 0x101418, 0xFFFFFF, 0x101418);
        }
        lv_label_set_text(a->pill_d, buf);
        show(a->pill_d, true);
    } else {
        show(a->pill_d, false);
    }

    for (int h = 0; h < BJ_HANDS_MAX; h++) {
        lv_obj_t *p = a->pill_p[h];
        if (h >= a->nh || !a->hn[h]) {
            show(p, false);
            continue;
        }
        const bj_hand_t *hh = &g->hand[h];
        total_text(hh->card, a->hn[h], buf, sizeof buf);
        if (hh->split && !strcmp(buf, "BJ")) {
            snprintf(buf, sizeof buf, "21");
        }
        uint32_t bg = 0x101418, border = 0x101418;
        int      t  = bj_total(hh, NULL);
        if (hh->result) {
            const char *word = "";
            switch (hh->result) {
            case BJ_RES_BLACKJACK: word = _("Blackjack"); bg = 0xB8860B; break;
            case BJ_RES_WIN:       word = _("Ganás");     bg = 0x1F7A3A; break;
            case BJ_RES_PUSH:      word = _("Empate");    bg = 0x4A5060; break;
            case BJ_RES_LOSE:      word = _("Perdés");    bg = 0x7A1420; break;
            default:               word = _("Te pasaste"); bg = 0x7A1420; break;
            }
            border = bg;
            if (hh->result == BJ_RES_BLACKJACK) {
                snprintf(buf, sizeof buf, "%s", word);
            } else {
                /* the total is at most "10/20"; copied by hand because GCC
                 * counts buf's 40 bytes against tot's 12 and stops the build */
                char   tot[12];
                size_t n = strlen(buf);
                n = n < sizeof tot - 1 ? n : sizeof tot - 1;
                memcpy(tot, buf, n);
                tot[n] = 0;
                snprintf(buf, sizeof buf, "%s · %.24s", tot, word);
            }
        } else if (t > 21) {
            bg = border = 0x7A1420;
        } else if (a->nh > 1 && g->state == BJ_ST_PLAYER && g->active == h) {
            border = 0xFFD35A;          /* the hand being played */
        }
        pill_style(p, bg, 0xFFFFFF, border);
        lv_label_set_text(p, buf);
        show(p, true);
    }
    pills_place(a);
}

static void pile_refresh(app_t *a)
{
    const bj_game_t *g = &a->g;
    int32_t          amount;
    /* split, the left hand covers the circle: each pill says its own */
    show(a->pile, a->nh < 2);
    show(a->lbl_stake, a->nh < 2);
    if (g->state == BJ_ST_BET) {
        amount = g->bet;
    } else {
        amount = g->insurance;
        for (int i = 0; i < g->nhands; i++) {
            amount += g->hand[i].bet;
        }
    }
    if (amount != a->pile_amount) {
        a->pile_amount = amount;
        bj_stack_draw(&a->pileimg, amount);
        lv_obj_invalidate(a->pile);
        char buf[16];
        snprintf(buf, sizeof buf, "%ld", (long)amount);
        lv_label_set_text(a->lbl_stake, amount ? buf : "");
    }
}

static void bank_refresh(app_t *a)
{
    char buf[48];
    snprintf(buf, sizeof buf, "%s  %ld", _("Banca"), (long)a->g.bank);
    lv_label_set_text(a->lbl_bank, buf);
}

/* --------------------------------------------------------------------------
 * Preferences
 * -------------------------------------------------------------------------- */

static void prefs_load(app_t *a)
{
    int32_t v;
    bj_game_t *g = &a->g;
    if (aos_hal_pref_get_i32(KEY_BANK, &v) && v >= BJ_BET_MIN) {
        g->bank = v;
    }
    if (aos_hal_pref_get_i32(KEY_BET, &v) && v >= BJ_BET_MIN && v <= BJ_BET_MAX) {
        g->bet = v <= g->bank ? v : g->bank - g->bank % BJ_BET_MIN;
    }
    if (aos_hal_pref_get_i32(KEY_BEST, &v) && v > 0) {
        g->best = v;
    }
    if (g->best < g->bank) {
        g->best = g->bank;
    }
    if (aos_hal_pref_get_i32(KEY_ROUNDS, &v) && v > 0) {
        g->rounds = (uint32_t)v;
    }
    if (aos_hal_pref_get_i32(KEY_WINS, &v) && v > 0) {
        g->wins = (uint32_t)v;
    }
    if (aos_hal_pref_get_i32(KEY_PUSH, &v) && v > 0) {
        g->pushes = (uint32_t)v;
    }
    if (aos_hal_pref_get_i32(KEY_BJS, &v) && v > 0) {
        g->blackjacks = (uint32_t)v;
    }
    g->losses = g->rounds > g->wins + g->pushes ? g->rounds - g->wins - g->pushes : 0;
    a->sfx = !(aos_hal_pref_get_i32(KEY_SFX, &v) && v == 0);
    a->hint = aos_hal_pref_get_i32(KEY_HINT, &v) && v == 1;
}

static void prefs_save(app_t *a)
{
    const bj_game_t *g = &a->g;
    int32_t          bank = g->bank;
    /* A round left half played gives its stakes back: it never finished. */
    if (g->state != BJ_ST_BET && g->state != BJ_ST_OVER) {
        bank += g->insurance;
        for (int i = 0; i < g->nhands; i++) {
            bank += g->hand[i].bet;
        }
    }
    aos_hal_pref_set_i32(KEY_BANK, bank);
    aos_hal_pref_set_i32(KEY_BET, g->bet);
    aos_hal_pref_set_i32(KEY_BEST, g->best);
    aos_hal_pref_set_i32(KEY_ROUNDS, (int32_t)g->rounds);
    aos_hal_pref_set_i32(KEY_WINS, (int32_t)g->wins);
    aos_hal_pref_set_i32(KEY_PUSH, (int32_t)g->pushes);
    aos_hal_pref_set_i32(KEY_BJS, (int32_t)g->blackjacks);
    aos_hal_pref_set_i32(KEY_SFX, a->sfx ? 1 : 0);
    aos_hal_pref_set_i32(KEY_HINT, a->hint ? 1 : 0);
}

/* --------------------------------------------------------------------------
 * The controls, by state
 * -------------------------------------------------------------------------- */

static void controls_refresh(app_t *a)
{
    const bj_game_t *g    = &a->g;
    bool             busy = bj_busy(g) || a->wait > 0;
    int              st   = busy ? -1 : (int)g->state;

    if (st != a->last_state) {
        a->last_state = st;
        show(a->row_bet, st == BJ_ST_BET);
        show(a->row_chips, st == BJ_ST_BET);
        show(a->row_ins, st == BJ_ST_INSURANCE);
        show(a->row_play, st == BJ_ST_PLAYER);
        show(a->row_over, st == BJ_ST_OVER);
        if (st == BJ_ST_OVER) {
            bool broke = bj_broke(g);
            show(a->btn_again, !broke);
            show(a->btn_rebet, !broke);
            show(a->btn_refill, broke);
        }
        if (st == BJ_ST_INSURANCE) {
            char buf[40];
            snprintf(buf, sizeof buf, "%s %ld", _("Seguro"), (long)(g->hand[0].bet / 2));
            lv_label_set_text(lv_obj_get_child(a->btn_ins, 0), buf);
            banner(a, _("La banca muestra un as. ¿Seguro?"), 0xFFFFFF, 0);
        }
        a->last_hint = 0;
    }
    if (st == BJ_ST_BET) {
        char buf[40];
        snprintf(buf, sizeof buf, "%s %ld", _("Apuesta"), (long)g->bet);
        lv_label_set_text(a->lbl_bet, buf);
        set_enabled(a->btn_deal, bj_can_deal(g));
        set_enabled(a->btn_clear, g->bet > 0);
    }
    if (st == BJ_ST_PLAYER) {
        set_enabled(a->btn_double, bj_can_double(g));
        set_enabled(a->btn_split, bj_can_split(g));
        char h = a->hint ? bj_advice(g) : 0;
        if (h != a->last_hint) {
            a->last_hint = h;
            set_hinted(a->btn_hit, h == 'h');
            set_hinted(a->btn_stand, h == 's');
            set_hinted(a->btn_double, h == 'd');
            set_hinted(a->btn_split, h == 'p');
        }
    }
    if (st == BJ_ST_OVER && a->btn_again && !lv_obj_has_flag(a->btn_again, LV_OBJ_FLAG_HIDDEN)) {
        set_enabled(a->btn_again, g->bet >= BJ_BET_MIN && g->bet <= g->bank);
    }
}

/* --------------------------------------------------------------------------
 * One event of the game, animated. Returns how long to wait before the next.
 * -------------------------------------------------------------------------- */

static int handle(app_t *a, const bj_ev_t *e)
{
    bj_game_t *g = &a->g;
    switch (e->type) {
    case BJ_EV_SHUFFLE:
        banner(a, _("Mezclando el sabot"), 0xFFFFFF, 900);
        beep(a, 660, 30);
        return 700;

    case BJ_EV_CARD:
        deal_card(a, e);
        pills_refresh(a);
        pile_refresh(a);
        bank_refresh(a);
        beep(a, 2200, 6);
        return DEAL_MS + 40;

    case BJ_EV_REVEAL: {
        int i = a->dslot[1];
        if (a->dn > 1 && i >= 0) {
            a->slot[i].flip_to = (int16_t)e->card;
            a->slot[i].ft      = 0;
        }
        pills_refresh(a);
        beep(a, 1600, 8);
        return FLIP_MS * 2 + 160;
    }

    case BJ_EV_SPLIT:
        a->nh = 2;
        a->hslot[1][0] = a->hslot[0][1];
        a->hn[1] = 1;
        a->hn[0] = 1;
        layout(a, 0);
        layout(a, 1);
        pills_refresh(a);
        pile_refresh(a);
        bank_refresh(a);
        return 280;

    case BJ_EV_TURN:
        pills_refresh(a);
        pile_refresh(a);
        bank_refresh(a);
        return 0;

    case BJ_EV_INSURANCE:
        if (e->amount) {
            char buf[48];
            snprintf(buf, sizeof buf, "%s +%ld", _("El seguro paga"), (long)e->amount);
            banner(a, buf, 0x7CFFA0, 1200);
        } else {
            banner(a, _("Seguro perdido"), 0xFF8A8A, 1000);
        }
        a->round_net += e->amount;
        pile_refresh(a);
        bank_refresh(a);
        return 600;

    case BJ_EV_RESULT: {
        const bj_hand_t *h = &g->hand[e->hand];
        a->round_net += e->amount;
        pills_refresh(a);
        pile_refresh(a);
        bank_refresh(a);
        switch (h->result) {
        case BJ_RES_BLACKJACK:
            beep(a, 784, 70); beep(a, 988, 70); beep(a, 1175, 70); beep(a, 1568, 180);
            break;
        case BJ_RES_WIN:
            beep(a, 784, 60); beep(a, 1047, 120);
            break;
        case BJ_RES_PUSH:
            beep(a, 660, 90);
            break;
        default:
            beep(a, 330, 90); beep(a, 247, 160);
            break;
        }
        return 380;
    }

    case BJ_EV_OVER: {
        int32_t net = a->round_net - a->round_stake;
        char    buf[48];
        if (net > 0) {
            snprintf(buf, sizeof buf, "+%ld", (long)net);
            banner(a, buf, 0x7CFFA0, 1800);
        } else if (net < 0) {
            snprintf(buf, sizeof buf, "%ld", (long)net);
            banner(a, buf, 0xFF8A8A, 1800);
        } else {
            banner(a, _("Empate"), 0xFFFFFF, 1500);
        }
        bank_refresh(a);
        prefs_save(a);
        return 0;
    }
    default:
        return 0;
    }
}

/* --------------------------------------------------------------------------
 * Actions
 * -------------------------------------------------------------------------- */

static void start_round(app_t *a)
{
    bj_game_t *g = &a->g;
    if (!bj_deal(g)) {
        return;
    }
    a->round_net   = 0;
    a->round_stake = g->bet;
    a->nh = 1;
    show(a->banner, false);
    a->last_state = -2;
    pile_refresh(a);
    bank_refresh(a);
}

/* The round over, cards back to the shoe; 'deal' starts the next with the
 * same bet. */
static void new_round(app_t *a, bool deal)
{
    collect(a);
    bj_next_round(&a->g);
    pills_refresh(a);
    show(a->banner, false);
    a->wait = 420;                       /* the cards leave before any come */
    if (deal) {
        start_round(a);
    }
    pile_refresh(a);
    a->last_state = -2;
}

static void act(app_t *a, char what)
{
    bj_game_t *g = &a->g;
    if (a->wait > 0 || bj_busy(g)) {
        return;
    }
    int32_t before = g->bank;
    switch (what) {
    case 'h': bj_hit(g); break;
    case 's': bj_stand(g); break;
    case 'd': bj_double(g); break;
    case 'p': bj_split(g); break;
    }
    a->round_stake += before - g->bank;  /* doubling and splitting stake more */
    a->last_state = -2;
    pile_refresh(a);
    bank_refresh(a);
}

static void hit_cb(lv_event_t *e)    { act((app_t *)lv_event_get_user_data(e), 'h'); }
static void stand_cb(lv_event_t *e)  { act((app_t *)lv_event_get_user_data(e), 's'); }
static void double_cb(lv_event_t *e) { act((app_t *)lv_event_get_user_data(e), 'd'); }
static void split_cb(lv_event_t *e)  { act((app_t *)lv_event_get_user_data(e), 'p'); }

static void chip_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    lv_obj_t *t = lv_event_get_target(e);
    int idx = (int)lv_obj_get_index(t);
    if (a->closing || a->g.state != BJ_ST_BET || idx < 0 || idx >= BJ_NCHIPS) {
        return;
    }
    int32_t before = a->g.bet;
    bj_bet_add(&a->g, bj_chip_value[idx]);
    if (a->g.bet != before) {
        beep(a, 1400 + idx * 200, 20);
    } else {
        beep(a, 300, 40);               /* no room: the bank or the limit */
    }
    pile_refresh(a);
    controls_refresh(a);
}

static void clear_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    bj_bet_clear(&a->g);
    pile_refresh(a);
    controls_refresh(a);
}

static void deal_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->wait <= 0) {
        start_round(a);
    }
}

static void insure_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    int32_t before = a->g.bank;
    bj_insure(&a->g, true);
    a->round_stake += before - a->g.bank;
    show(a->banner, false);
    a->last_state = -2;
    pile_refresh(a);
    bank_refresh(a);
}

static void noins_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    bj_insure(&a->g, false);
    show(a->banner, false);
    a->last_state = -2;
}

static void again_cb(lv_event_t *e)
{
    new_round((app_t *)lv_event_get_user_data(e), true);
}

static void rebet_cb(lv_event_t *e)
{
    new_round((app_t *)lv_event_get_user_data(e), false);
}

static void refill_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    bj_refill(&a->g);
    banner(a, _("Banca nueva: 1000"), 0xFFD35A, 1500);
    new_round(a, false);
    bank_refresh(a);
    prefs_save(a);
}

/* --------------------------------------------------------------------------
 * The menu
 * -------------------------------------------------------------------------- */

static void chip_set(lv_obj_t *c, const char *text, bool on)
{
    lv_obj_set_style_bg_color(c, lv_color_hex(on ? 0x1F5E3A : 0x1C1C24), 0);
    lv_obj_set_style_border_color(c, lv_color_hex(on ? 0x30D158 : 0x3A3A46), 0);
    lv_label_set_text(lv_obj_get_child(c, 0), text);
}

static void menu_refresh(app_t *a)
{
    const bj_game_t *g = &a->g;
    char             buf[200];
    unsigned         pct = g->rounds ? (unsigned)(g->wins * 100u / g->rounds) : 0;
    snprintf(buf, sizeof buf, "%s: %lu\n%s: %lu (%u %%)\n%s: %lu\n%s: %ld",
             _("Manos"), (unsigned long)g->rounds,
             _("Ganadas"), (unsigned long)g->wins, pct,
             _("Blackjacks"), (unsigned long)g->blackjacks,
             _("Mejor banca"), (long)g->best);
    lv_label_set_text(a->lbl_stats, buf);
    char s1[48], s2[48];
    snprintf(s1, sizeof s1, "%s %s", LV_SYMBOL_VOLUME_MAX, a->sfx ? _("Sonido") : _("Mudo"));
    snprintf(s2, sizeof s2, "%s %s", LV_SYMBOL_EYE_OPEN, _("Consejo"));
    chip_set(a->chip_sfx, s1, a->sfx);
    chip_set(a->chip_hint, s2, a->hint);
}

static void menu_show(app_t *a, bool on)
{
    if (on) {
        menu_refresh(a);
    }
    show(a->menu, on);
}

static void menu_cb(lv_event_t *e)     { menu_show((app_t *)lv_event_get_user_data(e), true); }
static void resume_cb(lv_event_t *e)   { menu_show((app_t *)lv_event_get_user_data(e), false); }

static void exit_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->want_exit = true;                /* deferred: aos_ui_back() destroys the app */
}

static void sfx_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->sfx = !a->sfx;
    menu_refresh(a);
    prefs_save(a);
}

static void hint_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->hint = !a->hint;
    a->last_hint = -1;
    menu_refresh(a);
    controls_refresh(a);
    prefs_save(a);
}

/* --------------------------------------------------------------------------
 * The timer
 * -------------------------------------------------------------------------- */

static void autoplay(app_t *a)
{
    bj_game_t *g = &a->g;
    if (g->state == BJ_ST_BET) {
        if (bj_broke(g)) {
            bj_refill(g);
        }
        if (!bj_can_deal(g)) {
            bj_bet_clear(g);
            bj_bet_add(g, 50);
        }
        start_round(a);
    } else if (g->state == BJ_ST_INSURANCE) {
        bj_insure(g, false);
    } else if (g->state == BJ_ST_PLAYER) {
        act(a, bj_advice(g));
    } else if (g->state == BJ_ST_OVER) {
        new_round(a, false);
    }
}

static void frame(lv_timer_t *timer)
{
    app_t *a = (app_t *)lv_timer_get_user_data(timer);
    if (a->closing) {
        return;
    }
    if (a->want_exit) {
        a->want_exit = false;
        a->leaving   = true;
        /* aos_ui_back() destroys the app: after this 'a' no longer exists */
        aos_ui_back();
        return;
    }
    uint64_t now = aos_hal_uptime_ms();
    int      dt  = (int)(uint32_t)(now - a->prev_ms);
    a->prev_ms = now;
    if (dt < 0 || dt > 100) {
        dt = FRAME_MS;
    }

    tweens(a, dt);

    if (a->banner_ms) {
        if (a->banner_ms <= dt) {
            a->banner_ms = 0;
            show(a->banner, false);
        } else {
            a->banner_ms = (uint16_t)(a->banner_ms - dt);
        }
    }

    if (!lv_obj_has_flag(a->menu, LV_OBJ_FLAG_HIDDEN)) {
        return;                         /* the table waits while it is open */
    }
    if (a->wait > 0) {
        a->wait -= dt;
        if (a->wait > 0) {
            controls_refresh(a);
            return;
        }
        a->wait = 0;
    }
    while (bj_busy(&a->g)) {
        bj_ev_t e = bj_step(&a->g);
        int     w = handle(a, &e);
        if (w > 0) {
            a->wait = w;
            break;
        }
    }
    controls_refresh(a);

    if (a->acts && *a->acts && a->wait <= 0 && !bj_busy(&a->g)) {
        if (a->g.state == BJ_ST_INSURANCE) {
            bj_insure(&a->g, *a->acts++ == 'y');
        } else if (a->g.state == BJ_ST_PLAYER) {
            act(a, *a->acts++);
        }
    }

    static uint16_t pause;
    if (a->autoplay && a->wait <= 0 && !bj_busy(&a->g)) {
        if (++pause > 12) {
            pause = 0;
            autoplay(a);
        }
    }
}

/* --------------------------------------------------------------------------
 * Back
 * -------------------------------------------------------------------------- */

static bool app_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    /* 'leaving': the Exit button goes through aos_ui_back(), which asks us
     * first. Without this we would open the menu, and never close. */
    if (!a || a->leaving) {
        return false;
    }
    if (lv_obj_has_flag(a->menu, LV_OBJ_FLAG_HIDDEN)) {
        menu_show(a, true);
        return true;
    }
    return false;                       /* from the menu, back leaves */
}

/* --------------------------------------------------------------------------
 * Building
 * -------------------------------------------------------------------------- */

static void build_controls(app_t *a, lv_obj_t *root)
{
    /* Betting: four chips on the cloth, then clear and deal. */
    a->row_chips = make_row(root, 256, 64);
    for (int i = 0; i < BJ_NCHIPS; i++) {
        bj_img_t *im = &a->chipimg[i];
        im->w  = BJ_CHIP_CV;
        im->h  = BJ_CHIP_CV;
        im->px = (uint32_t *)malloc((size_t)BJ_CHIP_CV * BJ_CHIP_CV * 4);
        if (!im->px) {
            continue;
        }
        bj_chip_draw(im, bj_chip_value[i]);
        lv_obj_t *c = lv_canvas_create(a->row_chips);
        lv_canvas_set_buffer(c, im->px, BJ_CHIP_CV, BJ_CHIP_CV, LV_COLOR_FORMAT_ARGB8888);
        lv_obj_set_size(c, BJ_CHIP_CV, BJ_CHIP_CV);
        lv_obj_set_pos(c, 118 + i * 62 - BJ_CHIP_CV / 2, 2);
        lv_obj_set_style_radius(c, 0, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_image_recolor(c, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
        lv_obj_set_style_image_recolor_opa(c, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_event_cb(c, chip_cb, LV_EVENT_CLICKED, a);
    }

    a->row_bet = make_row(root, 322, ROW_Y + ROW_H - 322);
    a->lbl_bet = make_label(a->row_bet, "", &aos_montserrat_20, 0xFFFFFF, 24, 4, AOS_SCREEN_W - 48);
    a->btn_clear = make_button(a->row_bet, _("Borrar"), 22, ROW_Y - 322, 108, ROW_H, 0x5A2A30,
                               &aos_montserrat_16, clear_cb, a);
    a->btn_deal = make_button(a->row_bet, _("Repartir"), 138, ROW_Y - 322, 208, ROW_H, 0x1F7A3A,
                              &aos_montserrat_20, deal_cb, a);

    a->row_ins = make_row(root, ROW_Y, ROW_H);
    a->btn_ins = make_button(a->row_ins, _("Seguro"), 22, 0, 190, ROW_H, 0x2D5DA8,
                             &aos_montserrat_16, insure_cb, a);
    make_button(a->row_ins, _("No"), 220, 0, 126, ROW_H, 0x4A5060, &aos_montserrat_16, noins_cb, a);

    a->row_play   = make_row(root, ROW_Y, ROW_H);
    a->btn_hit    = make_button(a->row_play, _("Pedir"), 20, 0, 72, ROW_H, 0x1F7A3A,
                                &aos_montserrat_16, hit_cb, a);
    a->btn_stand  = make_button(a->row_play, _("Plantarse"), 96, 0, 86, ROW_H, 0x7A1420,
                                &aos_montserrat_16, stand_cb, a);
    a->btn_double = make_button(a->row_play, _("Doblar"), 186, 0, 92, ROW_H, 0x8A6A12,
                                &aos_montserrat_16, double_cb, a);
    a->btn_split  = make_button(a->row_play, _("Dividir"), 282, 0, 66, ROW_H, 0x2D5DA8,
                                &aos_montserrat_16, split_cb, a);

    a->row_over   = make_row(root, ROW_Y, ROW_H);
    a->btn_again  = make_button(a->row_over, _("Otra mano"), 22, 0, 190, ROW_H, 0x1F7A3A,
                                &aos_montserrat_20, again_cb, a);
    a->btn_rebet  = make_button(a->row_over, _("Apuesta"), 220, 0, 126, ROW_H, 0x4A5060,
                                &aos_montserrat_16, rebet_cb, a);
    a->btn_refill = make_button(a->row_over, _("Banca nueva"), 64, 0, 240, ROW_H, 0x8A6A12,
                                &aos_montserrat_20, refill_cb, a);
}

static lv_obj_t *make_toggle(lv_obj_t *parent, int x, int y, int w, lv_event_cb_t cb, app_t *a)
{
    lv_obj_t *c = make_button(parent, "", x, y, w, 36, 0x1C1C24, &aos_montserrat_16, cb, a);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
    return c;
}

static void build_menu(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = lv_obj_create(root);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_style_bg_color(p, lv_color_hex(0x05100A), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_90, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);          /* touches do not pass */
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    a->menu = p;

    make_label(p, "Blackjack", &aos_montserrat_28, 0xF0CB62, 34, 40, 300);
    a->lbl_stats = make_label(p, "", &aos_montserrat_16, 0xDDE3EE, 34, 84, 300);
    lv_label_set_long_mode(a->lbl_stats, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_line_space(a->lbl_stats, 4, 0);

    a->chip_sfx  = make_toggle(p, 44, 192, 136, sfx_cb, a);
    a->chip_hint = make_toggle(p, 188, 192, 136, hint_cb, a);
    make_button(p, _("Seguir"), 64, 244, 240, 48, 0x1F7A3A, &aos_montserrat_20, resume_cb, a);
    make_button(p, _("Salir"), 64, 302, 240, 42, 0x7A1420, &aos_montserrat_20, exit_cb, a);
    make_label(p, _("La estrategia básica marca en dorado la jugada que sugiere."),
               &aos_montserrat_14, 0x9AA3B8, 44, 356, 280);
    lv_label_set_long_mode(lv_obj_get_child(p, -1), LV_LABEL_LONG_MODE_WRAP);
}

static void free_all(app_t *a)
{
    for (int i = 0; i < SLOTS; i++) {
        free(a->slot[i].img.px);
        a->slot[i].img.px = NULL;
    }
    for (int i = 0; i < BJ_NCHIPS; i++) {
        free(a->chipimg[i].px);
        a->chipimg[i].px = NULL;
    }
    free(a->pileimg.px);
    free(a->feltmem);
    a->pileimg.px = NULL;
    a->feltmem    = NULL;
    bj_art_free();
}

static void *blackjack_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) {
        return NULL;
    }
    uint32_t heap_int = 0, heap_psram = 0;
    aos_hal_heap_info(&heap_int, &heap_psram);
    aos_hal_log("blackjack", "opening | internal %u B, psram %u B",
                (unsigned)heap_int, (unsigned)heap_psram);

    a->root       = root;
    a->last_state = -2;
    a->pile_amount = -1;
    a->prev_ms    = aos_hal_uptime_ms();
    bj_game_init(&a->g, (uint32_t)aos_hal_uptime_ms() * 2654435761u + 1u, BJ_BANK0);
    prefs_load(a);

    /* The table: drawn at 32 bits in a temporary, kept at 16. */
    uint64_t t0 = aos_hal_uptime_ms();
    a->feltmem = (uint16_t *)malloc((size_t)AOS_SCREEN_W * AOS_SCREEN_H * 2);
    bj_img_t tmp = { (uint32_t *)malloc((size_t)AOS_SCREEN_W * AOS_SCREEN_H * 4),
                     AOS_SCREEN_W, AOS_SCREEN_H };
    a->pileimg.w  = BJ_STACK_W;
    a->pileimg.h  = BJ_STACK_H;
    a->pileimg.px = (uint32_t *)malloc((size_t)BJ_STACK_W * BJ_STACK_H * 4);
    if (!a->feltmem || !tmp.px || !a->pileimg.px || !bj_art_init()) {
        aos_hal_log("blackjack", "out of memory for the table");
        free(tmp.px);
        free_all(a);
        lv_free(a);
        return NULL;
    }
    bj_felt_draw(&tmp);
    bj_to_rgb565(&tmp, a->feltmem);
    free(tmp.px);
    bj_img_clear(&a->pileimg);

    lv_obj_set_style_bg_color(root, lv_color_hex(0x0A3A20), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    a->felt = lv_canvas_create(root);
    lv_canvas_set_buffer(a->felt, a->feltmem, AOS_SCREEN_W, AOS_SCREEN_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(a->felt, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(a->felt, 0, 0);
    lv_image_set_antialias(a->felt, false);
    lv_obj_remove_flag(a->felt, LV_OBJ_FLAG_CLICKABLE);
    print_table(a);

    /* the pile on the betting circle, and what it adds up to */
    a->pile = lv_canvas_create(root);
    lv_canvas_set_buffer(a->pile, a->pileimg.px, BJ_STACK_W, BJ_STACK_H, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_size(a->pile, BJ_STACK_W, BJ_STACK_H);
    lv_obj_set_pos(a->pile, 44 - BJ_STACK_W / 2, 300 + 11 - BJ_STACK_H);
    lv_obj_remove_flag(a->pile, LV_OBJ_FLAG_CLICKABLE);
    a->lbl_stake = make_label(root, "", &aos_montserrat_14, 0xF0CB62, 4, 334, 80);

    /* the cards' layer: above the cloth, below every word and button */
    a->table = lv_obj_create(root);
    lv_obj_remove_style_all(a->table);
    lv_obj_set_size(a->table, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(a->table, 0, 0);
    lv_obj_remove_flag(a->table, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->table, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < SLOTS; i++) {
        a->slot[i].flip_to = -3;
    }
    a->nh = 1;

    a->pill_d    = make_pill(root);
    a->pill_p[0] = make_pill(root);
    a->pill_p[1] = make_pill(root);

    /* the rail: the bank in the middle, the menu at the left */
    a->lbl_bank = make_label(root, "", &aos_montserrat_20, 0xF6E7B8, 84, 8, 200);
    lv_obj_t *mb = make_button(root, LV_SYMBOL_BARS, 30, 3, 50, 32, 0x2A1A0E,
                               &aos_montserrat_20, menu_cb, a);
    lv_obj_set_style_border_color(mb, lv_color_hex(0x8C6A2E), 0);
    lv_obj_set_style_border_opa(mb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mb, 1, 0);

    build_controls(a, root);

    a->banner = lv_label_create(root);
    lv_label_set_text(a->banner, "");
    lv_obj_set_style_text_font(a->banner, &aos_montserrat_20, 0);
    lv_obj_set_style_bg_color(a->banner, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(a->banner, LV_OPA_70, 0);
    lv_obj_set_style_radius(a->banner, 16, 0);
    lv_obj_set_style_pad_hor(a->banner, 16, 0);
    lv_obj_set_style_pad_ver(a->banner, 6, 0);
    lv_obj_set_style_text_align(a->banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_max_width(a->banner, 320, 0);
    lv_label_set_long_mode(a->banner, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(a->banner, LV_ALIGN_TOP_MID, 0, 186);
    lv_obj_remove_flag(a->banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(a->banner, LV_OBJ_FLAG_HIDDEN);

    build_menu(a, root);

    bank_refresh(a);
    pile_refresh(a);
    if (bj_broke(&a->g)) {
        bj_refill(&a->g);
        bank_refresh(a);
    }

    a->timer = lv_timer_create(frame, FRAME_MS, a);

#ifdef AOS_SIM_BUILTIN
    /* Development switches. On the board getenv() always returns NULL.
     *
     *   BJ_AUTO=1           basic strategy plays by itself, round after round
     *   BJ_SEED=42          the same shoe every time
     *   BJ_BANK=5000        start with that bank
     *   BJ_CARDS=0,12,9,22  the top of the shoe, in dealing order: player,
     *                       dealer up, player, dealer hole, then hits
     *                       (card = suit*13 + rank; rank 0 is the ace)
     *   BJ_DEAL=1           deal straight away
     *   BJ_ACTS=hsdp        play these, one after another, as the game waits
     *   BJ_SCREEN=menu      open the menu (the layout audit skips hidden
     *                       objects, and it is born hidden)
     *   BJ_HINT=1           the strategy hint on
     */
    {
        const char *env;
        if ((env = getenv("BJ_SEED")) && env[0]) {
            int32_t bank = a->g.bank, bet = a->g.bet;
            bj_game_init(&a->g, (uint32_t)atoi(env), bank);
            a->g.bet = bet;
        }
        if ((env = getenv("BJ_BANK")) && env[0]) {
            a->g.bank = atoi(env);
            bank_refresh(a);
        }
        if ((env = getenv("BJ_HINT")) && env[0]) {
            a->hint = true;
        }
        if ((env = getenv("BJ_CARDS")) && env[0]) {
            int n = 0;
            for (const char *s = env; *s && n < BJ_SHOE_N;) {
                a->g.shoe[n++] = (uint8_t)atoi(s);
                const char *c = strchr(s, ',');
                if (!c) {
                    break;
                }
                s = c + 1;
            }
            a->g.pos = 0;
        }
        if ((env = getenv("BJ_DEAL")) && env[0]) {
            start_round(a);
        }
        if ((env = getenv("BJ_ACTS")) && env[0]) {
            a->acts = env;              /* h s d p, and y / n for insurance */
        }
        if ((env = getenv("BJ_AUTO")) && env[0]) {
            a->autoplay = true;
        }
        if ((env = getenv("BJ_SCREEN")) && env[0] == 'm') {
            menu_show(a, true);
        }
    }
#endif

    aos_hal_heap_info(&heap_int, &heap_psram);
    aos_hal_log("blackjack", "ready in %u ms | internal %u B, psram %u B | bank %ld",
                (unsigned)(aos_hal_uptime_ms() - t0), (unsigned)heap_int, (unsigned)heap_psram,
                (long)a->g.bank);
    return a;
}

static void blackjack_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return;
    }
    if (a->timer) {
        lv_timer_delete(a->timer);
    }
    /* The objects are deleted HERE and not left to the runtime: it calls
     * destroy() and only then deletes the root, so an event from a finger
     * still down would reach a callback with the context freed. */
    a->closing = true;
    if (a->root) {
        lv_obj_clean(a->root);
    }
    prefs_save(a);
    aos_hal_log("blackjack", "closing | bank %ld, %lu rounds",
                (long)a->g.bank, (unsigned long)a->g.rounds);
    free_all(a);
    lv_free(a);
}

/* The launcher icon: two cards fanned, an ace of spades in front of a
 * card's back, on a green cloth. Percent coordinates. */
static const uint8_t BLACKJACK_ICON[] = {
    AIC_HEADER,
    /* the card behind: the red back, turned left */
    AIC_RECT(AIC_CENTER, -12, -2, 40, 56, 6, AIC_C_LIT(0xB0283A), 255),
    AIC_ROT(-150),
    AIC_BORDER(3, AIC_C_TEXT, 255),
    /* the ace in front, turned right */
    AIC_RECT(AIC_CENTER, 12, 2, 40, 56, 6, AIC_C_TEXT, 255),
    AIC_ROT(120),
    AIC_INTO,
    /* the spade: a square on its corner for the point, two lobes, a stem */
    AIC_RECT(AIC_CENTER, 0, -3, 15, 15, 1, AIC_C_BG, 255),
    AIC_ROT(450),
    AIC_RECT(AIC_CENTER, -6, 3, 12, 12, AIC_CIRCLE, AIC_C_BG, 255),
    AIC_RECT(AIC_CENTER, 6, 3, 12, 12, AIC_CIRCLE, AIC_C_BG, 255),
    AIC_RECT(AIC_CENTER, 0, 11, 5, 12, 1, AIC_C_BG, 255),
    AIC_OUT,
    AIC_END
};

static bool blackjack_init(aos_app_t *app)
{
    app->desc.id       = "demo.blackjack";
    app->desc.name     = "Blackjack";
    app->desc.icon     = LV_SYMBOL_SHUFFLE;     /* the fallback, if ever refused */
    /* The icon travels inside the .so (docs/ICONS.md): no firmware, no
     * reflash. icon_vec stays NONE so the glyph above is the only fallback. */
    app->desc.icon_vec = AOS_ICON_NONE;
    aos_icon_set_ops(app, BLACKJACK_ICON, sizeof BLACKJACK_ICON);
    /* the cloth: dark enough for the white card to read */
    app->desc.color_a  = 0x1F6B3E;
    app->desc.color_b  = 0x0B3320;
    app->desc.order    = 147;                   /* among the games */
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN;

    app->create  = blackjack_create;
    app->destroy = blackjack_destroy;
    app->back    = app_back;
    return true;
}

AOS_APP_ENTRY(blackjack_init);
