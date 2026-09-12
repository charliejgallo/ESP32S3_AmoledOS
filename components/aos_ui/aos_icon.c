/*
 * AmoledOS - Application icons.
 *
 * Every icon is a circle with a gradient and, on top, a glyph from the font or
 * a vector drawing made of LVGL objects. Zero bitmaps: it takes ~0 flash and
 * scales to any size without looking pixelated.
 */
#include "aos_theme.h"
#include <stdlib.h>

/* Icon hand: the theme's helper with the angle already applied. */
static lv_obj_t *hand(lv_obj_t *parent, int32_t w, int32_t h, int32_t deg,
                      lv_color_t color)
{
    lv_obj_t *obj = aos_hand_create(parent, w, h, color);
    aos_hand_set_angle(obj, deg * 10);
    return obj;
}

static lv_obj_t *ring(lv_obj_t *parent, int32_t size, int32_t border,
                      lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(obj, border, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_center(obj);
    return obj;
}

static lv_obj_t *activity_arc(lv_obj_t *parent, int32_t size, int32_t width,
                              int32_t value, lv_color_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_value(arc, value);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_center(arc);
    return arc;
}

static void draw_vector(lv_obj_t *base, aos_icon_id_t id, int32_t size)
{
    const int32_t s = size;

    switch (id) {
    case AOS_ICON_CLOCK:
    case AOS_ICON_ALARM: {
        ring(base, s * 68 / 100, LV_MAX(2, s / 26), AOS_C_TEXT);
        hand(base, LV_MAX(3, s / 24), s * 17 / 100, 60, AOS_C_TEXT);   /* hours   */
        hand(base, LV_MAX(2, s / 30), s * 26 / 100, 200, AOS_C_TEXT);  /* minutes */
        if (id == AOS_ICON_ALARM) {
            /* two "little bells" on top, like the alarm clock icon */
            for (int i = 0; i < 2; i++) {
                lv_obj_t *bell = lv_obj_create(base);
                lv_obj_remove_style_all(bell);
                lv_obj_set_size(bell, s * 16 / 100, s * 10 / 100);
                lv_obj_set_style_bg_color(bell, AOS_C_TEXT, 0);
                lv_obj_set_style_bg_opa(bell, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(bell, LV_RADIUS_CIRCLE, 0);
                lv_obj_align(bell, LV_ALIGN_CENTER,
                             (i == 0 ? -1 : 1) * s * 24 / 100, -s * 30 / 100);
            }
        }
        break;
    }

    case AOS_ICON_STOPWATCH: {
        ring(base, s * 66 / 100, LV_MAX(2, s / 26), AOS_C_TEXT);
        hand(base, LV_MAX(2, s / 30), s * 25 / 100, 135, AOS_C_TEXT);
        /* upper crown */
        lv_obj_t *crown = lv_obj_create(base);
        lv_obj_remove_style_all(crown);
        lv_obj_set_size(crown, s * 16 / 100, s * 9 / 100);
        lv_obj_set_style_bg_color(crown, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(crown, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(crown, s / 40, 0);
        lv_obj_align(crown, LV_ALIGN_CENTER, 0, -s * 38 / 100);
        break;
    }

    case AOS_ICON_TIMER: {
        lv_obj_t *arc = activity_arc(base, s * 68 / 100, LV_MAX(3, s / 16), 72,
                                     AOS_C_TEXT);
        lv_obj_set_style_arc_opa(arc, LV_OPA_30, LV_PART_MAIN);
        lv_obj_t *dot = lv_obj_create(base);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, s * 12 / 100, s * 12 / 100);
        lv_obj_set_style_bg_color(dot, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_center(dot);
        break;
    }

    case AOS_ICON_ACTIVITY: {
        activity_arc(base, s * 76 / 100, LV_MAX(4, s / 12), 78, AOS_C_PINK);
        activity_arc(base, s * 52 / 100, LV_MAX(4, s / 12), 62, AOS_C_GREEN);
        activity_arc(base, s * 28 / 100, LV_MAX(4, s / 12), 88, AOS_C_TEAL);
        break;
    }

    case AOS_ICON_FLASHLIGHT: {
        /* beam of light: a trapezoid approximated with two rectangles */
        lv_obj_t *head = lv_obj_create(base);
        lv_obj_remove_style_all(head);
        lv_obj_set_size(head, s * 34 / 100, s * 16 / 100);
        lv_obj_set_style_bg_color(head, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(head, s / 40, 0);
        lv_obj_align(head, LV_ALIGN_CENTER, 0, -s * 18 / 100);

        lv_obj_t *body = lv_obj_create(base);
        lv_obj_remove_style_all(body);
        lv_obj_set_size(body, s * 22 / 100, s * 34 / 100);
        lv_obj_set_style_bg_color(body, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(body, LV_OPA_70, 0);
        lv_obj_set_style_radius(body, s / 40, 0);
        lv_obj_align(body, LV_ALIGN_CENTER, 0, s * 14 / 100);
        break;
    }

    /* ---- Games ----------------------------------------------------------- */

    case AOS_ICON_PET: {
        /* Claudito: a cat's head, two triangular ears approximated with
         * squares rotated 45 degrees and two eyes. */
        lv_obj_t *head = lv_obj_create(base);
        lv_obj_remove_style_all(head);
        lv_obj_set_size(head, s * 46 / 100, s * 40 / 100);
        lv_obj_set_style_radius(head, s * 18 / 100, 0);
        lv_obj_set_style_bg_color(head, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
        lv_obj_align(head, LV_ALIGN_CENTER, 0, s * 6 / 100);

        for (int i = 0; i < 2; i++) {
            lv_obj_t *ear = lv_obj_create(base);
            lv_obj_remove_style_all(ear);
            lv_obj_set_size(ear, s * 17 / 100, s * 17 / 100);
            lv_obj_set_style_bg_color(ear, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(ear, LV_OPA_COVER, 0);
            lv_obj_set_style_transform_rotation(ear, 450, 0);   /* 45 degrees */
            lv_obj_set_style_transform_pivot_x(ear, lv_pct(50), 0);
            lv_obj_set_style_transform_pivot_y(ear, lv_pct(50), 0);
            lv_obj_align(ear, LV_ALIGN_CENTER,
                         (i ? 1 : -1) * s * 16 / 100, -s * 15 / 100);
        }

        for (int i = 0; i < 2; i++) {
            lv_obj_t *eye = lv_obj_create(head);
            lv_obj_remove_style_all(eye);
            lv_obj_set_size(eye, LV_MAX(2, s * 6 / 100), LV_MAX(2, s * 6 / 100));
            lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(eye, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
            lv_obj_align(eye, LV_ALIGN_CENTER,
                         (i ? 1 : -1) * s * 10 / 100, -s * 2 / 100);
        }
        break;
    }

    case AOS_ICON_SHIP: {
        /* 2043: a ship seen from above. A rhomboid body (a square at 45) with
         * two fins and an engine dot. */
        lv_obj_t *body = lv_obj_create(base);
        lv_obj_remove_style_all(body);
        lv_obj_set_size(body, s * 26 / 100, s * 26 / 100);
        lv_obj_set_style_bg_color(body, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(body, s / 30, 0);
        lv_obj_set_style_transform_rotation(body, 450, 0);
        lv_obj_set_style_transform_pivot_x(body, lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(body, lv_pct(50), 0);
        lv_obj_align(body, LV_ALIGN_CENTER, 0, -s * 6 / 100);

        for (int i = 0; i < 2; i++) {
            lv_obj_t *fin = lv_obj_create(base);
            lv_obj_remove_style_all(fin);
            lv_obj_set_size(fin, s * 8 / 100, s * 24 / 100);
            lv_obj_set_style_bg_color(fin, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(fin, LV_OPA_70, 0);
            lv_obj_set_style_radius(fin, s / 30, 0);
            lv_obj_align(fin, LV_ALIGN_CENTER,
                         (i ? 1 : -1) * s * 18 / 100, s * 8 / 100);
        }

        lv_obj_t *flame = lv_obj_create(base);
        lv_obj_remove_style_all(flame);
        lv_obj_set_size(flame, s * 9 / 100, s * 9 / 100);
        lv_obj_set_style_radius(flame, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(flame, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(flame, LV_OPA_50, 0);
        lv_obj_align(flame, LV_ALIGN_CENTER, 0, s * 24 / 100);
        break;
    }

    case AOS_ICON_GEM: {
        /* Gemas: a cut stone. One large rhombus and a light band on top that
         * acts as a facet. */
        lv_obj_t *stone = lv_obj_create(base);
        lv_obj_remove_style_all(stone);
        lv_obj_set_size(stone, s * 38 / 100, s * 38 / 100);
        lv_obj_set_style_bg_color(stone, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(stone, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(stone, s / 22, 0);
        lv_obj_set_style_transform_rotation(stone, 450, 0);
        lv_obj_set_style_transform_pivot_x(stone, lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(stone, lv_pct(50), 0);
        lv_obj_center(stone);

        lv_obj_t *facet = lv_obj_create(base);
        lv_obj_remove_style_all(facet);
        lv_obj_set_size(facet, s * 16 / 100, s * 16 / 100);
        lv_obj_set_style_bg_color(facet, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(facet, LV_OPA_30, 0);
        lv_obj_set_style_transform_rotation(facet, 450, 0);
        lv_obj_set_style_transform_pivot_x(facet, lv_pct(50), 0);
        lv_obj_set_style_transform_pivot_y(facet, lv_pct(50), 0);
        lv_obj_align(facet, LV_ALIGN_CENTER, 0, -s * 8 / 100);
        break;
    }

    case AOS_ICON_BRICKS: {
        /* Arkanos: two rows of bricks, the paddle and the ball. */
        for (int fila = 0; fila < 2; fila++) {
            for (int i = 0; i < 3; i++) {
                lv_obj_t *b = lv_obj_create(base);
                lv_obj_remove_style_all(b);
                lv_obj_set_size(b, s * 18 / 100, s * 9 / 100);
                lv_obj_set_style_bg_color(b, AOS_C_TEXT, 0);
                lv_obj_set_style_bg_opa(b, fila ? LV_OPA_60 : LV_OPA_COVER, 0);
                lv_obj_set_style_radius(b, s / 40, 0);
                lv_obj_align(b, LV_ALIGN_CENTER,
                             (i - 1) * s * 20 / 100,
                             -s * 24 / 100 + fila * s * 12 / 100);
            }
        }

        lv_obj_t *ball = lv_obj_create(base);
        lv_obj_remove_style_all(ball);
        lv_obj_set_size(ball, s * 10 / 100, s * 10 / 100);
        lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ball, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(ball, LV_OPA_COVER, 0);
        lv_obj_align(ball, LV_ALIGN_CENTER, s * 8 / 100, s * 6 / 100);

        lv_obj_t *pad = lv_obj_create(base);
        lv_obj_remove_style_all(pad);
        lv_obj_set_size(pad, s * 34 / 100, s * 7 / 100);
        lv_obj_set_style_bg_color(pad, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pad, LV_RADIUS_CIRCLE, 0);
        lv_obj_align(pad, LV_ALIGN_CENTER, 0, s * 26 / 100);
        break;
    }

    case AOS_ICON_BIRD: {
        /* Flappy: a round body, a wing, a beak and an eye. */
        lv_obj_t *body = lv_obj_create(base);
        lv_obj_remove_style_all(body);
        lv_obj_set_size(body, s * 40 / 100, s * 36 / 100);
        lv_obj_set_style_radius(body, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(body, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
        lv_obj_align(body, LV_ALIGN_CENTER, -s * 4 / 100, 0);

        lv_obj_t *wing = lv_obj_create(body);
        lv_obj_remove_style_all(wing);
        lv_obj_set_size(wing, s * 18 / 100, s * 11 / 100);
        lv_obj_set_style_radius(wing, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(wing, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(wing, LV_OPA_40, 0);
        lv_obj_align(wing, LV_ALIGN_CENTER, -s * 2 / 100, s * 3 / 100);

        lv_obj_t *eye = lv_obj_create(body);
        lv_obj_remove_style_all(eye);
        lv_obj_set_size(eye, LV_MAX(2, s * 7 / 100), LV_MAX(2, s * 7 / 100));
        lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(eye, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
        lv_obj_align(eye, LV_ALIGN_TOP_MID, s * 7 / 100, s * 6 / 100);

        lv_obj_t *beak = lv_obj_create(base);
        lv_obj_remove_style_all(beak);
        lv_obj_set_size(beak, s * 14 / 100, s * 8 / 100);
        lv_obj_set_style_bg_color(beak, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(beak, LV_OPA_70, 0);
        lv_obj_set_style_radius(beak, s / 40, 0);
        lv_obj_align(beak, LV_ALIGN_CENTER, s * 21 / 100, s * 2 / 100);
        break;
    }

    /* ---- Generic ones for games with no icon of their own ----------------- */

    case AOS_ICON_GAMEPAD: {
        lv_obj_t *body = lv_obj_create(base);
        lv_obj_remove_style_all(body);
        lv_obj_set_size(body, s * 56 / 100, s * 30 / 100);
        lv_obj_set_style_radius(body, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(body, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
        lv_obj_center(body);

        /* d-pad on the left */
        lv_obj_t *h = lv_obj_create(body);
        lv_obj_remove_style_all(h);
        lv_obj_set_size(h, s * 15 / 100, LV_MAX(2, s * 5 / 100));
        lv_obj_set_style_bg_color(h, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
        lv_obj_align(h, LV_ALIGN_LEFT_MID, s * 6 / 100, 0);

        lv_obj_t *v = lv_obj_create(body);
        lv_obj_remove_style_all(v);
        lv_obj_set_size(v, LV_MAX(2, s * 5 / 100), s * 15 / 100);
        lv_obj_set_style_bg_color(v, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
        lv_obj_align(v, LV_ALIGN_LEFT_MID, s * 11 / 100, 0);

        /* two buttons on the right */
        for (int i = 0; i < 2; i++) {
            lv_obj_t *b = lv_obj_create(body);
            lv_obj_remove_style_all(b);
            lv_obj_set_size(b, LV_MAX(2, s * 8 / 100), LV_MAX(2, s * 8 / 100));
            lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_align(b, LV_ALIGN_RIGHT_MID,
                         -s * (i ? 5 : 15) / 100, i ? s * 5 / 100 : -s * 5 / 100);
        }
        break;
    }

    case AOS_ICON_DICE: {
        lv_obj_t *cube = lv_obj_create(base);
        lv_obj_remove_style_all(cube);
        lv_obj_set_size(cube, s * 46 / 100, s * 46 / 100);
        lv_obj_set_style_radius(cube, s * 12 / 100, 0);
        lv_obj_set_style_bg_color(cube, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(cube, LV_OPA_COVER, 0);
        lv_obj_center(cube);

        /* five dots: the four corners and the centre */
        const int px[5] = { -1, 1, 0, -1, 1 };
        const int py[5] = { -1, -1, 0, 1, 1 };
        for (int i = 0; i < 5; i++) {
            lv_obj_t *pip = lv_obj_create(cube);
            lv_obj_remove_style_all(pip);
            lv_obj_set_size(pip, LV_MAX(2, s * 7 / 100), LV_MAX(2, s * 7 / 100));
            lv_obj_set_style_radius(pip, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(pip, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(pip, LV_OPA_COVER, 0);
            lv_obj_align(pip, LV_ALIGN_CENTER,
                         px[i] * s * 12 / 100, py[i] * s * 12 / 100);
        }
        break;
    }

    case AOS_ICON_JOYSTICK: {
        lv_obj_t *bs = lv_obj_create(base);
        lv_obj_remove_style_all(bs);
        lv_obj_set_size(bs, s * 44 / 100, s * 12 / 100);
        lv_obj_set_style_radius(bs, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(bs, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(bs, LV_OPA_70, 0);
        lv_obj_align(bs, LV_ALIGN_CENTER, 0, s * 24 / 100);

        lv_obj_t *stick = lv_obj_create(base);
        lv_obj_remove_style_all(stick);
        lv_obj_set_size(stick, LV_MAX(3, s * 8 / 100), s * 30 / 100);
        lv_obj_set_style_radius(stick, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(stick, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(stick, LV_OPA_COVER, 0);
        lv_obj_align(stick, LV_ALIGN_CENTER, 0, s * 4 / 100);

        lv_obj_t *knob = lv_obj_create(base);
        lv_obj_remove_style_all(knob);
        lv_obj_set_size(knob, s * 24 / 100, s * 24 / 100);
        lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(knob, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
        lv_obj_align(knob, LV_ALIGN_CENTER, 0, -s * 20 / 100);
        break;
    }

    case AOS_ICON_CALC: {
        /* a body with the display window on top and four keys below */
        lv_obj_t *body = lv_obj_create(base);
        lv_obj_remove_style_all(body);
        lv_obj_set_size(body, s * 52 / 100, s * 66 / 100);
        lv_obj_set_style_radius(body, LV_MAX(2, s / 12), 0);
        lv_obj_set_style_border_width(body, LV_MAX(2, s / 26), 0);
        lv_obj_set_style_border_color(body, AOS_C_TEXT, 0);
        lv_obj_center(body);

        lv_obj_t *screen = lv_obj_create(body);
        lv_obj_remove_style_all(screen);
        lv_obj_set_size(screen, s * 34 / 100, s * 13 / 100);
        lv_obj_set_style_bg_color(screen, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, s * 7 / 100);

        int32_t d = LV_MAX(2, s * 8 / 100);
        for (int i = 0; i < 4; i++) {
            lv_obj_t *k = lv_obj_create(body);
            lv_obj_remove_style_all(k);
            lv_obj_set_size(k, d, d);
            lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(k, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(k, (i == 3) ? LV_OPA_COVER : LV_OPA_60, 0);
            lv_obj_align(k, LV_ALIGN_TOP_MID,
                         (i % 2 ? 1 : -1) * s * 9 / 100,
                         s * (i < 2 ? 30 : 45) / 100);
        }
        break;
    }

    case AOS_ICON_LEVEL: {
        /* a horizontal tube with the bubble just off centre */
        lv_obj_t *tube = lv_obj_create(base);
        lv_obj_remove_style_all(tube);
        lv_obj_set_size(tube, s * 68 / 100, s * 26 / 100);
        lv_obj_set_style_radius(tube, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(tube, LV_MAX(2, s / 26), 0);
        lv_obj_set_style_border_color(tube, AOS_C_TEXT, 0);
        lv_obj_center(tube);

        lv_obj_t *bubble = lv_obj_create(tube);
        lv_obj_remove_style_all(bubble);
        lv_obj_set_size(bubble, s * 15 / 100, s * 15 / 100);
        lv_obj_set_style_radius(bubble, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(bubble, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
        lv_obj_align(bubble, LV_ALIGN_CENTER, s * 8 / 100, 0);
        break;
    }

    case AOS_ICON_REMOTE: {
        /* Body of the remote with the power button on top and a 2x3 grid of
         * keys. Everything cardinal on purpose: an icon with diagonals forces
         * LVGL to build a separate layer for every transform_angle. */
        lv_obj_t *body = lv_obj_create(base);
        lv_obj_remove_style_all(body);
        lv_obj_set_size(body, s * 38 / 100, s * 66 / 100);
        lv_obj_set_style_radius(body, s * 12 / 100, 0);
        lv_obj_set_style_border_width(body, LV_MAX(2, s / 26), 0);
        lv_obj_set_style_border_color(body, AOS_C_TEXT, 0);
        lv_obj_center(body);

        const int32_t d  = LV_MAX(2, s * 7 / 100);      /* key */
        const int32_t dx = s * 9 / 100;                 /* spacing */

        lv_obj_t *power = lv_obj_create(body);
        lv_obj_remove_style_all(power);
        lv_obj_set_size(power, d * 3 / 2, d * 3 / 2);
        lv_obj_set_style_radius(power, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(power, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(power, LV_OPA_COVER, 0);
        lv_obj_align(power, LV_ALIGN_CENTER, 0, -s * 20 / 100);

        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 2; col++) {
                lv_obj_t *key = lv_obj_create(body);
                lv_obj_remove_style_all(key);
                lv_obj_set_size(key, d, d);
                lv_obj_set_style_radius(key, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(key, AOS_C_TEXT, 0);
                lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
                lv_obj_align(key, LV_ALIGN_CENTER,
                             col ? dx : -dx, (row - 1) * dx + s * 12 / 100);
            }
        }
        break;
    }

    case AOS_ICON_MIC: {
        /* capsule, retaining arc and stand: the microphone of all time */
        lv_obj_t *capsule = lv_obj_create(base);
        lv_obj_remove_style_all(capsule);
        lv_obj_set_size(capsule, s * 22 / 100, s * 40 / 100);
        lv_obj_set_style_radius(capsule, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(capsule, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(capsule, LV_OPA_COVER, 0);
        lv_obj_align(capsule, LV_ALIGN_CENTER, 0, -s * 14 / 100);

        lv_obj_t *cradle = lv_arc_create(base);
        lv_obj_remove_style(cradle, NULL, LV_PART_KNOB);
        lv_obj_remove_style(cradle, NULL, LV_PART_MAIN);
        lv_obj_remove_flag(cradle, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(cradle, s * 46 / 100, s * 46 / 100);
        lv_arc_set_bg_angles(cradle, 20, 160);
        lv_arc_set_angles(cradle, 20, 160);
        lv_obj_set_style_arc_width(cradle, LV_MAX(2, s / 26), LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(cradle, AOS_C_TEXT, LV_PART_INDICATOR);
        lv_obj_align(cradle, LV_ALIGN_CENTER, 0, -s * 12 / 100);

        lv_obj_t *stem = lv_obj_create(base);
        lv_obj_remove_style_all(stem);
        lv_obj_set_size(stem, LV_MAX(2, s / 26), s * 12 / 100);
        lv_obj_set_style_bg_color(stem, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, 0);
        lv_obj_align(stem, LV_ALIGN_CENTER, 0, s * 22 / 100);

        lv_obj_t *foot = lv_obj_create(base);
        lv_obj_remove_style_all(foot);
        lv_obj_set_size(foot, s * 26 / 100, LV_MAX(2, s / 26));
        lv_obj_set_style_radius(foot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(foot, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(foot, LV_OPA_COVER, 0);
        lv_obj_align(foot, LV_ALIGN_CENTER, 0, s * 31 / 100);
        break;
    }

    case AOS_ICON_APPS: {
        /* four little squares, like a grid of applications */
        for (int i = 0; i < 4; i++) {
            lv_obj_t *sq = lv_obj_create(base);
            lv_obj_remove_style_all(sq);
            lv_obj_set_size(sq, s * 22 / 100, s * 22 / 100);
            lv_obj_set_style_bg_color(sq, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(sq, i == 3 ? LV_OPA_50 : LV_OPA_COVER, 0);
            lv_obj_set_style_radius(sq, s / 14, 0);
            lv_obj_align(sq, LV_ALIGN_CENTER,
                         (i % 2 ? 1 : -1) * s * 14 / 100,
                         (i / 2 ? 1 : -1) * s * 14 / 100);
        }
        break;
    }

    case AOS_ICON_WEATHER: {
        /* Sun behind and cloud in front, which is how "weather" reads at a
         * glance. The rays go only in the four cardinal directions: diagonally
         * they would have to be rotated, and a rotation in LVGL forces a
         * layer. */
        const int32_t sun_x = -s * 15 / 100;
        const int32_t sun_y = -s * 16 / 100;
        const int32_t ray_l = s * 9 / 100;
        const int32_t ray_w = LV_MAX(2, s * 5 / 100);
        const int32_t ray_d = s * 20 / 100;      /* from the centre of the sun to the ray */

        for (int i = 0; i < 4; i++) {
            lv_obj_t *ray = lv_obj_create(base);
            lv_obj_remove_style_all(ray);
            bool vertical = (i % 2) == 0;
            lv_obj_set_size(ray, vertical ? ray_w : ray_l,
                                 vertical ? ray_l : ray_w);
            lv_obj_set_style_bg_color(ray, AOS_C_YELLOW, 0);
            lv_obj_set_style_bg_opa(ray, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(ray, LV_RADIUS_CIRCLE, 0);
            lv_obj_align(ray, LV_ALIGN_CENTER,
                         sun_x + (i == 1 ? ray_d : (i == 3 ? -ray_d : 0)),
                         sun_y + (i == 2 ? ray_d : (i == 0 ? -ray_d : 0)));
        }

        lv_obj_t *sun = lv_obj_create(base);
        lv_obj_remove_style_all(sun);
        lv_obj_set_size(sun, s * 26 / 100, s * 26 / 100);
        lv_obj_set_style_bg_color(sun, AOS_C_YELLOW, 0);
        lv_obj_set_style_bg_opa(sun, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(sun, LV_RADIUS_CIRCLE, 0);
        lv_obj_align(sun, LV_ALIGN_CENTER, sun_x, sun_y);

        /* The cloud is drawn afterwards, so it covers the sun: three humps and
         * a base. */
        const int32_t base_y = s * 22 / 100;
        const struct { int32_t d, x, y; } lomo[3] = {
            { s * 30 / 100, s * 12 / 100, -s * 4 / 100 },
            { s * 24 / 100, -s * 9 / 100, -s * 9 / 100 },
            { s * 20 / 100, -s * 24 / 100,  0 },
        };
        for (int i = 0; i < 3; i++) {
            lv_obj_t *puff = lv_obj_create(base);
            lv_obj_remove_style_all(puff);
            lv_obj_set_size(puff, lomo[i].d, lomo[i].d);
            lv_obj_set_style_bg_color(puff, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(puff, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(puff, LV_RADIUS_CIRCLE, 0);
            lv_obj_align(puff, LV_ALIGN_CENTER, lomo[i].x, base_y + lomo[i].y);
        }

        lv_obj_t *bottom = lv_obj_create(base);
        lv_obj_remove_style_all(bottom);
        lv_obj_set_size(bottom, s * 60 / 100, s * 18 / 100);
        lv_obj_set_style_bg_color(bottom, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(bottom, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bottom, LV_RADIUS_CIRCLE, 0);
        lv_obj_align(bottom, LV_ALIGN_CENTER, -s * 3 / 100, base_y + s * 5 / 100);
        break;
    }

    case AOS_ICON_CALENDAR: {
        /* A sheet with the two rings on top, the header band and six days.
         * All cardinal shapes: a diagonal or a rotation in LVGL costs a layer,
         * and the menu redraws the visible icons on every frame of the
         * scroll. */
        const int32_t bw     = s * 58 / 100;    /* body */
        const int32_t bh     = s * 58 / 100;
        const int32_t border = LV_MAX(2, s / 26);
        const int32_t dy     = s * 5 / 100;     /* the body sits a little lower */

        /* The rings are drawn BEFORE the body so they sit behind it and only
         * poke out on top. */
        for (int i = 0; i < 2; i++) {
            lv_obj_t *tab = lv_obj_create(base);
            lv_obj_remove_style_all(tab);
            lv_obj_set_size(tab, LV_MAX(2, s * 6 / 100), s * 12 / 100);
            lv_obj_set_style_radius(tab, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(tab, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
            lv_obj_align(tab, LV_ALIGN_CENTER,
                         (i == 0 ? -1 : 1) * s * 16 / 100, dy - bh / 2 - s * 4 / 100);
        }

        lv_obj_t *body = lv_obj_create(base);
        lv_obj_remove_style_all(body);
        lv_obj_set_size(body, bw, bh);
        lv_obj_set_style_radius(body, LV_MAX(3, s / 9), 0);
        lv_obj_set_style_border_width(body, border, 0);
        lv_obj_set_style_border_color(body, AOS_C_TEXT, 0);
        lv_obj_align(body, LV_ALIGN_CENTER, 0, dy);

        lv_obj_t *band = lv_obj_create(body);
        lv_obj_remove_style_all(band);
        lv_obj_set_size(band, bw - 2 * border, s * 13 / 100);
        lv_obj_set_style_bg_color(band, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
        lv_obj_align(band, LV_ALIGN_TOP_MID, 0, border);

        /* Six days; the middle one of the first row is filled, which is the
         * app's marked "today". */
        const int32_t d = LV_MAX(2, s * 9 / 100);
        for (int i = 0; i < 6; i++) {
            lv_obj_t *dot = lv_obj_create(body);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, d, d);
            lv_obj_set_style_radius(dot, LV_MAX(1, s / 40), 0);
            lv_obj_set_style_bg_color(dot, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(dot, i == 1 ? LV_OPA_COVER : LV_OPA_50, 0);
            lv_obj_align(dot, LV_ALIGN_TOP_MID,
                         ((i % 3) - 1) * s * 16 / 100,
                         border + s * (i < 3 ? 22 : 36) / 100);
        }
        break;
    }

    case AOS_ICON_POMODORO: {
        /* The timer's tomato: a filled body, a stem and two leaves. The
         * left-hand leaf is dimmer so the volume reads without any diagonal (a
         * rotation here costs a layer per icon). */
        const int32_t d  = s * 56 / 100;
        const int32_t dy = s * 7 / 100;

        lv_obj_t *body = lv_obj_create(base);
        lv_obj_remove_style_all(body);
        lv_obj_set_size(body, d, d);
        lv_obj_set_style_radius(body, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(body, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
        lv_obj_align(body, LV_ALIGN_CENTER, 0, dy);

        lv_obj_t *stalk = lv_obj_create(base);
        lv_obj_remove_style_all(stalk);
        lv_obj_set_size(stalk, LV_MAX(2, s * 6 / 100), s * 12 / 100);
        lv_obj_set_style_radius(stalk, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(stalk, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(stalk, LV_OPA_COVER, 0);
        lv_obj_align(stalk, LV_ALIGN_CENTER, 0, dy - d / 2 - s * 4 / 100);

        for (int i = 0; i < 2; i++) {
            lv_obj_t *leaf = lv_obj_create(base);
            lv_obj_remove_style_all(leaf);
            lv_obj_set_size(leaf, s * 20 / 100, LV_MAX(2, s * 7 / 100));
            lv_obj_set_style_radius(leaf, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(leaf, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(leaf, i == 0 ? LV_OPA_60 : LV_OPA_COVER, 0);
            lv_obj_align(leaf, LV_ALIGN_CENTER,
                         (i == 0 ? -1 : 1) * s * 11 / 100, dy - d / 2 + s * 1 / 100);
        }
        break;
    }

    case AOS_ICON_GLOBE: {
        /* Globe: the ring, a meridian made with an empty pill (radius = half
         * the width, which is the closest thing to an ellipse without drawing
         * one) and three parallels. */
        const int32_t dia    = s * 66 / 100;
        const int32_t border = LV_MAX(2, s / 26);

        ring(base, dia, border, AOS_C_TEXT);

        lv_obj_t *meridian = lv_obj_create(base);
        lv_obj_remove_style_all(meridian);
        lv_obj_set_size(meridian, s * 30 / 100, dia);
        lv_obj_set_style_radius(meridian, s * 15 / 100, 0);
        lv_obj_set_style_border_width(meridian, border, 0);
        lv_obj_set_style_border_color(meridian, AOS_C_TEXT, 0);
        lv_obj_set_style_border_opa(meridian, LV_OPA_COVER, 0);
        lv_obj_center(meridian);

        /* The whole equator and two shorter parallels, inside the ring. */
        const int32_t widths[3] = { dia - 2 * border, dia * 76 / 100, dia * 76 / 100 };
        const int32_t offs[3]   = { 0, -dia * 26 / 100, dia * 26 / 100 };
        for (int i = 0; i < 3; i++) {
            lv_obj_t *par = lv_obj_create(base);
            lv_obj_remove_style_all(par);
            lv_obj_set_size(par, widths[i], border);
            lv_obj_set_style_bg_color(par, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(par, i == 0 ? LV_OPA_COVER : LV_OPA_70, 0);
            lv_obj_align(par, LV_ALIGN_CENTER, 0, offs[i]);
        }
        break;
    }

    case AOS_ICON_CONVERT: {
        /* The converter's two facing arrows. The heads are built by stacking
         * rectangles (a symmetric staircase) rather than by rotating a square:
         * a transform_rotation would be one layer per head, and the menu
         * redraws the visible icons on every frame of the scroll. */
        const int32_t u    = LV_MAX(2, s * 4 / 100);   /* step */
        const int32_t bar  = LV_MAX(2, s * 7 / 100);   /* shaft thickness */
        const int32_t len  = s * 44 / 100;
        const int32_t gap  = s * 11 / 100;             /* vertical spacing */

        for (int a = 0; a < 2; a++) {
            const int32_t dir = a == 0 ? 1 : -1;        /* 1 = head pointing right */
            const int32_t y   = a == 0 ? -gap : gap;
            const int32_t tip = dir * (len / 2);

            lv_obj_t *shaft = lv_obj_create(base);
            lv_obj_remove_style_all(shaft);
            lv_obj_set_size(shaft, len - 2 * u, bar);
            lv_obj_set_style_radius(shaft, bar / 2, 0);
            lv_obj_set_style_bg_color(shaft, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(shaft, LV_OPA_COVER, 0);
            lv_obj_align(shaft, LV_ALIGN_CENTER, -dir * u, y);

            /* The head: three columns of decreasing height towards the tip.
             * It is drawn by COLUMNS and not by symmetric rows -which would be
             * twice the objects- because the menu redraws the visible icons on
             * every frame of the scroll and this was the most expensive icon
             * in the catalogue: 14 objects against the calendar's 10. This way
             * it is 4. */
            for (int i = 0; i < 3; i++) {
                lv_obj_t *step = lv_obj_create(base);
                lv_obj_remove_style_all(step);
                lv_obj_set_size(step, u, (2 * i + 1) * u);   /* i=0 is the tip */
                lv_obj_set_style_bg_color(step, AOS_C_TEXT, 0);
                lv_obj_set_style_bg_opa(step, LV_OPA_COVER, 0);
                lv_obj_align(step, LV_ALIGN_CENTER,
                             tip - dir * (i * u + u / 2), y);
            }
        }
        break;
    }

    case AOS_ICON_LIFE: {
        /* A Game of Life glider on its 3x3 grid: the five live cells filled,
         * the four dead ones barely hinted at. */
        static const uint8_t GLIDER[9] = { 0, 1, 0,
                                           0, 0, 1,
                                           1, 1, 1 };
        const int32_t cell = s * 18 / 100;
        const int32_t step = s * 22 / 100;

        for (int i = 0; i < 9; i++) {
            lv_obj_t *dot = lv_obj_create(base);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, cell, cell);
            lv_obj_set_style_radius(dot, LV_MAX(1, s / 22), 0);
            lv_obj_set_style_bg_color(dot, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(dot, GLIDER[i] ? LV_OPA_COVER : LV_OPA_20, 0);
            lv_obj_align(dot, LV_ALIGN_CENTER,
                         ((i % 3) - 1) * step, ((i / 3) - 1) * step);
        }
        break;
    }

    case AOS_ICON_SIMON: {
        /* The four panels, in their usual colours and a black eye in the
         * middle. Four squares and a circle: nothing diagonal, nothing
         * rotated. */
        static const uint32_t PAD[4] = { 0x30D158, 0xFF453A,
                                         0xFFD60A, 0x0A84FF };
        const int32_t pad = s * 30 / 100;
        const int32_t off = s * 17 / 100;

        for (int i = 0; i < 4; i++) {
            lv_obj_t *q = lv_obj_create(base);
            lv_obj_remove_style_all(q);
            lv_obj_set_size(q, pad, pad);
            lv_obj_set_style_radius(q, s * 8 / 100, 0);
            lv_obj_set_style_bg_color(q, lv_color_hex(PAD[i]), 0);
            lv_obj_set_style_bg_opa(q, LV_OPA_COVER, 0);
            lv_obj_align(q, LV_ALIGN_CENTER,
                         (i & 1) ? off : -off, (i & 2) ? off : -off);
        }

        lv_obj_t *eye = lv_obj_create(base);
        lv_obj_remove_style_all(eye);
        lv_obj_set_size(eye, s * 20 / 100, s * 20 / 100);
        lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(eye, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
        lv_obj_center(eye);
        break;
    }

    case AOS_ICON_MINES: {
        /* Minesweeper's mine: a round body, four cardinal spikes and four studs
         * on the diagonals. The studs are little squares placed at the diagonal
         * offset and not rotated rectangles: a transform_rotation would be a
         * layer, and the menu redraws the visible icons on every frame of the
         * scroll. At this size a little square reads as a spike. */
        const int32_t d    = s * 40 / 100;
        const int32_t sp   = LV_MAX(2, s * 7 / 100);    /* spike thickness   */
        const int32_t reach = s * 64 / 100;             /* spike by spike, cardinal */
        const int32_t diag = s * 20 / 100;              /* centre of the stud */

        for (int i = 0; i < 2; i++) {
            lv_obj_t *spike = lv_obj_create(base);
            lv_obj_remove_style_all(spike);
            lv_obj_set_size(spike, i ? sp : reach, i ? reach : sp);
            lv_obj_set_style_radius(spike, sp / 2, 0);
            lv_obj_set_style_bg_color(spike, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(spike, LV_OPA_COVER, 0);
            lv_obj_center(spike);
        }

        for (int i = 0; i < 4; i++) {
            lv_obj_t *nub = lv_obj_create(base);
            lv_obj_remove_style_all(nub);
            lv_obj_set_size(nub, sp, sp);
            lv_obj_set_style_radius(nub, sp / 3, 0);
            lv_obj_set_style_bg_color(nub, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
            lv_obj_align(nub, LV_ALIGN_CENTER,
                         (i & 1) ? diag : -diag, (i & 2) ? diag : -diag);
        }

        lv_obj_t *body = lv_obj_create(base);
        lv_obj_remove_style_all(body);
        lv_obj_set_size(body, d, d);
        lv_obj_set_style_radius(body, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(body, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
        lv_obj_center(body);

        /* The highlight: a dot in the gradient's colour, top left, which is
         * the only thing that makes the ball read as a ball. */
        lv_obj_t *shine = lv_obj_create(body);
        lv_obj_remove_style_all(shine);
        lv_obj_set_size(shine, d * 26 / 100, d * 26 / 100);
        lv_obj_set_style_radius(shine, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(shine, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(shine, LV_OPA_40, 0);
        lv_obj_align(shine, LV_ALIGN_CENTER, -d * 20 / 100, -d * 20 / 100);
        break;
    }

    case AOS_ICON_MAZE: {
        /* A piece of maze: the frame, two walls inside and the ball. The walls
         * are placed so that an L-shaped corridor reads, which is what
         * distinguishes this from "a square with a dot". */
        const int32_t box = s * 62 / 100;
        const int32_t wt  = LV_MAX(2, s * 6 / 100);

        lv_obj_t *frame = lv_obj_create(base);
        lv_obj_remove_style_all(frame);
        lv_obj_set_size(frame, box, box);
        lv_obj_set_style_radius(frame, s * 6 / 100, 0);
        lv_obj_set_style_border_width(frame, wt, 0);
        lv_obj_set_style_border_color(frame, AOS_C_TEXT, 0);
        lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
        lv_obj_center(frame);

        lv_obj_t *v = lv_obj_create(frame);
        lv_obj_remove_style_all(v);
        lv_obj_set_size(v, wt, box * 46 / 100);
        lv_obj_set_style_bg_color(v, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
        lv_obj_align(v, LV_ALIGN_TOP_MID, -box * 8 / 100, 0);

        lv_obj_t *h = lv_obj_create(frame);
        lv_obj_remove_style_all(h);
        lv_obj_set_size(h, box * 44 / 100, wt);
        lv_obj_set_style_bg_color(h, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
        lv_obj_align(h, LV_ALIGN_BOTTOM_RIGHT, 0, -box * 18 / 100);

        lv_obj_t *ball = lv_obj_create(frame);
        lv_obj_remove_style_all(ball);
        lv_obj_set_size(ball, box * 24 / 100, box * 24 / 100);
        lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ball, AOS_C_ORANGE, 0);
        lv_obj_set_style_bg_opa(ball, LV_OPA_COVER, 0);
        lv_obj_align(ball, LV_ALIGN_TOP_RIGHT, -box * 6 / 100, box * 6 / 100);
        break;
    }

    case AOS_ICON_CARDS: {
        /* Two overlapping cards, the back one poking out at the top and on the
         * left. They are not rotated: a transform_angle forces LVGL to build a
         * separate layer and the launcher redraws the icons on every frame of
         * the scroll. Overlapping them with an offset is enough for them to
         * read as a pair of cards and not as a rectangle. */
        const int32_t cw = s * 34 / 100;
        const int32_t ch = s * 52 / 100;
        const int32_t off = s * 9 / 100;

        for (int i = 0; i < 2; i++) {
            lv_obj_t *c = lv_obj_create(base);
            lv_obj_remove_style_all(c);
            lv_obj_set_size(c, cw, ch);
            lv_obj_set_style_radius(c, LV_MAX(2, s * 6 / 100), 0);
            lv_obj_set_style_bg_color(c, i ? AOS_C_TEXT : lv_color_hex(0xD9CDB0), 0);
            lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(c, LV_MAX(1, s * 3 / 100), 0);
            lv_obj_set_style_border_color(c, lv_color_hex(0x3A3226), 0);
            lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
            lv_obj_align(c, LV_ALIGN_CENTER, i ? off : -off, i ? off : -off);

            if (i) {
                /* the suit: a coin in the centre of the front card */
                lv_obj_t *pip = lv_obj_create(c);
                lv_obj_remove_style_all(pip);
                lv_obj_set_size(pip, cw * 46 / 100, cw * 46 / 100);
                lv_obj_set_style_radius(pip, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(pip, lv_color_hex(0xD9A62B), 0);
                lv_obj_set_style_bg_opa(pip, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(pip, LV_MAX(1, s * 3 / 100), 0);
                lv_obj_set_style_border_color(pip, lv_color_hex(0x8E6510), 0);
                lv_obj_set_style_border_opa(pip, LV_OPA_COVER, 0);
                lv_obj_center(pip);
            }
        }
        break;
    }

    case AOS_ICON_CHART: {
        /* Two axes and a line rising in three steps. The steps are STACKED
         * rectangles and not diagonal segments: a diagonal asks for
         * transform_angle and that forces LVGL to build a separate layer,
         * which is what this file avoids everywhere. Three bars of different
         * heights read as a chart all the same —and in fact that is what the
         * app draws—. */
        int32_t gruesa = LV_MAX(2, s / 26);

        lv_obj_t *eje_y = lv_obj_create(base);
        lv_obj_remove_style_all(eje_y);
        lv_obj_set_size(eje_y, gruesa, s * 60 / 100);
        lv_obj_set_style_bg_color(eje_y, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(eje_y, LV_OPA_COVER, 0);
        lv_obj_align(eje_y, LV_ALIGN_CENTER, -s * 30 / 100, -s * 5 / 100);

        lv_obj_t *eje_x = lv_obj_create(base);
        lv_obj_remove_style_all(eje_x);
        lv_obj_set_size(eje_x, s * 62 / 100, gruesa);
        lv_obj_set_style_bg_color(eje_x, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(eje_x, LV_OPA_COVER, 0);
        lv_obj_align(eje_x, LV_ALIGN_CENTER, 0, s * 25 / 100);

        static const int32_t ALTO[3] = { 18, 34, 26 };
        for (int b = 0; b < 3; b++) {
            lv_obj_t *barra = lv_obj_create(base);
            lv_obj_remove_style_all(barra);
            int32_t h = s * ALTO[b] / 100;
            lv_obj_set_size(barra, s * 13 / 100, h);
            lv_obj_set_style_radius(barra, LV_MAX(1, s / 40), 0);
            lv_obj_set_style_bg_color(barra, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(barra, LV_OPA_COVER, 0);
            /* Resting on the axis: the base of each one has to end up on the
             * same line, so the vertical offset depends on the bar's height. */
            lv_obj_align(barra, LV_ALIGN_CENTER,
                         s * (-14 + b * 19) / 100,
                         s * 25 / 100 - h / 2 - gruesa);
        }
        break;
    }

    case AOS_ICON_MONEY: {
        /* A banknote lying flat and a coin overlapping on the right. The same
         * rule as the rest of the file: LVGL objects, zero bitmaps and no
         * rotation —a diagonal would ask for transform_angle, which forces
         * LVGL to build a separate layer, and the launcher redraws the icons
         * on every frame of the scroll—.
         *
         * And the same lesson in style the Clima icon left behind: the coin is
         * opaque and COVERS a corner of the banknote. Without overlap the two
         * elements compete and at 56 px the icon looks dirty.
         *
         * The sizes came from looking at it drawn, not from picking them: with
         * the banknote at 74% and the coin at 42% the two figures ran OUTSIDE
         * the launcher's circle —the bottom-left corner and the coin's edge
         * came out clipped—. With the radius at 28 px out of 56, the sum is
         * that the banknote's furthest corner and the coin's outer edge have
         * to end up inside that radius. */
        lv_obj_t *billete = lv_obj_create(base);
        lv_obj_remove_style_all(billete);
        lv_obj_set_size(billete, s * 64 / 100, s * 40 / 100);
        lv_obj_set_style_radius(billete, LV_MAX(2, s / 16), 0);
        lv_obj_set_style_border_width(billete, LV_MAX(2, s / 26), 0);
        lv_obj_set_style_border_color(billete, AOS_C_TEXT, 0);
        lv_obj_set_style_border_opa(billete, LV_OPA_COVER, 0);
        lv_obj_align(billete, LV_ALIGN_CENTER, -s * 7 / 100, -s * 5 / 100);

        /* The mark in the centre of the banknote: a little rectangle, not a
         * symbol. At 56 px a "$" inside the banknote is three pixels across
         * and looks like dirt; the block reads as "banknote" all the same. */
        lv_obj_t *marca = lv_obj_create(billete);
        lv_obj_remove_style_all(marca);
        lv_obj_set_size(marca, s * 20 / 100, s * 14 / 100);
        lv_obj_set_style_radius(marca, LV_MAX(1, s / 40), 0);
        lv_obj_set_style_bg_color(marca, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(marca, LV_OPA_COVER, 0);
        lv_obj_center(marca);

        lv_obj_t *moneda = lv_obj_create(base);
        lv_obj_remove_style_all(moneda);
        int32_t d = s * 38 / 100;
        lv_obj_set_size(moneda, d, d);
        lv_obj_set_style_radius(moneda, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(moneda, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(moneda, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(moneda, LV_MAX(2, s / 26), 0);
        lv_obj_set_style_border_color(moneda, AOS_C_BG, 0);
        lv_obj_set_style_border_opa(moneda, LV_OPA_COVER, 0);
        lv_obj_align(moneda, LV_ALIGN_CENTER, s * 20 / 100, s * 16 / 100);

        /* The hole in the middle of the coin, in the background colour: it
         * turns the filled circle into a thick ring without asking for another
         * border. */
        lv_obj_t *hueco = lv_obj_create(moneda);
        lv_obj_remove_style_all(hueco);
        lv_obj_set_size(hueco, d * 34 / 100, d * 34 / 100);
        lv_obj_set_style_radius(hueco, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(hueco, AOS_C_BG, 0);
        lv_obj_set_style_bg_opa(hueco, LV_OPA_COVER, 0);
        lv_obj_center(hueco);
        break;
    }

    case AOS_ICON_RADAR: {
        /* Three concentric rings and an echo off centre. Deliberately no
         * diagonal sweep: a diagonal line asks for transform_angle, and that
         * forces LVGL to build a separate layer for the icon — very expensive
         * on this board, and the launcher redraws them on every frame of the
         * scroll. The rings on their own already read as a radar. */
        ring(base, s * 78 / 100, LV_MAX(1, s / 32), AOS_C_TEXT);
        ring(base, s * 52 / 100, LV_MAX(1, s / 36), AOS_C_TEXT);
        ring(base, s * 26 / 100, LV_MAX(1, s / 40), AOS_C_TEXT);

        lv_obj_t *eco = lv_obj_create(base);
        lv_obj_remove_style_all(eco);
        int32_t d = LV_MAX(3, s * 13 / 100);
        lv_obj_set_size(eco, d, d);
        lv_obj_set_style_radius(eco, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(eco, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(eco, LV_OPA_COVER, 0);
        lv_obj_align(eco, LV_ALIGN_CENTER, s * 20 / 100, -s * 20 / 100);
        break;
    }

    case AOS_ICON_JUMP: {
        /* Claude Jump: the critter in mid-air over a platform, with two thrust
         * lines below. All cardinal, without a single rotation: a diagonal
         * asks for transform_angle and that costs a layer for every icon
         * visible on every frame of the menu's scroll. */
        lv_obj_t *plat = lv_obj_create(base);
        lv_obj_remove_style_all(plat);
        lv_obj_set_size(plat, s * 54 / 100, LV_MAX(3, s * 10 / 100));
        lv_obj_set_style_radius(plat, LV_MAX(2, s * 5 / 100), 0);
        lv_obj_set_style_bg_color(plat, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(plat, LV_OPA_COVER, 0);
        lv_obj_align(plat, LV_ALIGN_CENTER, 0, s * 30 / 100);

        /* The thrust lines: they say "this goes up" without drawing an arrow. */
        for (int i = 0; i < 2; i++) {
            lv_obj_t *raya = lv_obj_create(base);
            lv_obj_remove_style_all(raya);
            lv_obj_set_size(raya, LV_MAX(2, s * 5 / 100), s * 12 / 100);
            lv_obj_set_style_radius(raya, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(raya, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(raya, LV_OPA_60, 0);
            lv_obj_align(raya, LV_ALIGN_CENTER,
                         (i ? 1 : -1) * s * 20 / 100, s * 12 / 100);
        }

        lv_obj_t *cuerpo = lv_obj_create(base);
        lv_obj_remove_style_all(cuerpo);
        lv_obj_set_size(cuerpo, s * 46 / 100, s * 32 / 100);
        lv_obj_set_style_radius(cuerpo, s * 10 / 100, 0);
        lv_obj_set_style_bg_color(cuerpo, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(cuerpo, LV_OPA_COVER, 0);
        lv_obj_align(cuerpo, LV_ALIGN_CENTER, 0, -s * 16 / 100);

        for (int i = 0; i < 2; i++) {
            lv_obj_t *ojo = lv_obj_create(cuerpo);
            lv_obj_remove_style_all(ojo);
            lv_obj_set_size(ojo, LV_MAX(2, s * 7 / 100), LV_MAX(2, s * 7 / 100));
            lv_obj_set_style_radius(ojo, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(ojo, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(ojo, LV_OPA_COVER, 0);
            lv_obj_align(ojo, LV_ALIGN_CENTER, (i ? 1 : -1) * s * 10 / 100, 0);
        }

        /* The little legs, which are what distinguishes the critter from a box. */
        for (int i = 0; i < 3; i++) {
            lv_obj_t *pata = lv_obj_create(base);
            lv_obj_remove_style_all(pata);
            lv_obj_set_size(pata, LV_MAX(2, s * 4 / 100), s * 8 / 100);
            lv_obj_set_style_bg_color(pata, AOS_C_TEXT, 0);
            lv_obj_set_style_bg_opa(pata, LV_OPA_COVER, 0);
            lv_obj_align(pata, LV_ALIGN_CENTER,
                         (i - 1) * s * 14 / 100, s * 3 / 100);
        }
        break;
    }

    case AOS_ICON_PIXEL: {
        /* Pixel Art: a heart on a 5x5 grid, one square per lit cell and
         * nothing where the cell is off. Thirteen rectangles, all axis
         * aligned, no rotation: the menu redraws every visible icon per
         * frame of scroll and a layer per icon would show. */
        static const uint8_t heart[5] = { 0x0A, 0x1F, 0x1F, 0x0E, 0x04 };
        int32_t pitch = s * 14 / 100;
        int32_t cell  = LV_MAX(2, s * 11 / 100);
        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 5; c++) {
                if (!(heart[r] & (0x10 >> c))) {
                    continue;
                }
                lv_obj_t *q = lv_obj_create(base);
                lv_obj_remove_style_all(q);
                lv_obj_set_size(q, cell, cell);
                lv_obj_set_style_radius(q, LV_MAX(1, s * 2 / 100), 0);
                lv_obj_set_style_bg_color(q, AOS_C_TEXT, 0);
                lv_obj_set_style_bg_opa(q, LV_OPA_COVER, 0);
                lv_obj_align(q, LV_ALIGN_CENTER, (c - 2) * pitch, (r - 2) * pitch);
            }
        }
        break;
    }

    case AOS_ICON_MOLE: {
        /* Topos: a mole peeking out of its hole. Cardinal shapes only, like
         * the rest of the catalogue. The body is white as every shape here;
         * the mound in front is dirt-coloured, so it reads as the edge of the
         * hole and not as more mole, and the nose carries the only other
         * colour. */
        lv_obj_t *cuerpo = lv_obj_create(base);
        lv_obj_remove_style_all(cuerpo);
        lv_obj_set_size(cuerpo, s * 44 / 100, s * 52 / 100);
        lv_obj_set_style_radius(cuerpo, s * 22 / 100, 0);
        lv_obj_set_style_bg_color(cuerpo, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(cuerpo, LV_OPA_COVER, 0);
        lv_obj_align(cuerpo, LV_ALIGN_CENTER, 0, -s * 4 / 100);

        for (int i = 0; i < 2; i++) {
            lv_obj_t *ojo = lv_obj_create(cuerpo);
            lv_obj_remove_style_all(ojo);
            lv_obj_set_size(ojo, LV_MAX(2, s * 6 / 100), LV_MAX(3, s * 8 / 100));
            lv_obj_set_style_radius(ojo, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(ojo, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(ojo, LV_OPA_COVER, 0);
            lv_obj_align(ojo, LV_ALIGN_TOP_MID, (i ? 1 : -1) * s * 8 / 100,
                         s * 12 / 100);
        }

        lv_obj_t *nariz = lv_obj_create(cuerpo);
        lv_obj_remove_style_all(nariz);
        lv_obj_set_size(nariz, LV_MAX(3, s * 13 / 100), LV_MAX(2, s * 9 / 100));
        lv_obj_set_style_radius(nariz, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(nariz, lv_color_hex(0xFF8FA6), 0);
        lv_obj_set_style_bg_opa(nariz, LV_OPA_COVER, 0);
        lv_obj_align(nariz, LV_ALIGN_TOP_MID, 0, s * 22 / 100);

        lv_obj_t *monte = lv_obj_create(base);
        lv_obj_remove_style_all(monte);
        lv_obj_set_size(monte, s * 76 / 100, s * 20 / 100);
        lv_obj_set_style_radius(monte, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(monte, lv_color_hex(0xDDA05E), 0);
        lv_obj_set_style_bg_opa(monte, LV_OPA_COVER, 0);
        lv_obj_align(monte, LV_ALIGN_CENTER, 0, s * 25 / 100);
        break;
    }

    default:
        break;
    }
}

lv_obj_t *aos_icon_create(lv_obj_t *parent, const aos_app_desc_t *desc, int32_t size)
{
    lv_obj_t *base = lv_obj_create(parent);
    lv_obj_remove_style_all(base);
    lv_obj_set_size(base, size, size);
    lv_obj_set_style_radius(base, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(base, lv_color_hex(desc->color_a), 0);
    lv_obj_set_style_bg_grad_color(base, lv_color_hex(desc->color_b ? desc->color_b
                                                                   : desc->color_a), 0);
    lv_obj_set_style_bg_grad_dir(base, LV_GRAD_DIR_VER, 0);
    lv_obj_remove_flag(base, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(base, LV_OBJ_FLAG_CLICKABLE);

    if (desc->icon_vec != AOS_ICON_NONE) {
        draw_vector(base, desc->icon_vec, size);
    } else if (desc->icon && desc->icon[0]) {
        lv_obj_t *glyph = lv_label_create(base);
        lv_label_set_text(glyph, desc->icon);
        lv_obj_set_style_text_color(glyph, AOS_C_TEXT, 0);
        lv_obj_set_style_text_font(glyph, size >= 64 ? aos_font_title : aos_font_body, 0);
        lv_obj_center(glyph);
    }

    aos_make_decorative(base);
    return base;
}
