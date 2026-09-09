/*
 * CLIMA - weather icons, drawn in code.
 *
 * They are painted first onto an RGBA canvas of 8 bits per channel, which is
 * convenient for blending, and only at the end converted to RGB565A8. A large
 * icon's canvas is 43 KB asked for and given back in the same function.
 *
 * All in integers. The shapes' coordinates are in thousandths of the icon's
 * side, so the same description works for any size. The anti-aliasing comes
 * from measuring the distance to the edge in sixteenths of a pixel: one whole
 * pixel of transition is enough and it avoids sampling four at a time.
 */
#include "wx_art.h"

#include <stdlib.h>
#include <string.h>

/* lvgl.h does not drag in the image cache's header (it lives in
 * misc/cache/instance/), but the symbol is in the firmware's table. */
extern void lv_image_cache_drop(const void *src);

/* -------------------------------------------------------------------------- */
/* Canvas                                                                      */
/* -------------------------------------------------------------------------- */

typedef struct {
    uint8_t *px;                /* RGBA, 4 bytes per pixel */
    int      w, h;
} canvas_t;

static void px_blend(canvas_t *c, int x, int y, uint32_t rgb, int alpha)
{
    if (alpha <= 0 || x < 0 || y < 0 || x >= c->w || y >= c->h) {
        return;
    }
    if (alpha > 255) {
        alpha = 255;
    }
    uint8_t *p = c->px + (y * c->w + x) * 4;

    int sr = (rgb >> 16) & 0xFF, sg = (rgb >> 8) & 0xFF, sb = rgb & 0xFF;
    int da = p[3];

    /* "Source over" blending with premultiplied alpha done by hand. */
    int out_a = alpha + da * (255 - alpha) / 255;
    if (out_a == 0) {
        p[0] = p[1] = p[2] = p[3] = 0;
        return;
    }
    p[0] = (uint8_t)((sr * alpha + p[0] * da * (255 - alpha) / 255) / out_a);
    p[1] = (uint8_t)((sg * alpha + p[1] * da * (255 - alpha) / 255) / out_a);
    p[2] = (uint8_t)((sb * alpha + p[2] * da * (255 - alpha) / 255) / out_a);
    p[3] = (uint8_t)out_a;
}

/* 32-bit integer square root, by Newton over an initial binary estimate. */
static uint32_t isqrt32(uint32_t v)
{
    if (v == 0) {
        return 0;
    }
    uint32_t x = v, y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + v / x) / 2;
    }
    return x;
}

/* Distance in sixteenths of a pixel. */
static int dist16(int dx, int dy)
{
    return (int)isqrt32((uint32_t)(dx * dx + dy * dy) * 256u);
}

/* Coverage of an edge: 255 inside, 0 outside, and a one-pixel ramp. 'd' and
 * 'r' arrive in sixteenths. */
static int edge(int d, int r)
{
    int t = r - d;
    if (t <= 0)  return 0;
    if (t >= 16) return 255;
    return t * 255 / 16;
}

static void disc(canvas_t *c, int cx, int cy, int r, uint32_t rgb, int alpha)
{
    int r16 = r * 16;
    int x0 = (cx - r - 1) / 16, x1 = (cx + r + 16) / 16;
    int y0 = (cy - r - 1) / 16, y1 = (cy + r + 16) / 16;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            int d = dist16(x * 16 - cx, y * 16 - cy);
            int a = edge(d, r16);
            if (a) {
                px_blend(c, x, y, rgb, a * alpha / 255);
            }
        }
    }
}

/* Capsule: a rectangle with rounded ends, that is, a thick segment. Used for
 * the sun's rays, the fog bars and the clouds' base. */
static void capsule(canvas_t *c, int ax, int ay, int bx, int by, int r,
                    uint32_t rgb, int alpha)
{
    int r16 = r * 16;
    int minx = ((ax < bx ? ax : bx) - r - 16) / 16;
    int maxx = ((ax > bx ? ax : bx) + r + 16) / 16;
    int miny = ((ay < by ? ay : by) - r - 16) / 16;
    int maxy = ((ay > by ? ay : by) + r + 16) / 16;

    int vx = bx - ax, vy = by - ay;
    int len2 = vx * vx + vy * vy;

    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            int px = x * 16 - ax, py = y * 16 - ay;
            int t = 0;
            if (len2 > 0) {
                t = (px * vx + py * vy) * 256 / len2;   /* 0..256 along the axis */
                if (t < 0)   t = 0;
                if (t > 256) t = 256;
            }
            int qx = px - vx * t / 256;
            int qy = py - vy * t / 256;
            int a = edge(dist16(qx, qy), r16);
            if (a) {
                px_blend(c, x, y, rgb, a * alpha / 255);
            }
        }
    }
}

