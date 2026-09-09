/*
 * ARKANOS - pixel art engine (see ak_pixel.h)
 */
#include "ak_pixel.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Buffer and clip
 * -------------------------------------------------------------------------- */

void ak_buf_init(ak_buf_t *b, uint16_t *px, int w, int h)
{
    b->px = px;
    b->w  = (int16_t)w;
    b->h  = (int16_t)h;
    ak_clip_none(b);
}

void ak_clip(ak_buf_t *b, int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > b->w) x1 = b->w;
    if (y1 > b->h) y1 = b->h;
    b->cx0 = (int16_t)x0;
    b->cy0 = (int16_t)y0;
    b->cx1 = (int16_t)(x1 > x0 ? x1 : x0);
    b->cy1 = (int16_t)(y1 > y0 ? y1 : y0);
}

void ak_clip_none(ak_buf_t *b)
{
    b->cx0 = 0;
    b->cy0 = 0;
    b->cx1 = b->w;
    b->cy1 = b->h;
}

uint16_t ak_mix(uint16_t a, uint16_t b, int f)
{
    if (f <= 0) return a;
    if (f >= 16) return b;

    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;

    int r  = ar + (br - ar) * f / 16;
    int g  = ag + (bg - ag) * f / 16;
    int bl = ab + (bb - ab) * f / 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

uint16_t ak_tone(uint16_t c, int f)
{
    return f < 0 ? ak_mix(c, 0x0000, -f) : ak_mix(c, 0xFFFF, f);
}

/* --------------------------------------------------------------------------
 * Dirty rectangles
 *
 * The list is deliberately short. Every rectangle costs twice: once on
 * upscaling and once on invalidating, and LVGL also builds a draw area per
 * invalidation. Twenty rectangles of ten pixels come out more expensive than
 * one of two hundred, so on adding, something to merge with is looked for
 * first.
 *
 * The merge criterion is the wasted area: it is accepted if the rectangle
 * enclosing both adds no more than AK_JOIN_SLACK pixels to the sum of the two
 * separately. With that the ball's trail and its four dots end up in a single
 * rectangle, and the paddle and a capsule falling at the other end stay
 * separate, which is what you want.
 * -------------------------------------------------------------------------- */

#define AK_JOIN_SLACK   380

static inline int rect_area(const ak_rect_t *r)
{
    return (r->x1 - r->x0) * (r->y1 - r->y0);
}

/* how much 'a' grows if it swallows 'b', in pixels */
static int join_cost(const ak_rect_t *a, const ak_rect_t *b)
{
    int x0 = a->x0 < b->x0 ? a->x0 : b->x0;
    int y0 = a->y0 < b->y0 ? a->y0 : b->y0;
    int x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    int y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    return (x1 - x0) * (y1 - y0) - rect_area(a) - rect_area(b);
}

static void join_into(ak_rect_t *a, const ak_rect_t *b)
{
    if (b->x0 < a->x0) a->x0 = b->x0;
    if (b->y0 < a->y0) a->y0 = b->y0;
    if (b->x1 > a->x1) a->x1 = b->x1;
    if (b->y1 > a->y1) a->y1 = b->y1;
}

void ak_dirty_reset(ak_dirty_t *d)
{
    d->n = 0;
    d->all = false;
}

void ak_dirty_all(ak_dirty_t *d)
{
    d->n = 0;
    d->all = true;
}

void ak_dirty_add(ak_dirty_t *d, int x, int y, int w, int h)
{
    if (d->all || w <= 0 || h <= 0) {
        return;
    }
    ak_rect_t n;
    n.x0 = (int16_t)(x < 0 ? 0 : x);
    n.y0 = (int16_t)(y < 0 ? 0 : y);
    n.x1 = (int16_t)(x + w > AK_W ? AK_W : x + w);
    n.y1 = (int16_t)(y + h > AK_H ? AK_H : y + h);
    if (n.x1 <= n.x0 || n.y1 <= n.y0) {
        return;
    }

    int best = -1, best_cost = 0;
    for (int i = 0; i < d->n; i++) {
        int cost = join_cost(&d->r[i], &n);
        if (best < 0 || cost < best_cost) {
            best = i;
            best_cost = cost;
        }
    }

    /* If there is room in the list, it only merges when it comes cheap. If
     * there is not, it merges anyway with the cheapest: losing a rectangle is
     * worse than repainting too much. */
    if (best >= 0 && (best_cost <= AK_JOIN_SLACK || d->n >= AK_MAX_DIRTY)) {
        join_into(&d->r[best], &n);
        return;
    }
    d->r[d->n++] = n;
}

void ak_dirty_join(ak_dirty_t *dst, const ak_dirty_t *src)
{
    if (dst->all) {
        return;
    }
    if (src->all) {
        ak_dirty_all(dst);
        return;
    }
    for (int i = 0; i < src->n; i++) {
        const ak_rect_t *r = &src->r[i];
        ak_dirty_add(dst, r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0);
    }
}

int ak_dirty_area(const ak_dirty_t *d)
{
    if (d->all) {
        return AK_W * AK_H;
    }
    int total = 0;
    for (int i = 0; i < d->n; i++) {
        total += rect_area(&d->r[i]);
    }
    return total;
}

void ak_restore(uint16_t *dst, const uint16_t *src, const ak_rect_t *r)
{
    int span = r->x1 - r->x0;
    if (span <= 0) {
        return;
    }
    for (int y = r->y0; y < r->y1; y++) {
        size_t off = (size_t)y * AK_W + r->x0;
        memcpy(dst + off, src + off, (size_t)span * sizeof(uint16_t));
    }
}

void ak_expand(const uint16_t *src, uint16_t *dst, const ak_rect_t *r)
{
    const int dw = AK_W * AK_SCALE;
    int span = r->x1 - r->x0;
    if (span <= 0) {
        return;
    }

    for (int y = r->y0; y < r->y1; y++) {
        const uint16_t *s   = src + (size_t)y * AK_W + r->x0;
        uint16_t       *row = dst + (size_t)y * AK_SCALE * dw + r->x0 * AK_SCALE;

        /* Two 16-bit writes per source pixel. The pair could be assembled into
         * a uint32_t, but the destination is only aligned to 4 when the source
         * column is even and the rectangle starts wherever it likes. */
        for (int x = 0; x < span; x++) {
            uint16_t c = s[x];
            row[x * 2]     = c;
            row[x * 2 + 1] = c;
        }
        /* the second row is identical */
        memcpy(row + dw, row, (size_t)span * AK_SCALE * sizeof(uint16_t));
    }
}

/* --------------------------------------------------------------------------
 * Primitives
 * -------------------------------------------------------------------------- */

void ak_px(ak_buf_t *b, int x, int y, uint16_t c)
{
    if (x < b->cx0 || y < b->cy0 || x >= b->cx1 || y >= b->cy1) {
        return;
    }
    b->px[y * b->w + x] = c;
}

void ak_rect(ak_buf_t *b, int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    int x0 = x < b->cx0 ? b->cx0 : x;
    int y0 = y < b->cy0 ? b->cy0 : y;
    int x1 = x + w; if (x1 > b->cx1) x1 = b->cx1;
    int y1 = y + h; if (y1 > b->cy1) y1 = b->cy1;
    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    /* Two pixels per write. The background is 41 thousand pixels and on the
     * board the buffer lives in PSRAM, where every short write hurts. */
    uint32_t pair = ((uint32_t)c << 16) | c;

    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = &b->px[yy * b->w];
        int xx = x0;

        if ((xx & 1) && xx < x1) {
            row[xx++] = c;
        }
        int pairs = (x1 - xx) / 2;
        uint32_t *p32 = (uint32_t *)(void *)&row[xx];
        for (int i = 0; i < pairs; i++) {
            p32[i] = pair;
        }
        xx += pairs * 2;
        while (xx < x1) {
            row[xx++] = c;
        }
    }
}

