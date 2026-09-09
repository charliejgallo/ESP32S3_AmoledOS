/*
 * Claudito - pixel art engine (see cl_pixel.h)
 */
#include "cl_pixel.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Palette
 *
 * One character per colour. The '.' and the space paint nothing, which is what
 * lets the sprites be written as ASCII art and overlaid without alpha.
 * -------------------------------------------------------------------------- */
bool cl_pal(char ch, uint16_t *out)
{
    uint32_t hex;

    switch (ch) {
    case 'k': hex = 0x1B1210; break;    /* near-black outline         */
    case 'e': hex = 0x2C2018; break;    /* eyes                       */
    case 'K': hex = 0x3A2A22; break;    /* dark brown                 */
    case 'w': hex = 0xFFFFFF; break;
    case 'W': hex = 0xD9D2C6; break;    /* off-white                  */
    case 'l': hex = 0xF7F7FA; break;
    case 'd': hex = 0x4A4A4E; break;
    case 'D': hex = 0x8A8A90; break;
    case 'r': hex = 0xE5484D; break;
    case 'R': hex = 0xA32A2E; break;
    case 'o': hex = 0xFF9F0A; break;
    case 'O': hex = 0xC96A00; break;
    case 'y': hex = 0xFFD60A; break;
    case 'Y': hex = 0xC9A400; break;
    case 'g': hex = 0x45C463; break;
    case 'G': hex = 0x21823E; break;
    case 'b': hex = 0x4A9DF5; break;
    case 'B': hex = 0x1F5FBF; break;
    case 'c': hex = 0x67DCEA; break;
    case 'p': hex = 0xB072F0; break;
    case 'P': hex = 0x7838C0; break;
    case 'm': hex = 0xFF6FAE; break;
    case 'M': hex = 0xC93C79; break;
    case 'n': hex = 0xA9713F; break;    /* wood                       */
    case 'N': hex = 0x6E4423; break;
    case 't': hex = 0xD97757; break;    /* Claude's orange            */
    case 'T': hex = 0xB0523A; break;
    case 'h': hex = 0xEC9A7C; break;    /* skin highlight             */
    case 's': hex = 0xF3E3C8; break;    /* cream                      */
    case 'S': hex = 0xD8C3A0; break;
    default:  return false;             /* '.' and anything else      */
    }

    *out = cl_rgb(hex);
    return true;
}

/* --------------------------------------------------------------------------
 * Primitives
 * -------------------------------------------------------------------------- */

void cl_px(cl_buf_t *b, int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= b->w || y >= b->h) {
        return;
    }
    b->px[y * b->w + x] = c;
}

void cl_fill(cl_buf_t *b, uint16_t c)
{
    int total = b->w * b->h;
    for (int i = 0; i < total; i++) {
        b->px[i] = c;
    }
}

void cl_copy(cl_buf_t *dst, const cl_buf_t *src)
{
    if (dst->w != src->w || dst->h != src->h) {
        return;
    }
    memcpy(dst->px, src->px, (size_t)dst->w * (size_t)dst->h * sizeof(uint16_t));
}

void cl_rect(cl_buf_t *b, int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > b->w) x1 = b->w;
    int y1 = y + h; if (y1 > b->h) y1 = b->h;

    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = &b->px[yy * b->w];
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = c;
        }
    }
}

void cl_hline(cl_buf_t *b, int x, int y, int len, uint16_t c)
{
    cl_rect(b, x, y, len, 1, c);
}

void cl_vline(cl_buf_t *b, int x, int y, int len, uint16_t c)
{
    cl_rect(b, x, y, 1, len, c);
}

void cl_frame(cl_buf_t *b, int x, int y, int w, int h, uint16_t c)
{
    cl_hline(b, x, y, w, c);
    cl_hline(b, x, y + h - 1, w, c);
    cl_vline(b, x, y, h, c);
    cl_vline(b, x + w - 1, y, h, c);
}

void cl_disc(cl_buf_t *b, int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            /* the +r/2 rounds the silhouette: without it a small circle comes
             * out as a diamond */
            if (dx * dx + dy * dy <= r * r + r / 2) {
                cl_px(b, cx + dx, cy + dy, c);
            }
        }
    }
}