/* Droplet: a circle below and a point above. It is built as the union of a
 * disc and a triangle, measuring for each pixel which of the two covers it
 * more. */
static void drop(canvas_t *c, int cx, int cy, int r, uint32_t rgb, int alpha)
{
    int r16 = r * 16;
    int top = cy - r * 5 / 2;
    for (int y = (top - 16) / 16; y <= (cy + r + 16) / 16; y++) {
        for (int x = (cx - r - 16) / 16; x <= (cx + r + 16) / 16; x++) {
            int fx = x * 16, fy = y * 16;
            int a = edge(dist16(fx - cx, fy - cy), r16);
            if (fy < cy && a < 255) {
                /* width of the point: zero at the top, r at the centre's height */
                int span = (fy - top) * r16 / (cy - top);
                if (span > 0) {
                    int b = edge(abs(fx - cx) * 16 / 16, span);
                    if (b > a) a = b;
                }
            }
            if (a) {
                px_blend(c, x, y, rgb, a * alpha / 255);
            }
        }
    }
}

/* Filled polygon, by the even-odd rule over four vertical subsamples. Used by
 * the lightning bolt, which is the only shape with sharp corners. */
static void poly(canvas_t *c, const int *pts, int n, uint32_t rgb, int alpha)
{
    int minx = pts[0], maxx = pts[0], miny = pts[1], maxy = pts[1];
    for (int i = 0; i < n; i++) {
        if (pts[i * 2]     < minx) minx = pts[i * 2];
        if (pts[i * 2]     > maxx) maxx = pts[i * 2];
        if (pts[i * 2 + 1] < miny) miny = pts[i * 2 + 1];
        if (pts[i * 2 + 1] > maxy) maxy = pts[i * 2 + 1];
    }
    for (int y = miny / 16; y <= maxy / 16 + 1; y++) {
        for (int x = minx / 16; x <= maxx / 16 + 1; x++) {
            int hits = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    int fx = x * 16 + sx * 4 + 2;
                    int fy = y * 16 + sy * 4 + 2;
                    int inside = 0;
                    for (int i = 0, j = n - 1; i < n; j = i++) {
                        int yi = pts[i * 2 + 1], yj = pts[j * 2 + 1];
                        if ((yi > fy) == (yj > fy)) {
                            continue;
                        }
                        int xi = pts[i * 2], xj = pts[j * 2];
                        int cross = xi + (fy - yi) * (xj - xi) / (yj - yi);
                        if (fx < cross) {
                            inside = !inside;
                        }
                    }
                    hits += inside;
                }
            }
            if (hits) {
                px_blend(c, x, y, rgb, hits * 255 / 16 * alpha / 255);
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* The weather's shapes                                                        */
/* -------------------------------------------------------------------------- */

#define C_SUN_A     0xFFD60A
#define C_SUN_B     0xFF9F0A
#define C_MOON      0xE8E8F0
#define C_CLOUD_A   0xF2F2F7
#define C_CLOUD_B   0xB0B0BA
#define C_STORM_A   0xA0A0AA
#define C_STORM_B   0x6E6E78
#define C_RAIN      0x3AA0FF
#define C_SNOW      0xE8F4FF
#define C_BOLT      0xFFD60A
#define C_FOG       0xC8C8D0

/* u(x): from thousandths of the side to sixteenths of a pixel. */
#define U(v)   ((v) * size * 16 / 1000)

static void draw_sun(canvas_t *c, int size, int cx, int cy, int r, bool rays)
{
    if (rays) {
        /* Eight rays. The sines and cosines of 45 degrees come from a
         * four-entry table: libm is not needed for eight angles. */
        static const int dir[8][2] = {
            {  0, -1000 }, {  707, -707 }, { 1000, 0 }, {  707,  707 },
            {  0,  1000 }, { -707,  707 }, {-1000, 0 }, { -707, -707 },
        };
        int r0 = r * 128 / 100, r1 = r * 165 / 100;
        for (int i = 0; i < 8; i++) {
            capsule(c,
                    cx + dir[i][0] * r0 / 1000, cy + dir[i][1] * r0 / 1000,
                    cx + dir[i][0] * r1 / 1000, cy + dir[i][1] * r1 / 1000,
                    U(28), C_SUN_B, 255);
        }
    }
    disc(c, cx, cy, r, C_SUN_B, 255);
    disc(c, cx - r / 4, cy - r / 4, r * 72 / 100, C_SUN_A, 255);
}

static void draw_moon(canvas_t *c, int size, int cx, int cy, int r)
{
    /* Crescent: one filled disc and another that bites a piece out of it.
     * Since the canvas is RGBA, biting means writing zero alpha, and for that
     * the pixel has to be overwritten rather than blended. */
    disc(c, cx, cy, r, C_MOON, 255);

    int bx = cx + r * 55 / 100, by = cy - r * 42 / 100, br = r * 92 / 100;
    int br16 = br * 16;
    for (int y = (by - br - 16) / 16; y <= (by + br + 16) / 16; y++) {
        for (int x = (bx - br - 16) / 16; x <= (bx + br + 16) / 16; x++) {
            if (x < 0 || y < 0 || x >= c->w || y >= c->h) {
                continue;
            }
            int a = edge(dist16(x * 16 - bx, y * 16 - by), br16);
            if (!a) {
                continue;
            }
            uint8_t *p = c->px + (y * c->w + x) * 4;
            int keep = p[3] * (255 - a) / 255;
            p[3] = (uint8_t)keep;
        }
    }
    /* A couple of maria, which give the moon scale for free. */
    disc(c, cx - r * 30 / 100, cy + r * 10 / 100, r * 20 / 100, 0xC8C8D4, 160);
    disc(c, cx - r * 5 / 100,  cy + r * 45 / 100, r * 13 / 100, 0xC8C8D4, 130);
    (void)size;
}

/* Cloud: three humps and a straight base. cx/cy is the centre of the base. */
static void draw_cloud(canvas_t *c, int size, int cx, int cy, int w,
                       uint32_t top, uint32_t bottom)
{
    int r1 = w * 32 / 100;      /* large hump, on the right */
    int r2 = w * 24 / 100;      /* middle hump             */
    int r3 = w * 19 / 100;      /* small hump, on the left */

    int base_y = cy;
    disc(c, cx + w * 18 / 100, base_y - r1 * 62 / 100, r1, bottom, 255);
    disc(c, cx - w * 8 / 100,  base_y - r2 * 118 / 100, r2, top, 255);
    disc(c, cx - w * 33 / 100, base_y - r3 * 75 / 100, r3, bottom, 255);
    capsule(c, cx - w * 38 / 100, base_y - U(30), cx + w * 34 / 100, base_y - U(30),
            U(38), bottom, 255);
    (void)size;
}

static void draw_rain(canvas_t *c, int size, int cx, int cy, int w, int count, bool hard)
{
    int r = U(hard ? 34 : 26);
    for (int i = 0; i < count; i++) {
        int x = cx + (i - (count - 1)) * w * 24 / 100 + (i & 1 ? U(10) : 0);
        int y = cy + U(hard ? 60 : 50) + (i & 1 ? U(45) : 0);
        drop(c, x, y, r, C_RAIN, hard ? 255 : 210);
    }
    (void)size;
}

static void draw_snow(canvas_t *c, int size, int cx, int cy, int w, int count)
{
    for (int i = 0; i < count; i++) {
        int x = cx + (i - (count - 1)) * w * 24 / 100 + (i & 1 ? U(10) : 0);
        int y = cy + U(70) + (i & 1 ? U(40) : 0);
        int r = U(60);
        /* six points: three crossed capsules */
        static const int dir[3][2] = { { 0, 1000 }, { 866, 500 }, { 866, -500 } };
        for (int k = 0; k < 3; k++) {
            capsule(c, x - dir[k][0] * r / 1000, y - dir[k][1] * r / 1000,
                       x + dir[k][0] * r / 1000, y + dir[k][1] * r / 1000,
                       U(14), C_SNOW, 255);
        }
    }
    (void)size;
}

static void draw_bolt(canvas_t *c, int size, int cx, int cy)
{
    /* A seven-pointed bolt, in thousandths relative to the given centre. */
    static const int shape[7][2] = {
        {  60, -140 }, { -140,  110 }, { -20, 110 }, { -90, 300 },
        { 150,   40 }, {  20,   40 }, { 130, -140 },
    };
    int pts[14];
    for (int i = 0; i < 7; i++) {
        pts[i * 2]     = cx + U(shape[i][0]);
        pts[i * 2 + 1] = cy + U(shape[i][1]);
    }
    poly(c, pts, 7, C_BOLT, 255);
}

/* -------------------------------------------------------------------------- */
/* Assembling the sprite                                                       */
/* -------------------------------------------------------------------------- */

static void paint(canvas_t *c, int size, wx_icon_t icon, bool night)
{
    uint32_t top    = night ? 0xD8D8E4 : C_CLOUD_A;
    uint32_t bottom = night ? 0x9898A6 : C_CLOUD_B;

    switch (icon) {
    case WX_ICON_CLEAR:
        if (night) {
            draw_moon(c, size, U(500), U(500), U(300));
        } else {
            draw_sun(c, size, U(500), U(500), U(250), true);
        }
        break;

    case WX_ICON_FEWCLOUDS:
        if (night) {
            draw_moon(c, size, U(360), U(330), U(190));
        } else {
            draw_sun(c, size, U(345), U(320), U(165), true);
        }
        draw_cloud(c, size, U(560), U(730), U(620), top, bottom);
        break;

    case WX_ICON_CLOUDY:
        draw_cloud(c, size, U(590), U(560), U(500), top, bottom);
        draw_cloud(c, size, U(430), U(720), U(660), C_CLOUD_A, C_CLOUD_B);
        break;

    case WX_ICON_FOG:
        draw_cloud(c, size, U(500), U(520), U(640), top, bottom);
        for (int i = 0; i < 3; i++) {
            int y = U(660 + i * 130);
            int x0 = U(i == 1 ? 250 : 180);
            int x1 = U(i == 1 ? 830 : 760);
            capsule(c, x0, y, x1, y, U(40), C_FOG, 230 - i * 40);
        }
        break;

    case WX_ICON_DRIZZLE:
        draw_cloud(c, size, U(500), U(500), U(660), top, bottom);
        draw_rain(c, size, U(500), U(560), U(660), 3, false);
        break;

    case WX_ICON_RAIN:
        draw_cloud(c, size, U(500), U(480), U(680), top, bottom);
        draw_rain(c, size, U(500), U(540), U(680), 3, true);
        break;

    case WX_ICON_SNOW:
        draw_cloud(c, size, U(500), U(470), U(680), top, bottom);
        draw_snow(c, size, U(500), U(500), U(680), 3);
        break;

    case WX_ICON_STORM:
        draw_cloud(c, size, U(500), U(470), U(680), C_STORM_A, C_STORM_B);
        draw_bolt(c, size, U(500), U(640));
        break;

    default:
        draw_cloud(c, size, U(500), U(560), U(680), top, bottom);
        break;
    }
}

bool wx_art_make(wx_sprite_t *sp, wx_icon_t icon, bool night, int size)
{
    if (!sp || size < 8) {
        return false;
    }
    wx_art_free(sp);

    /* The working canvas is temporary and large: it goes through malloc, which
     * on the board lands in PSRAM. So does the final sprite, which is 3 bytes
     * per pixel. */
    canvas_t c = { .px = calloc(1, (size_t)size * size * 4), .w = size, .h = size };
    if (!c.px) {
        return false;
    }

    size_t bytes = (size_t)size * size * 3;
    sp->data = malloc(bytes);
    if (!sp->data) {
        free(c.px);
        return false;
    }

    paint(&c, size, icon, night);

    /* To RGB565A8: first the colour plane, with its stride of w*2, and then
     * the alpha plane, which starts at w*h*2 and has half the stride. That is
     * how LVGL's software drawer expects it. */
    uint16_t *color = (uint16_t *)sp->data;
    uint8_t  *alpha = sp->data + (size_t)size * size * 2;
    for (int i = 0; i < size * size; i++) {
        uint8_t *p = c.px + i * 4;
        color[i] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
        alpha[i] = p[3];
    }
    free(c.px);

    sp->dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    sp->dsc.header.cf     = LV_COLOR_FORMAT_RGB565A8;
    sp->dsc.header.w      = (uint32_t)size;
    sp->dsc.header.h      = (uint32_t)size;
    sp->dsc.header.stride = (uint32_t)(size * 2);
    sp->dsc.data          = sp->data;
    sp->dsc.data_size     = (uint32_t)bytes;

    sp->size  = size;
    sp->icon  = icon;
    sp->night = night;
    return true;
}

void wx_art_free(wx_sprite_t *sp)
{
    if (sp && sp->data) {
        /* The runtime deletes the objects AFTER destroy(), and LVGL's image
         * cache indexes by the descriptor's pointer: without releasing it, an
         * entry is left pointing at freed memory. */
        lv_image_cache_drop(&sp->dsc);
        free(sp->data);
        sp->data = NULL;
        sp->size = 0;
    }
}

uint32_t wx_art_mood(wx_icon_t icon, bool night)
{
    if (night) {
        return 0x101828;
    }
    switch (icon) {
    case WX_ICON_CLEAR:      return 0x123A5E;
    case WX_ICON_FEWCLOUDS:  return 0x14344F;
    case WX_ICON_CLOUDY:     return 0x22262C;
    case WX_ICON_FOG:        return 0x24262A;
    case WX_ICON_DRIZZLE:
    case WX_ICON_RAIN:       return 0x10283C;
    case WX_ICON_SNOW:       return 0x1E2A38;
    case WX_ICON_STORM:      return 0x201C2E;
    default:                 return 0x1C1C1E;
    }
}
