/*
 * 2043 - pixel art engine (see gx_pixel.h)
 */
#include "gx_pixel.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Palette
 *
 * One character per colour. The '.' and the space paint nothing, which is what
 * lets the sprites be written as ASCII art and overlaid without alpha. Upper
 * case = the dark version of the same colour, except in the greys.
 * -------------------------------------------------------------------------- */
bool gx_pal(char ch, uint16_t *out)
{
    uint32_t hex;

    switch (ch) {
    case 'k': hex = 0x090B14; break;    /* near-black outline    */
    case 'K': hex = 0x161B2B; break;    /* night blue            */
    case 'x': hex = 0x2A3145; break;    /* metal in shadow       */
    case 'd': hex = 0x3D465F; break;
    case 'D': hex = 0x606B85; break;
    case 'g': hex = 0x99A3BC; break;
    case 'G': hex = 0xD5DCEB; break;
    case 'w': hex = 0xFFFFFF; break;
    case 'c': hex = 0x7BE9FF; break;    /* cockpit               */
    case 'C': hex = 0x18A6D8; break;
    case 'z': hex = 0x0B5E86; break;
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
    case 'n': hex = 0x2AF0C8; break;    /* neon turquoise        */
    case 'N': hex = 0x0E8A78; break;
    case 's': hex = 0xE8D8B0; break;    /* sand                  */
    case 'S': hex = 0xA88C55; break;
    case 't': hex = 0xD97757; break;    /* Claude's orange       */
    case 'T': hex = 0x8E4630; break;
    case 'e': hex = 0xFF2D55; break;    /* engine red            */
    case 'i': hex = 0xBFE9FF; break;    /* ice                   */
    default:  return false;             /* '.' and any other     */
    }

    *out = gx_rgb(hex);
    return true;
}

uint16_t gx_pal_or(char ch, uint16_t fallback)
{
    uint16_t c;
    return gx_pal(ch, &c) ? c : fallback;
}

uint16_t gx_mix(uint16_t a, uint16_t b, int f)
{
    if (f <= 0) return a;
    if (f >= 16) return b;

    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;

    int r = ar + (br - ar) * f / 16;
    int g = ag + (bg - ag) * f / 16;
    int bl = ab + (bb - ab) * f / 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* --------------------------------------------------------------------------
 * Integer trigonometry
 *
 * A quarter turn tabulated (64 entries, 0..90 degrees) in 1/256. The rest
 * comes out by symmetry. It is more than enough for boss orbits and shot
 * patterns, and it does not drag in libm.
 * -------------------------------------------------------------------------- */
static const uint8_t gx_sin_q[65] = {
      0,   6,  13,  19,  25,  31,  37,  44,  50,  56,  62,  68,  74,  80,  86,
     92,  98, 103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162, 167,
    171, 176, 180, 185, 189, 193, 197, 201, 205, 208, 212, 215, 219, 222, 225,
    228, 231, 233, 236, 238, 240, 242, 244, 246, 247, 249, 250, 251, 252, 253,
    254, 254, 255, 255, 255
};

int gx_sin(int brad)
{
    brad &= 0xFF;
    if (brad <= 64)  return  gx_sin_q[brad];
    if (brad <= 128) return  gx_sin_q[128 - brad];
    if (brad <= 192) return -gx_sin_q[brad - 128];
    return -gx_sin_q[256 - brad];
}

int gx_cos(int brad)
{
    return gx_sin(brad + 64);
}

int gx_isqrt(int v)
{
    if (v <= 0) {
        return 0;
    }
    int r = v, prev;
    /* Integer Newton. It stops when it stops decreasing: comparing for
     * equality it could end up oscillating between two neighbouring values. */
    do {
        prev = r;
        r = (r + v / r) / 2;
    } while (r < prev);
    return prev;
}

/* Arctangent by a table of the first octant: atan(i/32) in brads. With the
 * quadrant and the axes' symmetry the full angle comes out, exact to the brad
 * and without touching libm. */
static const uint8_t gx_atan_q[33] = {
      0,   1,   3,   4,   5,   6,   8,   9,  10,  11,  12,  13,  15,  16,  17,
     18,  19,  20,  21,  22,  23,  24,  25,  25,  26,  27,  28,  29,  29,  30,
     31,  31,  32
};

int gx_atan2(int y, int x)
{
    int ax = x < 0 ? -x : x;
    int ay = y < 0 ? -y : y;
    if (ax == 0 && ay == 0) {
        return 0;
    }

    int base;
    if (ax >= ay) {
        base = gx_atan_q[ay * 32 / ax];
    } else {
        base = 64 - gx_atan_q[ax * 32 / ay];
    }

    if (x >= 0) {
        return y >= 0 ? base : (256 - base) & 0xFF;
    }
    return y >= 0 ? 128 - base : 128 + base;
}

/* --------------------------------------------------------------------------
 * Primitives
 * -------------------------------------------------------------------------- */

void gx_px(gx_buf_t *b, int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= b->w || y >= b->h) {
        return;
    }
    b->px[y * b->w + x] = c;
}

