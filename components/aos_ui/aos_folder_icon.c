/*
 * AmoledOS - A folder's icon (see aos_folder_icon.h).
 *
 * Every pixel is computed here instead of asking LVGL for a polygon: the
 * hexagon is the exact signed distance to a rounded hexagon, so the edge is
 * antialiased to a fraction of a pixel and the corners are real arcs, and
 * the gradient is a formula the portal's SVG preview repeats with the same
 * numbers (see /menu's glifos.js consumer). An 82 px icon is 6,724 pixels;
 * drawing it costs well under a millisecond, once per launcher build.
 *
 * The glyph is rasterised by LVGL's font engine to an A8 mask at the font's
 * own size (GLYPH_PX in the generator, 42) and resampled bilinearly to half
 * the hexagon's height, so one font serves the three launcher sizes.
 */
#include "aos_folder_icon.h"
/* Not in lvgl.h, and needed: the board caches decoded images by source
 * pointer, and a freed buffer must leave that cache with it. */
#include "src/misc/cache/instance/lv_image_cache.h"

#include <math.h>
#include <string.h>

LV_FONT_DECLARE(aos_folder_font);

/* How round the corners are, as a fraction of the circumradius. 0.24 is the
 * 14 % the design mock-up settled on (a cut of 0.14 R along each edge, times
 * tan 60). */
#define CORNER      0.24f
#define GLYPH_SCALE 0.5f        /* the glyph's em box against the icon's height */
#define FONT_PX     42.0f       /* as GLYPH_PX in tools/gen_folder_glyphs.py */

uint32_t aos_folder_glyph_codepoint(const char *name)
{
    if (name) {
        for (int i = 0; i < aos_folder_glyph_count; i++) {
            if (strcmp(aos_folder_glyphs[i].name, name) == 0) {
                return aos_folder_glyphs[i].codepoint;
            }
        }
    }
    return aos_folder_glyphs[0].codepoint;
}

/* Signed distance to a regular hexagon with inradius r, pointy side up:
 * negative inside. Inigo Quilez's hexagon, with x and y swapped (his has the
 * flat side up). */
static float sd_hexagon(float px, float py, float r)
{
    const float kx = -0.866025404f, ky = 0.5f, kz = 0.577350269f;
    float x = fabsf(py), y = fabsf(px);
    float d = 2.0f * fminf(kx * x + ky * y, 0.0f);
    x -= d * kx;
    y -= d * ky;
    x -= fminf(fmaxf(x, -kz * r), kz * r);
    y -= r;
    float len = sqrtf(x * x + y * y);
    return y > 0.0f ? len : -len;
}

static uint8_t lerp8(uint8_t a, uint8_t b, float t)
{
    return (uint8_t)((float)a + ((float)b - (float)a) * t + 0.5f);
}

static void icon_deleted_cb(lv_event_t *e)
{
    lv_draw_buf_t *buf = (lv_draw_buf_t *)lv_event_get_user_data(e);
    lv_image_cache_drop(buf);
    lv_draw_buf_destroy(buf);
}

