/*
 * PIXEL ART - the document and its file format. See px_file.h.
 */
#include "px_file.h"

#include <stdio.h>
#include <string.h>

/* 32 colours. Index 0 is black -the AMOLED's "off"- and the rest go by
 * family so that the strip on the watch reads as a spectrum: greys, reds,
 * oranges, yellows, greens, cyans, blues, purples, pinks, and a run of browns
 * and skin tones for the things people actually draw. Chosen for the panel:
 * two neighbouring dark greys look identical on it (measured), so every
 * family has a light and a dark member, not two middles. */
const uint8_t px_palette[PX_COLORS][3] = {
    { 0x00, 0x00, 0x00 },   /*  0 black         */
    { 0xFF, 0xFF, 0xFF },   /*  1 white         */
    { 0xB0, 0xB0, 0xB8 },   /*  2 light grey    */
    { 0x5A, 0x5A, 0x64 },   /*  3 dark grey     */
    { 0xFF, 0x3B, 0x30 },   /*  4 red           */
    { 0xA8, 0x14, 0x1E },   /*  5 dark red      */
    { 0xFF, 0x8A, 0x1E },   /*  6 orange        */
    { 0xC8, 0x50, 0x00 },   /*  7 dark orange   */
    { 0xFF, 0xD6, 0x0A },   /*  8 yellow        */
    { 0xFF, 0xF4, 0x8C },   /*  9 light yellow  */
    { 0x30, 0xD1, 0x58 },   /* 10 green         */
    { 0x14, 0x7A, 0x32 },   /* 11 dark green    */
    { 0xA6, 0xF0, 0x5A },   /* 12 lime          */
    { 0x00, 0xC8, 0xBE },   /* 13 teal          */
    { 0x5A, 0xC8, 0xFA },   /* 14 sky           */
    { 0x0A, 0x84, 0xFF },   /* 15 blue          */
    { 0x10, 0x3C, 0xA0 },   /* 16 navy          */
    { 0xAF, 0x52, 0xDE },   /* 17 purple        */
    { 0x5E, 0x1E, 0x8C },   /* 18 dark purple   */
    { 0xFF, 0x2D, 0x95 },   /* 19 pink          */
    { 0xFF, 0xB3, 0xC8 },   /* 20 light pink    */
    { 0x8B, 0x46, 0x18 },   /* 21 brown         */
    { 0x50, 0x28, 0x0A },   /* 22 dark brown    */
    { 0xC8, 0x8A, 0x46 },   /* 23 tan           */
    { 0xE6, 0xB8, 0x8A },   /* 24 sand          */
    { 0xFF, 0xDC, 0xB4 },   /* 25 skin light    */
    { 0xD2, 0x96, 0x6E },   /* 26 skin medium   */
    { 0x8C, 0x5A, 0x3C },   /* 27 skin dark     */
    { 0xFF, 0xE4, 0xE1 },   /* 28 blush         */
    { 0xC8, 0xE6, 0xFF },   /* 29 ice           */
    { 0xD4, 0xA0, 0x17 },   /* 30 gold          */
    { 0x2A, 0x2A, 0x32 },   /* 31 near black    */
};