void cl_ring(cl_buf_t *b, int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d = dx * dx + dy * dy;
            if (d <= r * r + r / 2 && d > (r - 1) * (r - 1)) {
                cl_px(b, cx + dx, cy + dy, c);
            }
        }
    }
}

void cl_round(cl_buf_t *b, int x, int y, int w, int h, int cut, uint16_t c)
{
    for (int yy = 0; yy < h; yy++) {
        int inset = 0;
        if (yy < cut) {
            inset = cut - yy;
        } else if (yy >= h - cut) {
            inset = cut - (h - 1 - yy);
        }
        cl_hline(b, x + inset, y + yy, w - 2 * inset, c);
    }
}

void cl_shade(cl_buf_t *b, int x, int y, int w, int h, int f)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > b->w) x1 = b->w;
    int y1 = y + h; if (y1 > b->h) y1 = b->h;

    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = &b->px[yy * b->w];
        for (int xx = x0; xx < x1; xx++) {
            int r = (row[xx] >> 11) & 0x1F;
            int g = (row[xx] >> 5)  & 0x3F;
            int bl =  row[xx]       & 0x1F;

            if (f < 0) {
                r  = r  * (16 + f) / 16;
                g  = g  * (16 + f) / 16;
                bl = bl * (16 + f) / 16;
            } else {
                r  += (31 - r)  * f / 16;
                g  += (63 - g)  * f / 16;
                bl += (31 - bl) * f / 16;
            }
            row[xx] = (uint16_t)((r << 11) | (g << 5) | bl);
        }
    }
}

/* --------------------------------------------------------------------------
 * Sprites
 * -------------------------------------------------------------------------- */

void cl_blit(cl_buf_t *b, int x, int y, const char *const *rows, int nrows, bool flip)
{
    for (int ry = 0; ry < nrows; ry++) {
        const char *row = rows[ry];
        int len = (int)strlen(row);
        for (int rx = 0; rx < len; rx++) {
            uint16_t c;
            if (!cl_pal(row[rx], &c)) {
                continue;
            }
            cl_px(b, x + (flip ? len - 1 - rx : rx), y + ry, c);
        }
    }
}

void cl_blit_solid(cl_buf_t *b, int x, int y, const char *const *rows, int nrows,
                   bool flip, uint16_t c)
{
    for (int ry = 0; ry < nrows; ry++) {
        const char *row = rows[ry];
        int len = (int)strlen(row);
        for (int rx = 0; rx < len; rx++) {
            uint16_t ignored;
            if (!cl_pal(row[rx], &ignored)) {
                continue;
            }
            cl_px(b, x + (flip ? len - 1 - rx : rx), y + ry, c);
        }
    }
}

/* --------------------------------------------------------------------------
 * Text
 *
 * The table is generated by apps/claudito/tools/mkfont.py from ASCII art:
 * writing the letters by hand in hexadecimal is where the mistakes creep in.
 * -------------------------------------------------------------------------- */
