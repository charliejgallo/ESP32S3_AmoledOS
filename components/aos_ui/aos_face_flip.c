/*
 * AmoledOS - Split-flap face
 *
 * Two stacked cards, the hour on top and the minutes below, with the
 * horizontal slot across the middle of each, like an airport board.
 *
 * The flap's turn is NOT done by rotating anything. A transform in LVGL builds
 * a separate layer, memsets it, renders it and composites it; on the board
 * that is expensive and it is also exactly the path that hung the firmware
 * when a layer buffer did not fit. What is animated is a plain black
 * rectangle:
 *
 *   0 -> 100   grows from the top edge until it covers half the card
 *              (the flap falling)
 *   100        the number is changed, hidden under the black
 *   100 -> 200 shrinks from the bottom edge until it disappears
 *              (the flap coming to rest)
 *
 * It is two solid 280x75 fills over 200 ms, once a minute.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <stdio.h>
#include <string.h>

#define CARD_W      284
#define CARD_H      152
#define CARD_X      ((AOS_SCREEN_W - CARD_W) / 2)
#define CARD1_Y     58
#define CARD2_Y     (CARD1_Y + CARD_H + 18)
#define FLAP_MS     200

typedef struct {
    lv_obj_t *card;
    lv_obj_t *label;
    lv_obj_t *cover;
    char      shown[4];
    char      pending[4];
    bool      swapped;
} flip_card_t;

typedef struct {
    flip_card_t hour;
    flip_card_t minute;
    lv_obj_t   *date;
    lv_obj_t   *battery;
    bool        aod;
} flip_t;

/* -------------------------------------------------------------------------- */