uint16_t px_rgb565(int idx)
{
    if (idx < 0 || idx >= PX_COLORS) {
        idx = 0;
    }
    const uint8_t *c = px_palette[idx];
    return (uint16_t)(((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3));
}

int px_nearest(uint8_t r, uint8_t g, uint8_t b)
{
    int best = 0;
    long best_d = 1L << 30;
    for (int i = 0; i < PX_COLORS; i++) {
        long dr = (long)r - px_palette[i][0];
        long dg = (long)g - px_palette[i][1];
        long db = (long)b - px_palette[i][2];
        /* Weighted like the eye: green counts most. */
        long d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

void px_doc_init(px_doc_t *d, int size)
{
    memset(d, 0, sizeof(*d));
    d->size     = (uint8_t)(size == 8 ? 8 : 16);
    d->frames   = 1;
    d->delay_ms = PX_DELAY_DEF;
}

/* --------------------------------------------------------------------------
 * File
 * -------------------------------------------------------------------------- */

#define HDR_LEN     12
static const char MAGIC[4] = { 'P', 'I', 'X', '1' };

static bool header_ok(const uint8_t *h, int *size, int *frames, int *ncolors,
                      int *delay)
{
    if (memcmp(h, MAGIC, 4) != 0) {
        return false;
    }
    int s = h[4], f = h[5], n = h[6] ? h[6] : 256;
    if ((s != 8 && s != 16) || f < 1 || f > PX_MAX_FRAMES) {
        return false;
    }
    *size = s;
    *frames = f;
    *ncolors = n;
    *delay = h[8] | (h[9] << 8);
    return true;
}

bool px_doc_peek(const char *path, int *size, int *frames)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    uint8_t h[HDR_LEN];
    bool ok = fread(h, 1, HDR_LEN, f) == HDR_LEN;
    fclose(f);
    int n, delay;
    return ok && header_ok(h, size, frames, &n, &delay);
}

bool px_doc_load(px_doc_t *d, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    uint8_t h[HDR_LEN];
    int size, frames, ncolors, delay;
    if (fread(h, 1, HDR_LEN, f) != HDR_LEN ||
        !header_ok(h, &size, &frames, &ncolors, &delay)) {
        fclose(f);
        return false;
    }

    /* The file's palette. When it is ours byte for byte, indexes pass through;
     * otherwise each entry is mapped to the nearest colour we have. A file
     * written by the portal is always the first case; one edited elsewhere
     * with its own colours is the second, and it still opens. */
    uint8_t map[256];
    bool same = (ncolors == PX_COLORS);
    for (int i = 0; i < ncolors; i++) {
        uint8_t rgb[3];
        if (fread(rgb, 1, 3, f) != 3) {
            fclose(f);
            return false;
        }
        int idx = px_nearest(rgb[0], rgb[1], rgb[2]);
        if (same && (idx != i || memcmp(rgb, px_palette[i], 3) != 0)) {
            same = false;
        }
        map[i] = (uint8_t)idx;
    }

    px_doc_init(d, size);
    d->frames = (uint8_t)frames;
    d->delay_ms = (uint16_t)(delay < PX_DELAY_MIN ? PX_DELAY_DEF :
                             delay > PX_DELAY_MAX ? PX_DELAY_MAX : delay);

    size_t cells = (size_t)size * size;
    for (int fr = 0; fr < frames; fr++) {
        if (fread(d->px[fr], 1, cells, f) != cells) {
            fclose(f);
            return false;
        }
        for (size_t i = 0; i < cells; i++) {
            uint8_t v = d->px[fr][i];
            d->px[fr][i] = (v < ncolors) ? (same ? v : map[v]) : 0;
        }
    }
    fclose(f);
    return true;
}

bool px_doc_save(const px_doc_t *d, const char *path)
{
    /* Written to a temporary and renamed: if the card is pulled or the
     * board resets half way, the previous file is still whole. */
    char tmp[200];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return false;
    }
    uint8_t h[HDR_LEN] = { 'P', 'I', 'X', '1',
                           d->size, d->frames, PX_COLORS, 0,
                           (uint8_t)(d->delay_ms & 0xFF),
                           (uint8_t)(d->delay_ms >> 8), 0, 0 };
    bool ok = fwrite(h, 1, HDR_LEN, f) == HDR_LEN &&
              fwrite(px_palette, 1, sizeof(px_palette), f) == sizeof(px_palette);
    size_t cells = (size_t)d->size * d->size;
    for (int fr = 0; ok && fr < d->frames; fr++) {
        ok = fwrite(d->px[fr], 1, cells, f) == cells;
    }
    if (fclose(f) != 0) {
        ok = false;
    }
    if (!ok) {
        remove(tmp);
        return false;
    }
    remove(path);       /* FAT's rename does not overwrite */
    return rename(tmp, path) == 0;
}

/* --------------------------------------------------------------------------
 * Frames
 * -------------------------------------------------------------------------- */

static int insert_after(px_doc_t *d, int at)
{
    if (d->frames >= PX_MAX_FRAMES) {
        return -1;
    }
    if (at < 0) at = 0;
    if (at >= d->frames) at = d->frames - 1;
    int pos = at + 1;
    memmove(d->px[pos + 1], d->px[pos], (size_t)(d->frames - pos) * PX_CELLS);
    d->frames++;
    return pos;
}

int px_doc_frame_dup(px_doc_t *d, int at)
{
    int pos = insert_after(d, at);
    if (pos > 0) {
        memcpy(d->px[pos], d->px[pos - 1], PX_CELLS);
    }
    return pos;
}

int px_doc_frame_blank(px_doc_t *d, int at)
{
    int pos = insert_after(d, at);
    if (pos > 0) {
        memset(d->px[pos], 0, PX_CELLS);
    }
    return pos;
}

bool px_doc_frame_delete(px_doc_t *d, int at)
{
    if (d->frames <= 1 || at < 0 || at >= d->frames) {
        return false;
    }
    memmove(d->px[at], d->px[at + 1], (size_t)(d->frames - at - 1) * PX_CELLS);
    d->frames--;
    memset(d->px[d->frames], 0, PX_CELLS);
    return true;
}

void px_doc_fill(px_doc_t *d, int frame, int x, int y, uint8_t color)
{
    int n = d->size;
    if (frame < 0 || frame >= d->frames || x < 0 || y < 0 || x >= n || y >= n) {
        return;
    }
    uint8_t *px = d->px[frame];
    uint8_t from = px[y * n + x];
    if (from == color) {
        return;
    }
    /* An explicit stack of cell indexes: a frame has at most 256 cells and
     * each one is pushed once, so 256 entries can never overflow. */
    uint8_t stack[PX_CELLS];
    int sp = 0;
    stack[sp++] = (uint8_t)(y * n + x);
    px[y * n + x] = color;
    while (sp > 0) {
        int i = stack[--sp];
        int cx = i % n, cy = i / n;
        const int dx[4] = { 1, -1, 0, 0 };
        const int dy[4] = { 0, 0, 1, -1 };
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], ny = cy + dy[k];
            if (nx < 0 || ny < 0 || nx >= n || ny >= n) {
                continue;
            }
            int j = ny * n + nx;
            if (px[j] == from) {
                px[j] = color;
                stack[sp++] = (uint8_t)j;
            }
        }
    }
}