void ak_fill(ak_buf_t *b, uint16_t c)
{
    ak_rect(b, 0, 0, b->w, b->h, c);
}

void ak_hline(ak_buf_t *b, int x, int y, int len, uint16_t c)
{
    ak_rect(b, x, y, len, 1, c);
}

void ak_vline(ak_buf_t *b, int x, int y, int len, uint16_t c)
{
    ak_rect(b, x, y, 1, len, c);
}

void ak_frame(ak_buf_t *b, int x, int y, int w, int h, uint16_t c)
{
    ak_hline(b, x, y, w, c);
    ak_hline(b, x, y + h - 1, w, c);
    ak_vline(b, x, y, h, c);
    ak_vline(b, x + w - 1, y, h, c);
}

void ak_line(ak_buf_t *b, int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int steps = adx > ady ? adx : ady;

    if (steps == 0) {
        ak_px(b, x0, y0, c);
        return;
    }
    for (int i = 0; i <= steps; i++) {
        ak_px(b, x0 + dx * i / steps, y0 + dy * i / steps, c);
    }
}

void ak_disc(ak_buf_t *b, int cx, int cy, int r, uint16_t c)
{
    if (r < 0) {
        return;
    }
    int r2 = r * r + r;
    for (int dy = -r; dy <= r; dy++) {
        int span = 0;
        while ((span + 1) * (span + 1) + dy * dy <= r2) {
            span++;
        }
        ak_hline(b, cx - span, cy + dy, span * 2 + 1, c);
    }
}