void gx_fill(gx_buf_t *b, uint16_t c)
{
    int total = b->w * b->h;
    uint16_t *p = b->px;
    for (int i = 0; i < total; i++) {
        p[i] = c;
    }
}

void gx_rect(gx_buf_t *b, int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > b->w) x1 = b->w;
    int y1 = y + h; if (y1 > b->h) y1 = b->h;

    /* Two pixels per write. It sounds like a silly micro-optimisation until
     * you count: the sky's gradient alone is 41 thousand writes per frame, and
     * on the board the buffer lives in PSRAM. Since the buffer's width is even,
     * the alignment depends on nothing but the column's parity. */
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

void gx_hline(gx_buf_t *b, int x, int y, int len, uint16_t c)
{
    gx_rect(b, x, y, len, 1, c);
}

void gx_vline(gx_buf_t *b, int x, int y, int len, uint16_t c)
{
    gx_rect(b, x, y, 1, len, c);
}

void gx_frame(gx_buf_t *b, int x, int y, int w, int h, uint16_t c)
{
    gx_hline(b, x, y, w, c);
    gx_hline(b, x, y + h - 1, w, c);
    gx_vline(b, x, y, h, c);
    gx_vline(b, x + w - 1, y, h, c);
}

void gx_line(gx_buf_t *b, int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int steps = adx > ady ? adx : ady;

    if (steps == 0) {
        gx_px(b, x0, y0, c);
        return;
    }
    for (int i = 0; i <= steps; i++) {
        gx_px(b, x0 + dx * i / steps, y0 + dy * i / steps, c);
    }
}

void gx_disc(gx_buf_t *b, int cx, int cy, int r, uint16_t c)
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
        gx_hline(b, cx - span, cy + dy, span * 2 + 1, c);
    }
}

void gx_ring(gx_buf_t *b, int cx, int cy, int r, uint16_t c)
{
    if (r <= 0) {
        gx_px(b, cx, cy, c);
        return;
    }
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gx_px(b, cx + x, cy + y, c); gx_px(b, cx + y, cy + x, c);
        gx_px(b, cx - y, cy + x, c); gx_px(b, cx - x, cy + y, c);
        gx_px(b, cx - x, cy - y, c); gx_px(b, cx - y, cy - x, c);
        gx_px(b, cx + y, cy - x, c); gx_px(b, cx + x, cy - y, c);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gx_round(gx_buf_t *b, int x, int y, int w, int h, int cut, uint16_t c)
{
    for (int yy = 0; yy < h; yy++) {
        int inset = 0;
        if (yy < cut) {
            inset = cut - yy;
        } else if (yy >= h - cut) {
            inset = cut - (h - 1 - yy);
        }
        gx_hline(b, x + inset, y + yy, w - inset * 2, c);
    }
}

void gx_vgrad(gx_buf_t *b, int y0, int y1, uint16_t top, uint16_t bot)
{
    if (y1 < y0) {
        return;
    }
    int span = y1 - y0;
    for (int y = y0; y <= y1; y++) {
        int f = span > 0 ? (y - y0) * 16 / span : 0;
        gx_hline(b, 0, y, b->w, gx_mix(top, bot, f));
    }
}

void gx_shade(gx_buf_t *b, int x, int y, int w, int h, int f)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > b->w) x1 = b->w;
    int y1 = y + h; if (y1 > b->h) y1 = b->h;
    uint16_t target = f < 0 ? 0x0000 : 0xFFFF;
    int amount = f < 0 ? -f : f;

    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = &b->px[yy * b->w];
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = gx_mix(row[xx], target, amount);
        }
    }
}

void gx_glow(gx_buf_t *b, int cx, int cy, int r, uint16_t c, int f)
{
    if (r < 0) {
        return;
    }
    int r2 = r * r + r;
    if (r2 <= 0) {
        return;
    }
    /* The falloff is f*(1 - d/r2). Dividing by r2 on every pixel is expensive:
     * a shot's halo is fifty pixels and there are dozens of shots. The
     * reciprocal is computed once, in 1/4096, and after that it is a
     * multiplication. */
    int inv = (f << 12) / r2;

    for (int dy = -r; dy <= r; dy++) {
        int yy = cy + dy;
        if (yy < 0 || yy >= b->h) {
            continue;
        }
        int span = 0;
        while ((span + 1) * (span + 1) + dy * dy <= r2) {
            span++;
        }
        uint16_t *row = &b->px[yy * b->w];
        int x0 = cx - span; if (x0 < 0) x0 = 0;
        int x1 = cx + span; if (x1 >= b->w) x1 = b->w - 1;
        for (int xx = x0; xx <= x1; xx++) {
            /* stronger in the centre: the halo fades towards the edge */
            int d = (xx - cx) * (xx - cx) + dy * dy;
            row[xx] = gx_mix(row[xx], c, f - ((d * inv) >> 12));
        }
    }
}