static void flap_exec(void *var, int32_t t)
{
    flip_card_t *c = (flip_card_t *)var;
    const int32_t half = CARD_H / 2;

    if (t < 100) {
        lv_obj_remove_flag(c->cover, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(c->cover, CARD_W, half * t / 100);
        lv_obj_align(c->cover, LV_ALIGN_TOP_MID, 0, 0);
        return;
    }

    if (!c->swapped) {
        c->swapped = true;
        lv_label_set_text(c->label, c->pending);
        memcpy(c->shown, c->pending, sizeof(c->shown));
    }

    int32_t h = half * (200 - t) / 100;
    if (h <= 0) {
        lv_obj_add_flag(c->cover, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_size(c->cover, CARD_W, h);
    lv_obj_align(c->cover, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void card_set(flip_card_t *c, const char *text, bool animate)
{
    if (strcmp(c->shown, text) == 0) {
        return;
    }
    snprintf(c->pending, sizeof(c->pending), "%s", text);

    if (!animate) {
        lv_label_set_text(c->label, c->pending);
        memcpy(c->shown, c->pending, sizeof(c->shown));
        lv_obj_add_flag(c->cover, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    c->swapped = false;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, flap_exec);
    lv_anim_set_values(&a, 0, 200);
    lv_anim_set_duration(&a, FLAP_MS);
    lv_anim_start(&a);
}

static void build_card(lv_obj_t *root, flip_card_t *c, int y, lv_color_t color)
{
    c->card = lv_obj_create(root);
    lv_obj_remove_style_all(c->card);
    lv_obj_set_size(c->card, CARD_W, CARD_H);
    lv_obj_set_pos(c->card, CARD_X, y);
    lv_obj_set_style_radius(c->card, 22, 0);
    lv_obj_set_style_bg_color(c->card, lv_color_hex(0x1A1A1C), 0);
    lv_obj_set_style_bg_opa(c->card, LV_OPA_COVER, 0);
    lv_obj_remove_flag(c->card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c->card, LV_OBJ_FLAG_CLICKABLE);

    c->label = aos_label_scaled(c->card, "--", aos_font_huge, color, 420);
    lv_obj_center(c->label);

    /* The slot: it goes AFTER the number so it sits on top, which is what
     * makes it read as two halves and not as a card with a line on it. */
    lv_obj_t *slot = lv_obj_create(c->card);
    lv_obj_remove_style_all(slot);
    lv_obj_set_size(slot, CARD_W, 4);
    lv_obj_set_style_bg_color(slot, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, 0);
    lv_obj_align(slot, LV_ALIGN_CENTER, 0, 0);

    c->cover = lv_obj_create(c->card);
    lv_obj_remove_style_all(c->cover);
    lv_obj_set_size(c->cover, CARD_W, 0);
    lv_obj_set_style_bg_color(c->cover, lv_color_hex(0x0A0A0B), 0);
    lv_obj_set_style_bg_opa(c->cover, LV_OPA_COVER, 0);
    lv_obj_add_flag(c->cover, LV_OBJ_FLAG_HIDDEN);

    c->shown[0] = '\0';
}

/* -------------------------------------------------------------------------- */

static void *create(lv_obj_t *root)
{
    flip_t *face = lv_malloc_zeroed(sizeof(flip_t));
    if (!face) {
        return NULL;
    }

    face->date = aos_label(root, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(face->date, LV_ALIGN_TOP_MID, 0, 24);

    build_card(root, &face->hour,   CARD1_Y, AOS_C_TEXT);
    build_card(root, &face->minute, CARD2_Y, AOS_C_ORANGE);

    face->battery = aos_label(root, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(face->battery, LV_ALIGN_BOTTOM_MID, 0, -22);

    return face;
}

static void set_aod(void *ctx, bool aod)
{
    flip_t *face = (flip_t *)ctx;
    if (!face) {
        return;
    }
    face->aod = aod;

    lv_obj_set_style_text_color(face->hour.label,
                                aod ? lv_color_hex(0x9A9A9A) : AOS_C_TEXT, 0);
    lv_obj_set_style_text_color(face->minute.label,
                                aod ? lv_color_hex(0x6A4A10) : AOS_C_ORANGE, 0);
    /* No card background when dimmed: that is 2 x 284x152 lit pixels. */
    lv_obj_set_style_bg_opa(face->hour.card,   aod ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(face->minute.card, aod ? LV_OPA_TRANSP : LV_OPA_COVER, 0);

    if (aod) {
        lv_obj_add_flag(face->date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(face->battery, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(face->date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(face->battery, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh(void *ctx, const struct tm *now)
{
    flip_t *face = (flip_t *)ctx;
    if (!face) {
        return;
    }

    char buf[16];
    /* A box of 16 even though three fit: for GCC a %d takes up to 11
     * characters and -Werror=format-truncation stops the firmware's build. */
    snprintf(buf, sizeof(buf), "%02d", now->tm_hour);
    card_set(&face->hour, buf, !face->aod);

    snprintf(buf, sizeof(buf), "%02d", now->tm_min);
    card_set(&face->minute, buf, !face->aod);

    if (face->aod) {
        return;
    }

    char line[32];
    snprintf(line, sizeof(line), "%s %d %s", aos_day_name(now->tm_wday),
             now->tm_mday, aos_month_name(now->tm_mon));
    lv_label_set_text(face->date, line);

    aos_battery_t batt;
    if (aos_hal_battery_read(&batt) && batt.percent >= 0) {
        snprintf(line, sizeof(line), "%s%d%%",
                 batt.charging ? LV_SYMBOL_CHARGE " " : "", batt.percent);
        lv_label_set_text(face->battery, line);
    }
}

static void destroy(void *ctx)
{
    flip_t *face = (flip_t *)ctx;
    if (face) {
        /* The animations point at the context's cards: if one is left running
         * when the face is changed, it writes into freed memory. */
        lv_anim_delete(&face->hour, flap_exec);
        lv_anim_delete(&face->minute, flap_exec);
    }
    lv_free(ctx);
}

void aos_face_flip_get(aos_watchface_t *face)
{
    face->id      = "flip";
    face->name    = N_("Tarjetas");
    face->create  = create;
    face->refresh = refresh;
    face->set_aod = set_aod;
    face->destroy = destroy;
}
