/*
 * AmoledOS - Analogue face.
 *
 * The hands are rectangles rotated about the centre. The dial's markers are
 * NOT: they are painted once onto a canvas.
 *
 * Reason, measured on the board: in LVGL, an object with transform_rotation is
 * a LAYER (separate buffer, memset, render and compositing). The twelve
 * markers were twelve 14x152 layers, and since the second hand sweeps nearly
 * the whole dial, every second they were all rebuilt: 45 ms per frame with the
 * clock standing still.
 *
 * Drawn onto the canvas they are ordinary pixels. And what matters most: LVGL
 * clips the drawing to the invalidated area, so only the strip the second hand
 * touches is repainted from the canvas. A layer, by contrast, is rebuilt WHOLE
 * even when a sliver of it is visible.
 *
 * In dimmed mode the second hand disappears, which is the only thing forcing a
 * redraw.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define DIAL_R      152
#define TICKS       12

typedef struct {
    lv_obj_t *dial;
    lv_obj_t *marcas;           /* canvas with the twelve markers already painted */
    uint16_t *marcas_buf;
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *second;
    lv_obj_t *center;
    lv_obj_t *date;
    bool      aod;
} analog_t;

/* Paints a rotated rectangle straight into the canvas's buffer.
 *
 * 'radio' is the distance from the dial's centre to the marker's OUTER edge.
 * The marker is walked in its own coordinates (u across, v along) and each
 * point is carried onto the dial by rotating it: no LVGL transformation is
 * needed, and therefore no layer. */
static void marca_pintar(uint16_t *buf, int lado, float grados,
                         int ancho, int largo, int radio, uint16_t color)
{
    const float a   = grados * 3.14159265f / 180.0f;
    const float sa  = sinf(a), ca = cosf(a);
    const float cx  = lado / 2.0f, cy = lado / 2.0f;

    for (int v = 0; v < largo; v++) {
        float r = (float)(radio - v);
        for (int u = -ancho / 2; u <= ancho / 2; u++) {
            /* 0 degrees is twelve o'clock: outwards is (sin, -cos). */
            float x = cx + (float)u * ca + r * sa;
            float y = cy + (float)u * sa - r * ca;
            int   xi = (int)(x + 0.5f), yi = (int)(y + 0.5f);
            if (xi >= 0 && xi < lado && yi >= 0 && yi < lado) {
                buf[yi * lado + xi] = color;
            }
        }
    }
}

static void *create(lv_obj_t *root)
{
    analog_t *face = lv_malloc_zeroed(sizeof(analog_t));
    if (!face) {
        return NULL;
    }

    face->dial = lv_obj_create(root);
    lv_obj_remove_style_all(face->dial);
    lv_obj_set_size(face->dial, DIAL_R * 2, DIAL_R * 2);
    lv_obj_align(face->dial, LV_ALIGN_CENTER, 0, -18);

    /* 304x304 in RGB565 is ~185 KB: it goes to PSRAM, of which there is
     * plenty. */
    const int lado = DIAL_R * 2;
    face->marcas_buf = malloc((size_t)lado * lado * sizeof(uint16_t));
    if (face->marcas_buf) {
        memset(face->marcas_buf, 0, (size_t)lado * lado * sizeof(uint16_t));

        for (int i = 0; i < TICKS; i++) {
            bool mayor = (i % 3) == 0;
            marca_pintar(face->marcas_buf, lado, (float)(i * 30),
                         mayor ? 6 : 3, mayor ? 20 : 11, DIAL_R - 6,
                         mayor ? lv_color_to_u16(AOS_C_TEXT)
                               : lv_color_to_u16(AOS_C_DIM));
        }

        face->marcas = lv_canvas_create(face->dial);
        lv_canvas_set_buffer(face->marcas, face->marcas_buf, lado, lado,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_center(face->marcas);
        lv_obj_remove_flag(face->marcas, LV_OBJ_FLAG_CLICKABLE);
    }

    face->date = aos_label(root, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(face->date, LV_ALIGN_BOTTOM_MID, 0, -34);

    face->hour   = aos_hand_create(face->dial, 9, 86,  AOS_C_TEXT);
    face->minute = aos_hand_create(face->dial, 7, 128, AOS_C_TEXT);
    face->second = aos_hand_create(face->dial, 3, 140, AOS_C_RED);

    face->center = lv_obj_create(face->dial);
    lv_obj_remove_style_all(face->center);
    lv_obj_set_size(face->center, 14, 14);
    lv_obj_set_style_radius(face->center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(face->center, AOS_C_RED, 0);
    lv_obj_set_style_bg_opa(face->center, LV_OPA_COVER, 0);
    lv_obj_center(face->center);

    return face;
}

static void set_aod(void *ctx, bool aod)
{
    analog_t *face = (analog_t *)ctx;
    if (!face) {
        return;
    }
    face->aod = aod;

    lv_color_t hand_color = aod ? lv_color_hex(0x8A8A8A) : AOS_C_TEXT;
    lv_obj_set_style_bg_color(face->hour, hand_color, 0);
    lv_obj_set_style_bg_color(face->minute, hand_color, 0);
    lv_obj_set_style_bg_color(face->center,
                              aod ? lv_color_hex(0x8A8A8A) : AOS_C_RED, 0);

    /* Ordinary opacity, not opa_layered: the first is multiplied into the blit
     * and the second would build a layer, which is precisely what we came here
     * to avoid. */
    if (face->marcas) {
        lv_obj_set_style_opa(face->marcas, aod ? LV_OPA_30 : LV_OPA_COVER, 0);
    }

    if (aod) {
        lv_obj_add_flag(face->second, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(face->date, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(face->second, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(face->date, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh(void *ctx, const struct tm *now)
{
    analog_t *face = (analog_t *)ctx;
    if (!face) {
        return;
    }

    /* angles in tenths of a degree */
    int32_t minute_angle = (int32_t)(now->tm_min * 60 + now->tm_sec);   /* 0..3599 */
    aos_hand_set_angle(face->minute, minute_angle);

    int32_t hour_angle = (int32_t)(((now->tm_hour % 12) * 3600 + minute_angle) / 12);
    aos_hand_set_angle(face->hour, hour_angle);

    if (face->aod) {
        return;
    }

    aos_hand_set_angle(face->second, (int32_t)(now->tm_sec * 60));

    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d", aos_day_name(now->tm_wday), now->tm_mday);
    lv_label_set_text(face->date, buf);
}

static void destroy(void *ctx)
{
    analog_t *face = (analog_t *)ctx;
    if (face) {
        free(face->marcas_buf);
    }
    lv_free(ctx);
}

void aos_face_analog_get(aos_watchface_t *face)
{
    face->id      = "analog";
    face->name    = N_("Analogica");
    face->create  = create;
    face->refresh = refresh;
    face->set_aod = set_aod;
    face->destroy = destroy;
}