void ak_ring(ak_buf_t *b, int cx, int cy, int r, uint16_t c)
{
    if (r <= 0) {
        ak_px(b, cx, cy, c);
        return;
    }
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        ak_px(b, cx + x, cy + y, c); ak_px(b, cx + y, cy + x, c);
        ak_px(b, cx - y, cy + x, c); ak_px(b, cx - x, cy + y, c);
        ak_px(b, cx - x, cy - y, c); ak_px(b, cx - y, cy - x, c);
        ak_px(b, cx + y, cy - x, c); ak_px(b, cx + x, cy - y, c);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void ak_round(ak_buf_t *b, int x, int y, int w, int h, int cut, uint16_t c)
{
    for (int yy = 0; yy < h; yy++) {
        int inset = 0;
        if (yy < cut) {
            inset = cut - yy;
        } else if (yy >= h - cut) {
            inset = cut - (h - 1 - yy);
        }
        ak_hline(b, x + inset, y + yy, w - inset * 2, c);
    }
}

void ak_vgrad(ak_buf_t *b, int x, int y0, int w, int y1, uint16_t top, uint16_t bot)
{
    if (y1 < y0) {
        return;
    }
    int span = y1 - y0;
    for (int y = y0; y <= y1; y++) {
        int f = span > 0 ? (y - y0) * 16 / span : 0;
        ak_hline(b, x, y, w, ak_mix(top, bot, f));
    }
}

void ak_shade(ak_buf_t *b, int x, int y, int w, int h, int f)
{
    int x0 = x < b->cx0 ? b->cx0 : x;
    int y0 = y < b->cy0 ? b->cy0 : y;
    int x1 = x + w; if (x1 > b->cx1) x1 = b->cx1;
    int y1 = y + h; if (y1 > b->cy1) y1 = b->cy1;
    uint16_t target = f < 0 ? 0x0000 : 0xFFFF;
    int amount = f < 0 ? -f : f;

    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = &b->px[yy * b->w];
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = ak_mix(row[xx], target, amount);
        }
    }
}

void ak_glow(ak_buf_t *b, int cx, int cy, int r, uint16_t c, int f)
{
    if (r <= 0) {
        return;
    }
    int r2 = r * r + r;
    /* The falloff is f*(1 - d/r2). Dividing by r2 on every pixel is expensive:
     * the reciprocal is computed once, in 1/4096, and after that it is a
     * multiplication. */
    int inv = (f << 12) / r2;

    for (int dy = -r; dy <= r; dy++) {
        int yy = cy + dy;
        if (yy < b->cy0 || yy >= b->cy1) {
            continue;
        }
        int span = 0;
        while ((span + 1) * (span + 1) + dy * dy <= r2) {
            span++;
        }
        uint16_t *row = &b->px[yy * b->w];
        int x0 = cx - span; if (x0 < b->cx0) x0 = b->cx0;
        int x1 = cx + span; if (x1 >= b->cx1) x1 = b->cx1 - 1;
        for (int xx = x0; xx <= x1; xx++) {
            int d = (xx - cx) * (xx - cx) + dy * dy;
            row[xx] = ak_mix(row[xx], c, f - ((d * inv) >> 12));
        }
    }
}