/* --------------------------------------------------------------------------
 * ASCII sprites
 * -------------------------------------------------------------------------- */

int gx_sprite_w(const char *const *rows)
{
    return (int)strlen(rows[0]);
}

void gx_blit(gx_buf_t *b, int x, int y, const char *const *rows, int nrows)
{
    for (int ry = 0; ry < nrows; ry++) {
        const char *row = rows[ry];
        for (int rx = 0; row[rx]; rx++) {
            uint16_t c;
            if (gx_pal(row[rx], &c)) {
                gx_px(b, x + rx, y + ry, c);
            }
        }
    }
}

void gx_blit_solid(gx_buf_t *b, int x, int y, const char *const *rows, int nrows,
                   uint16_t c)
{
    for (int ry = 0; ry < nrows; ry++) {
        const char *row = rows[ry];
        for (int rx = 0; row[rx]; rx++) {
            uint16_t ignored;
            if (gx_pal(row[rx], &ignored)) {
                gx_px(b, x + rx, y + ry, c);
            }
        }
    }
}

void gx_blit_c(gx_buf_t *b, int cx, int cy, const char *const *rows, int nrows)
{
    gx_blit(b, cx - gx_sprite_w(rows) / 2, cy - nrows / 2, rows, nrows);
}

void gx_blit_c_solid(gx_buf_t *b, int cx, int cy, const char *const *rows,
                     int nrows, uint16_t c)
{
    gx_blit_solid(b, cx - gx_sprite_w(rows) / 2, cy - nrows / 2, rows, nrows, c);
}

void gx_blit_c_xscale(gx_buf_t *b, int cx, int cy, const char *const *rows,
                      int nrows, int dst_w)
{
    if (dst_w <= 0) {
        return;
    }
    int src_w = gx_sprite_w(rows);
    int x = cx - dst_w / 2;
    int y = cy - nrows / 2;

    for (int dx = 0; dx < dst_w; dx++) {
        int sx = dx * src_w / dst_w;
        for (int ry = 0; ry < nrows; ry++) {
            uint16_t col;
            if (gx_pal(rows[ry][sx], &col)) {
                gx_px(b, x + dx, y + ry, col);
            }
        }
    }
}

void gx_blit_c_flipv(gx_buf_t *b, int cx, int cy, const char *const *rows, int nrows)
{
    int x = cx - gx_sprite_w(rows) / 2;
    int y = cy - nrows / 2;
    for (int ry = 0; ry < nrows; ry++) {
        const char *row = rows[nrows - 1 - ry];
        for (int rx = 0; row[rx]; rx++) {
            uint16_t c;
            if (gx_pal(row[rx], &c)) {
                gx_px(b, x + rx, y + ry, c);
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * 5x7 font
 *
 * One column per byte, bit 0 at the top. Upper case only, digits and a few
 * signs: it is what is used on an arcade scoreboard.
 * -------------------------------------------------------------------------- */

#define GX_FONT_FIRST   32
#define GX_FONT_LAST    95

static const uint8_t gx_font5x7[GX_FONT_LAST - GX_FONT_FIRST + 1][GX_CH_W] = {
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

int gx_text_w(const char *s)
{
    int n = (int)strlen(s);
    return n > 0 ? n * GX_CH_ADV - 1 : 0;
}

void gx_text(gx_buf_t *b, int x, int y, const char *s, uint16_t c)
{
    for (; *s; s++, x += GX_CH_ADV) {
        uint8_t ch = (uint8_t)*s;
        if (ch >= 'a' && ch <= 'z') {
            ch = (uint8_t)(ch - 32);        /* the font is upper case only */
        }
        if (ch < GX_FONT_FIRST || ch > GX_FONT_LAST) {
            continue;
        }
        const uint8_t *cols = gx_font5x7[ch - GX_FONT_FIRST];
        for (int cx = 0; cx < GX_CH_W; cx++) {
            uint8_t bits = cols[cx];
            for (int cy = 0; cy < GX_CH_H; cy++) {
                if (bits & (1u << cy)) {
                    gx_px(b, x + cx, y + cy, c);
                }
            }
        }
    }
}

void gx_text_sh(gx_buf_t *b, int x, int y, const char *s, uint16_t c, uint16_t sh)
{
    gx_text(b, x + 1, y + 1, s, sh);
    gx_text(b, x, y, s, c);
}

void gx_text_center(gx_buf_t *b, int cx, int y, const char *s, uint16_t c, uint16_t sh)
{
    gx_text_sh(b, cx - gx_text_w(s) / 2, y, s, c, sh);
}