/* The glyph's mask, resampled into the icon and blended over its colour. */
static void draw_glyph(lv_draw_buf_t *dst, int32_t size, uint32_t cp, lv_color_t color)
{
    lv_font_glyph_dsc_t g;
    if (!lv_font_get_glyph_dsc(&aos_folder_font, &g, cp, 0) || !g.box_w || !g.box_h) {
        return;
    }
    lv_draw_buf_t *mask = lv_draw_buf_create(g.box_w, g.box_h, LV_COLOR_FORMAT_A8, 0);
    if (!mask) {
        return;
    }
    /* It returns the draw buffer it filled, not the pixels: for a bitmap
     * font that is 'mask' itself, and reading the returned pointer as data
     * reads the buffer's header as the glyph's first rows. */
    if (!lv_font_get_glyph_bitmap(&g, mask)) {
        lv_draw_buf_destroy(mask);
        return;
    }
    const uint8_t *bits = mask->data;
    const uint32_t mstride = mask->header.stride;

    /* Centred on its ink, not on its em box: MDI draws most icons centred in
     * the box but not all, and the page's preview centres the same way. */
    const float s = (float)size * GLYPH_SCALE / FONT_PX;
    const float w = (float)g.box_w * s, h = (float)g.box_h * s;
    const float x0 = ((float)size - w) / 2.0f, y0 = ((float)size - h) / 2.0f;

    const uint32_t dstride = dst->header.stride;
    for (int32_t y = (int32_t)y0; y < (int32_t)ceilf(y0 + h) && y < size; y++) {
        uint8_t *row = dst->data + y * dstride;
        for (int32_t x = (int32_t)x0; x < (int32_t)ceilf(x0 + w) && x < size; x++) {
            /* Bilinear, clamped: sample the mask at this pixel's centre. */
            float sx = ((float)x + 0.5f - x0) / s - 0.5f;
            float sy = ((float)y + 0.5f - y0) / s - 0.5f;
            int ix = (int)floorf(sx), iy = (int)floorf(sy);
            float fx = sx - (float)ix, fy = sy - (float)iy;
            float acc = 0.0f;
            for (int j = 0; j < 2; j++) {
                int yy = iy + j;
                if (yy < 0 || yy >= (int)g.box_h) continue;
                for (int i = 0; i < 2; i++) {
                    int xx = ix + i;
                    if (xx < 0 || xx >= (int)g.box_w) continue;
                    float wgt = (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy);
                    acc += wgt * (float)bits[yy * mstride + xx];
                }
            }
            if (acc < 1.0f) continue;
            float t = acc / 255.0f;
            uint8_t *px = row + x * 4;              /* B G R A */
            px[0] = lerp8(px[0], color.blue,  t);
            px[1] = lerp8(px[1], color.green, t);
            px[2] = lerp8(px[2], color.red,   t);
        }
    }
    lv_font_glyph_release_draw_data(&g);
    lv_draw_buf_destroy(mask);
}

lv_obj_t *aos_folder_icon_create(lv_obj_t *parent, const aos_menu_folder_t *f, int32_t size)
{
    lv_draw_buf_t *buf = lv_draw_buf_create(size, size, LV_COLOR_FORMAT_ARGB8888, 0);
    if (!buf) {
        return NULL;
    }

    /* The rounded tip lands on the image's edge: rounding pulls a vertex in
     * by (1/cos30 - 1) of the corner radius, so the sharp hexagon is made
     * that much larger to begin with. */
    const float half = (float)size / 2.0f;
    const float r0 = half - 0.5f;
    const float rc = CORNER * r0;
    const float inr = (r0 + 0.1547f * rc) * 0.866025404f;

    const lv_color_t a = lv_color_hex(f->color_a);
    const lv_color_t b = lv_color_hex(f->color_b);
    const float fsize = (float)size;

    const uint32_t stride = buf->header.stride;
    for (int32_t y = 0; y < size; y++) {
        uint8_t *row = buf->data + y * stride;
        for (int32_t x = 0; x < size; x++) {
            float cx = (float)x + 0.5f, cy = (float)y + 0.5f;
            float sd = sd_hexagon(cx - half, cy - half, inr - rc) - rc;
            float cov = 0.5f - sd;                  /* one pixel of ramp */
            uint8_t *px = row + x * 4;
            if (cov <= 0.0f) {
                px[0] = px[1] = px[2] = px[3] = 0;
                continue;
            }
            float t;
            switch (f->fill) {
            case AOS_MENU_FILL_VER:  t = cy / fsize; break;
            case AOS_MENU_FILL_DIAG: t = (cx + cy) / (2.0f * fsize); break;
            case AOS_MENU_FILL_RADIAL: {
                float dx = cx - 0.4f * fsize, dy = cy - 0.35f * fsize;
                t = sqrtf(dx * dx + dy * dy) / (0.7f * fsize);
                if (t > 1.0f) t = 1.0f;
                break;
            }
            case AOS_MENU_FILL_SOLID:
            default:                 t = 0.0f; break;
            }
            px[0] = lerp8(a.blue,  b.blue,  t);
            px[1] = lerp8(a.green, b.green, t);
            px[2] = lerp8(a.red,   b.red,   t);
            px[3] = cov >= 1.0f ? 255 : (uint8_t)(cov * 255.0f + 0.5f);
        }
    }

    draw_glyph(buf, size, aos_folder_glyph_codepoint(f->glyph),
               f->glyph_dark ? lv_color_black() : lv_color_white());

    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, buf);
    lv_obj_add_event_cb(img, icon_deleted_cb, LV_EVENT_DELETE, buf);
    return img;
}