void ak_wave(ak_buf_t *b, int cx, int cy, int r, int thick, uint16_t c, int f)
{
    if (r <= 0 || thick <= 0) {
        return;
    }
    int outer = (r + thick) * (r + thick);
    int inner = r * r;

    for (int dy = -(r + thick); dy <= r + thick; dy++) {
        int yy = cy + dy;
        if (yy < b->cy0 || yy >= b->cy1) {
            continue;
        }
        uint16_t *row = &b->px[yy * b->w];
        int dy2 = dy * dy;
        for (int dx = -(r + thick); dx <= r + thick; dx++) {
            int xx = cx + dx;
            if (xx < b->cx0 || xx >= b->cx1) {
                continue;
            }
            int d = dx * dx + dy2;
            if (d < inner || d > outer) {
                continue;
            }
            row[xx] = ak_mix(row[xx], c, f);
        }
    }
}

/* --------------------------------------------------------------------------
 * Integer trigonometry
 * -------------------------------------------------------------------------- */

static const uint8_t ak_sin_q[65] = {
      0,   6,  13,  19,  25,  31,  37,  44,  50,  56,  62,  68,  74,  80,  86,
     92,  98, 103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162, 167,
    171, 176, 180, 185, 189, 193, 197, 201, 205, 208, 212, 215, 219, 222, 225,
    228, 231, 233, 236, 238, 240, 242, 244, 246, 247, 249, 250, 251, 252, 253,
    254, 254, 255, 255, 255
};

int ak_sin(int brad)
{
    brad &= 0xFF;
    if (brad <= 64)  return  ak_sin_q[brad];
    if (brad <= 128) return  ak_sin_q[128 - brad];
    if (brad <= 192) return -ak_sin_q[brad - 128];
    return -ak_sin_q[256 - brad];
}

int ak_cos(int brad)
{
    return ak_sin(brad + 64);
}

int ak_isqrt(int v)
{
    if (v <= 0) {
        return 0;
    }
    int r = v, prev;
    do {
        prev = r;
        r = (r + v / r) / 2;
    } while (r < prev);
    return prev;
}

/* --------------------------------------------------------------------------
 * Palette and ASCII sprites
 * -------------------------------------------------------------------------- */

bool ak_pal(char ch, uint16_t *out)
{
    uint32_t hex;

    switch (ch) {
    case 'k': hex = 0x05060C; break;    /* near-black outline */
    case 'K': hex = 0x101527; break;
    case 'x': hex = 0x232B41; break;
    case 'd': hex = 0x3D465F; break;
    case 'D': hex = 0x606B85; break;
    case 'g': hex = 0x99A3BC; break;
    case 'G': hex = 0xD5DCEB; break;
    case 'w': hex = 0xFFFFFF; break;
    case 'c': hex = 0x7BE9FF; break;
    case 'C': hex = 0x18A6D8; break;
    case 'b': hex = 0x4A9DF5; break;
    case 'B': hex = 0x1F4FBF; break;
    case 'p': hex = 0xB072F0; break;
    case 'P': hex = 0x6A2FB5; break;
    case 'm': hex = 0xFF6FAE; break;
    case 'M': hex = 0xC0246A; break;
    case 'r': hex = 0xFF4A3D; break;
    case 'R': hex = 0xA31E1A; break;
    case 'o': hex = 0xFF9F0A; break;
    case 'O': hex = 0xC05A00; break;
    case 'y': hex = 0xFFE45E; break;
    case 'Y': hex = 0xE0A800; break;
    case 'v': hex = 0x4ADE80; break;
    case 'V': hex = 0x1E7A3C; break;
    case 'n': hex = 0x2AF0C8; break;
    case 'N': hex = 0x0E8A78; break;
    case 't': hex = 0xD97757; break;    /* Claude's orange */
    case 'T': hex = 0x8E4630; break;
    default:  return false;             /* '.' and any other */
    }

    *out = ak_rgb(hex);
    return true;
}

int ak_sprite_w(const char *const *rows)
{
    return (int)strlen(rows[0]);
}

void ak_blit(ak_buf_t *b, int x, int y, const char *const *rows, int nrows)
{
    for (int ry = 0; ry < nrows; ry++) {
        const char *row = rows[ry];
        for (int rx = 0; row[rx]; rx++) {
            uint16_t c;
            if (ak_pal(row[rx], &c)) {
                ak_px(b, x + rx, y + ry, c);
            }
        }
    }
}