/* GENERATED from the app's tools/mkfont.py - 5x7 font, bit n = row n */
static const uint8_t CL_FONT_FIRST = 32;
static const uint8_t CL_FONT_LAST  = 126;
static const uint8_t cl_font5x7[][5] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* ' ' */
    { 0x00, 0x00, 0x5F, 0x00, 0x00 },   /* '!' */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 34 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 35 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 36 */
    { 0x23, 0x13, 0x68, 0x66, 0x01 },   /* '%' */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 38 */
    { 0x00, 0x00, 0x03, 0x00, 0x00 },   /* 'apostrophe' */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 40 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 41 */
    { 0x2A, 0x1C, 0x3E, 0x1C, 0x2A },   /* '*' */
    { 0x00, 0x08, 0x3E, 0x08, 0x00 },   /* '+' */
    { 0x00, 0x40, 0x20, 0x00, 0x00 },   /* ',' */
    { 0x00, 0x08, 0x08, 0x08, 0x00 },   /* '-' */
    { 0x00, 0x00, 0x40, 0x00, 0x00 },   /* '.' */
    { 0x40, 0x30, 0x08, 0x06, 0x01 },   /* '/' */
    { 0x3E, 0x51, 0x49, 0x45, 0x3E },   /* '0' */
    { 0x00, 0x42, 0x7F, 0x40, 0x00 },   /* '1' */
    { 0x42, 0x61, 0x51, 0x49, 0x46 },   /* '2' */
    { 0x41, 0x49, 0x49, 0x49, 0x36 },   /* '3' */
    { 0x18, 0x14, 0x12, 0x7F, 0x10 },   /* '4' */
    { 0x27, 0x45, 0x45, 0x45, 0x39 },   /* '5' */
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 },   /* '6' */
    { 0x01, 0x71, 0x09, 0x05, 0x03 },   /* '7' */
    { 0x36, 0x49, 0x49, 0x49, 0x36 },   /* '8' */
    { 0x06, 0x49, 0x49, 0x29, 0x1E },   /* '9' */
    { 0x00, 0x00, 0x36, 0x00, 0x00 },   /* ':' */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 59 */
    { 0x08, 0x14, 0x22, 0x41, 0x00 },   /* '<' */
    { 0x14, 0x14, 0x14, 0x14, 0x14 },   /* '=' */
    { 0x00, 0x41, 0x22, 0x14, 0x08 },   /* '>' */
    { 0x02, 0x01, 0x51, 0x09, 0x06 },   /* '?' */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 64 */
    { 0x7E, 0x09, 0x09, 0x09, 0x7E },   /* 'A' */
    { 0x7F, 0x49, 0x49, 0x49, 0x36 },   /* 'B' */
    { 0x3E, 0x41, 0x41, 0x41, 0x41 },   /* 'C' */
    { 0x7F, 0x41, 0x41, 0x41, 0x3E },   /* 'D' */
    { 0x7F, 0x49, 0x49, 0x49, 0x41 },   /* 'E' */
    { 0x7F, 0x09, 0x09, 0x09, 0x01 },   /* 'F' */
    { 0x3E, 0x41, 0x41, 0x49, 0x3A },   /* 'G' */
    { 0x7F, 0x08, 0x08, 0x08, 0x7F },   /* 'H' */
    { 0x00, 0x41, 0x7F, 0x41, 0x00 },   /* 'I' */
    { 0x20, 0x40, 0x41, 0x3F, 0x01 },   /* 'J' */
    { 0x7F, 0x08, 0x14, 0x22, 0x41 },   /* 'K' */
    { 0x7F, 0x40, 0x40, 0x40, 0x40 },   /* 'L' */
    { 0x7F, 0x02, 0x0C, 0x02, 0x7F },   /* 'M' */
    { 0x7F, 0x02, 0x0C, 0x10, 0x7F },   /* 'N' */
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
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 91 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 92 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 93 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 94 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 95 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 96 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 97 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 98 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 99 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 100 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 101 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 102 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 103 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 104 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 105 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 106 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 107 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 108 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 109 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 110 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 111 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 112 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 113 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 114 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 115 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 116 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 117 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 118 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 119 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 120 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 121 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 122 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 123 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 124 */
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 125 */
    { 0x7E, 0x09, 0x11, 0x22, 0x7D },   /* '~' */
};

int cl_text_w(const char *s)
{
    int n = (int)strlen(s);
    return n > 0 ? n * CL_CH_ADV - 1 : 0;
}

void cl_text(cl_buf_t *b, int x, int y, const char *s, uint16_t c)
{
    for (; *s; s++, x += CL_CH_ADV) {
        uint8_t ch = (uint8_t)*s;
        if (ch < CL_FONT_FIRST || ch > CL_FONT_LAST) {
            continue;
        }
        const uint8_t *cols = cl_font5x7[ch - CL_FONT_FIRST];
        for (int cx = 0; cx < CL_CH_W; cx++) {
            uint8_t bits = cols[cx];
            for (int cy = 0; cy < CL_CH_H; cy++) {
                if (bits & (1u << cy)) {
                    cl_px(b, x + cx, y + cy, c);
                }
            }
        }
    }
}

void cl_text_sh(cl_buf_t *b, int x, int y, const char *s, uint16_t c, uint16_t sh)
{
    cl_text(b, x + 1, y + 1, s, sh);
    cl_text(b, x, y, s, c);
}

void cl_text_center(cl_buf_t *b, int cx, int y, const char *s, uint16_t c, uint16_t sh)
{
    cl_text_sh(b, cx - cl_text_w(s) / 2, y, s, c, sh);
}