void ak_blit_c(ak_buf_t *b, int cx, int cy, const char *const *rows, int nrows)
{
    ak_blit(b, cx - ak_sprite_w(rows) / 2, cy - nrows / 2, rows, nrows);
}

/* --------------------------------------------------------------------------
 * 5x7 font
 *
 * One column per byte, bit 0 at the top. Upper case, digits and a few signs:
 * what is used on an arcade scoreboard.
 * -------------------------------------------------------------------------- */

#define AK_FONT_FIRST   32
#define AK_FONT_LAST    95

static const uint8_t ak_font5x7[AK_FONT_LAST - AK_FONT_FIRST + 1][AK_CH_W] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* ' ' */
    { 0x00, 0x00, 0x5F, 0x00, 0x00 },   /* '!' */
    { 0x00, 0x07, 0x00, 0x07, 0x00 },   /* '"' */
    { 0x14, 0x7F, 0x14, 0x7F, 0x14 },   /* '#' */
    { 0x24, 0x2A, 0x7F, 0x2A, 0x12 },   /* '$' */
    { 0x23, 0x13, 0x08, 0x64, 0x62 },   /* '%' */
    { 0x36, 0x49, 0x55, 0x22, 0x50 },   /* '&' */
    { 0x00, 0x00, 0x07, 0x00, 0x00 },   /* apostrophe */
    { 0x00, 0x1C, 0x22, 0x41, 0x00 },   /* '(' */
    { 0x00, 0x41, 0x22, 0x1C, 0x00 },   /* ')' */
    { 0x14, 0x08, 0x3E, 0x08, 0x14 },   /* '*' */
    { 0x08, 0x08, 0x3E, 0x08, 0x08 },   /* '+' */
    { 0x00, 0x50, 0x30, 0x00, 0x00 },   /* ',' */
    { 0x08, 0x08, 0x08, 0x08, 0x08 },   /* '-' */
    { 0x00, 0x60, 0x60, 0x00, 0x00 },   /* '.' */
    { 0x20, 0x10, 0x08, 0x04, 0x02 },   /* '/' */
    { 0x3E, 0x51, 0x49, 0x45, 0x3E },   /* '0' */
    { 0x00, 0x42, 0x7F, 0x40, 0x00 },   /* '1' */
    { 0x42, 0x61, 0x51, 0x49, 0x46 },   /* '2' */
    { 0x21, 0x41, 0x45, 0x4B, 0x31 },   /* '3' */
    { 0x18, 0x14, 0x12, 0x7F, 0x10 },   /* '4' */
    { 0x27, 0x45, 0x45, 0x45, 0x39 },   /* '5' */
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 },   /* '6' */
    { 0x01, 0x71, 0x09, 0x05, 0x03 },   /* '7' */
    { 0x36, 0x49, 0x49, 0x49, 0x36 },   /* '8' */
    { 0x06, 0x49, 0x49, 0x29, 0x1E },   /* '9' */
    { 0x00, 0x36, 0x36, 0x00, 0x00 },   /* ':' */
    { 0x00, 0x56, 0x36, 0x00, 0x00 },   /* ';' */
    { 0x08, 0x14, 0x22, 0x41, 0x00 },   /* '<' */
    { 0x14, 0x14, 0x14, 0x14, 0x14 },   /* '=' */
    { 0x00, 0x41, 0x22, 0x14, 0x08 },   /* '>' */
    { 0x02, 0x01, 0x51, 0x09, 0x06 },   /* '?' */
    { 0x32, 0x49, 0x79, 0x41, 0x3E },   /* '@' */
    { 0x7E, 0x11, 0x11, 0x11, 0x7E },   /* 'A' */
    { 0x7F, 0x49, 0x49, 0x49, 0x36 },   /* 'B' */
    { 0x3E, 0x41, 0x41, 0x41, 0x22 },   /* 'C' */
    { 0x7F, 0x41, 0x41, 0x22, 0x1C },   /* 'D' */
    { 0x7F, 0x49, 0x49, 0x49, 0x41 },   /* 'E' */
    { 0x7F, 0x09, 0x09, 0x09, 0x01 },   /* 'F' */
    { 0x3E, 0x41, 0x49, 0x49, 0x7A },   /* 'G' */
    { 0x7F, 0x08, 0x08, 0x08, 0x7F },   /* 'H' */
    { 0x00, 0x41, 0x7F, 0x41, 0x00 },   /* 'I' */
    { 0x20, 0x40, 0x41, 0x3F, 0x01 },   /* 'J' */
    { 0x7F, 0x08, 0x14, 0x22, 0x41 },   /* 'K' */
    { 0x7F, 0x40, 0x40, 0x40, 0x40 },   /* 'L' */
    { 0x7F, 0x02, 0x0C, 0x02, 0x7F },   /* 'M' */
    { 0x7F, 0x04, 0x08, 0x10, 0x7F },   /* 'N' */
    { 0x3E, 0x41, 0x41, 0x41, 0x3E },   /* 'O' */
    { 0x7F, 0x09, 0x09, 0x09, 0x06 },   /* 'P' */
    { 0x3E, 0x41, 0x51, 0x21, 0x5E },   /* 'Q' */
    { 0x7F, 0x09, 0x19, 0x29, 0x46 },   /* 'R' */
    { 0x46, 0x49, 0x49, 0x49, 0x31 },   /* 'S' */
    { 0x01, 0x01, 0x7F, 0x01, 0x01 },   /* 'T' */
    { 0x3F, 0x40, 0x40, 0x40, 0x3F },   /* 'U' */
    { 0x1F, 0x20, 0x40, 0x20, 0x1F },   /* 'V' */
    { 0x7F, 0x20, 0x18, 0x20, 0x7F },   /* 'W' */
    { 0x63, 0x14, 0x08, 0x14, 0x63 },   /* 'X' */
    { 0x03, 0x04, 0x78, 0x04, 0x03 },   /* 'Y' */
    { 0x61, 0x51, 0x49, 0x45, 0x43 },   /* 'Z' */
    { 0x00, 0x7F, 0x41, 0x41, 0x00 },   /* '[' */
    { 0x02, 0x04, 0x08, 0x10, 0x20 },   /* backslash */
    { 0x00, 0x41, 0x41, 0x7F, 0x00 },   /* ']' */
    { 0x04, 0x02, 0x01, 0x02, 0x04 },   /* '^' */
    { 0x40, 0x40, 0x40, 0x40, 0x40 },   /* '_' */
};

int ak_text_w(const char *s)
{
    int n = (int)strlen(s);
    return n > 0 ? n * AK_CH_ADV - 1 : 0;
}

void ak_text(ak_buf_t *b, int x, int y, const char *s, uint16_t c)
{
    for (; *s; s++, x += AK_CH_ADV) {
        uint8_t ch = (uint8_t)*s;
        if (ch >= 'a' && ch <= 'z') {
            ch = (uint8_t)(ch - 32);        /* the font is upper case only */
        }
        if (ch < AK_FONT_FIRST || ch > AK_FONT_LAST) {
            continue;
        }
        const uint8_t *cols = ak_font5x7[ch - AK_FONT_FIRST];
        for (int cx = 0; cx < AK_CH_W; cx++) {
            uint8_t bits = cols[cx];
            for (int cy = 0; cy < AK_CH_H; cy++) {
                if (bits & (1u << cy)) {
                    ak_px(b, x + cx, y + cy, c);
                }
            }
        }
    }
}

void ak_text_sh(ak_buf_t *b, int x, int y, const char *s, uint16_t c, uint16_t sh)
{
    ak_text(b, x + 1, y + 1, s, sh);
    ak_text(b, x, y, s, c);
}

void ak_text_center(ak_buf_t *b, int cx, int y, const char *s, uint16_t c, uint16_t sh)
{
    ak_text_sh(b, cx - ak_text_w(s) / 2, y, s, c, sh);
}

char *ak_num(char *dst, uint32_t v, int min_digits)
{
    char tmp[12];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < (int)sizeof(tmp));
    while (n < min_digits && n < (int)sizeof(tmp)) {
        tmp[n++] = '0';
    }
    for (int i = 0; i < n; i++) {
        dst[i] = tmp[n - 1 - i];
    }
    dst[n] = '\0';
    return dst + n;
}
