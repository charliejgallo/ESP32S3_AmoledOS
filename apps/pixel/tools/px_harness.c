/*
 * PIXEL ART - test bench, no LVGL, no ESP-IDF.
 *
 *     cc -O1 -Wall -Wextra -I../main tools/px_harness.c main/px_file.c main/px_export.c -o /tmp/px_harness
 *     /tmp/px_harness /tmp/px
 *
 * It builds a document, saves it, loads it back and compares; exercises the
 * frame operations and the flood fill; writes a PNG and a GIF, decodes them
 * with its own decoders (a stored-deflate PNG reader and an LZW GIF reader,
 * both written independently of the encoders) and compares every pixel.
 * Exit status is the number of failures.
 */
#include "px_file.h"
#include "px_export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n);
    if (fread(b, 1, *n, f) != *n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static uint32_t be32(const uint8_t *p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

/* PNG reader for exactly what we write: stored blocks, filter 0. */
static int check_png(const char *path, const px_doc_t *d, int frame, int scale)
{
    size_t n;
    uint8_t *b = slurp(path, &n);
    if (!b) { printf("FAIL cannot read %s\n", path); return 1; }
    int bad = 0;
    size_t p = 8;
    int w = 0;
    uint8_t *raw = NULL;
    size_t rawn = 0;
    uint8_t pal[256][3];
    while (p + 8 <= n) {
        uint32_t len = be32(b + p);
        const char *type = (const char *)b + p + 4;
        const uint8_t *data = b + p + 8;
        if (!memcmp(type, "IHDR", 4)) {
            w = (int)be32(data);
            bad += be32(data + 4) != (uint32_t)w || data[8] != 8 || data[9] != 3;
        } else if (!memcmp(type, "PLTE", 4)) {
            memcpy(pal, data, len);
        } else if (!memcmp(type, "IDAT", 4)) {
            size_t q = 2;                       /* zlib header */
            raw = malloc((size_t)w * (w + 1));
            int final = 0;
            while (!final && q < len) {
                final = data[q] & 1;
                bad += (data[q] >> 1) != 0;     /* stored */
                unsigned blen = data[q + 1] | (data[q + 2] << 8);
                bad += (unsigned)(data[q + 3] | (data[q + 4] << 8)) != (~blen & 0xFFFF);
                memcpy(raw + rawn, data + q + 5, blen);
                rawn += blen;
                q += 5 + blen;
            }
            /* adler */
            uint32_t a = 1, bb = 0;
            for (size_t i = 0; i < rawn; i++) { a = (a + raw[i]) % 65521; bb = (bb + a) % 65521; }
            bad += be32(data + q) != ((bb << 16) | a);
        }
        p += 12 + len;
    }
    CHECK(w == d->size * scale, "png width %d", w);
    CHECK(rawn == (size_t)w * (w + 1), "png raw %zu", rawn);
    for (int y = 0; y < w && raw; y++) {
        bad += raw[y * (w + 1)] != 0;
        for (int x = 0; x < w; x++) {
            uint8_t idx = raw[y * (w + 1) + 1 + x];
            uint8_t want = d->px[frame][(y / scale) * d->size + x / scale];
            if (idx != want) { bad++; }
            else if (memcmp(pal[idx], px_palette[idx], 3)) bad++;
        }
    }
    free(raw);
    free(b);
    return bad;
}

/* GIF LZW decoder, written from the spec and not from the encoder. */
static int check_gif(const char *path, const px_doc_t *d, int scale)
{
    size_t n;
    uint8_t *b = slurp(path, &n);
    if (!b) { printf("FAIL cannot read %s\n", path); return 1; }
    int bad = 0;
    int w = b[6] | (b[7] << 8);
    CHECK(w == d->size * scale, "gif width %d", w);
    CHECK((b[10] & 0x87) == 0x84, "gif lsd packed %02x", b[10]);
    bad += memcmp(b + 13, px_palette, 96) != 0;
    size_t p = 13 + 96;
    int frames = 0;
    unsigned delay = 0;
    uint8_t *img = malloc((size_t)w * w);
    while (p < n && b[p] != 0x3B) {
        if (b[p] == 0x21) {                     /* extension */
            if (b[p + 1] == 0xF9) delay = b[p + 4] | (b[p + 5] << 8);
            p += 2;
            while (b[p]) p += b[p] + 1;
            p++;
        } else if (b[p] == 0x2C) {
            int iw = b[p + 5] | (b[p + 6] << 8);
            bad += iw != w;
            p += 10;
            int min = b[p++];
            /* gather sub-blocks */
            uint8_t *data = malloc(n);
            size_t dn = 0;
            while (b[p]) { memcpy(data + dn, b + p + 1, b[p]); dn += b[p]; p += b[p] + 1; }
            p++;
            /* decode */
            unsigned clear = 1u << min, eoi = clear + 1;
            int width = min + 1;
            unsigned next = eoi + 1;
            static uint16_t prefix[4096];
            static uint8_t suffix[4096], first[4096];
            for (unsigned i = 0; i < clear; i++) { suffix[i] = first[i] = (uint8_t)i; }
            uint32_t acc = 0; int nb = 0; size_t di = 0;
            int prev = -1;
            size_t out = 0;
            uint8_t stack[4096];
            while (1) {
                while (nb < width && di < dn) { acc |= (uint32_t)data[di++] << nb; nb += 8; }
                if (nb < width) break;
                unsigned code = acc & ((1u << width) - 1);
                acc >>= width; nb -= width;
                if (code == clear) { next = eoi + 1; width = min + 1; prev = -1; continue; }
                if (code == eoi) break;
                unsigned c = code;
                int sp = 0;
                uint8_t fch;
                if (code < next) {
                    while (c >= clear) { stack[sp++] = suffix[c]; c = prefix[c]; }
                    stack[sp++] = (uint8_t)c;
                    fch = (uint8_t)c;
                } else if (code == next && prev >= 0) {
                    /* KwKwK: the string of prev followed by its first char.
                     * The first char goes to the BOTTOM of the stack so that
                     * it comes out last. */
                    c = (unsigned)prev;
                    while (c >= clear) c = prefix[c];
                    fch = (uint8_t)c;
                    stack[sp++] = fch;
                    c = (unsigned)prev;
                    while (c >= clear) { stack[sp++] = suffix[c]; c = prefix[c]; }
                    stack[sp++] = (uint8_t)c;
                } else { bad++; break; }
                while (sp > 0 && out < (size_t)w * w) img[out++] = stack[--sp];
                if (prev >= 0 && next < 4096) {
                    prefix[next] = (uint16_t)prev;
                    suffix[next] = fch;
                    next++;
                    if (next == (1u << width) && width < 12) width++;
                }
                prev = (int)code;
            }
            free(data);
            CHECK(out == (size_t)w * w, "gif frame %d decoded %zu of %d px", frames, out, w * w);
            for (int y = 0; y < w; y++)
                for (int x = 0; x < w; x++)
                    if (img[y * w + x] != d->px[frames][(y / scale) * d->size + x / scale]) bad++;
            frames++;
        } else { bad++; break; }
    }
    CHECK(frames == d->frames, "gif frames %d", frames);
    CHECK(d->frames == 1 || delay == (unsigned)(d->delay_ms + 5) / 10, "gif delay %u", delay);
    free(img);
    free(b);
    return bad;
}

int main(int argc, char **argv)
{
    const char *base = argc > 1 ? argv[1] : "/tmp/px";
    char path[256];
    px_doc_t *d = malloc(sizeof(*d));
    px_doc_t *e = malloc(sizeof(*e));

    /* a 16x16 document with three frames: a diagonal, a filled square, noise */
    px_doc_init(d, 16);
    d->delay_ms = 150;
    for (int i = 0; i < 16; i++) d->px[0][i * 16 + i] = 4;
    CHECK(px_doc_frame_dup(d, 0) == 1, "dup");
    CHECK(memcmp(d->px[0], d->px[1], 256) == 0, "dup copies");
    px_doc_fill(d, 1, 0, 15, 15);           /* fill the triangle below the diagonal */
    CHECK(d->px[1][15 * 16] == 15 && d->px[1][15] == 0 && d->px[1][0] == 4, "fill stays under the diagonal");
    CHECK(px_doc_frame_blank(d, 1) == 2, "blank");
    uint32_t r = 12345;
    for (int i = 0; i < 256; i++) { r = r * 1103515245u + 12345u; d->px[2][i] = (r >> 16) % PX_COLORS; }
    CHECK(d->frames == 3, "frames %d", d->frames);

    snprintf(path, sizeof(path), "%s.pix", base);
    CHECK(px_doc_save(d, path), "save");
    CHECK(px_doc_load(e, path), "load");
    CHECK(memcmp(d, e, sizeof(*d)) == 0, "round trip differs");
    int s, fr;
    CHECK(px_doc_peek(path, &s, &fr) && s == 16 && fr == 3, "peek");

    /* a foreign palette maps by nearest colour: rewrite the file's palette
     * with the channels slightly off and check the indexes survive */
    {
        size_t n; uint8_t *b = slurp(path, &n);
        for (int i = 0; i < 96; i++) b[12 + i] = (uint8_t)(b[12 + i] ^ 3);
        FILE *f = fopen(path, "wb"); fwrite(b, 1, n, f); fclose(f); free(b);
        CHECK(px_doc_load(e, path), "load foreign");
        CHECK(memcmp(d->px, e->px, sizeof(d->px)) == 0, "foreign palette maps back");
    }

    /* delete */
    CHECK(px_doc_frame_delete(d, 1), "delete");
    CHECK(d->frames == 2 && d->px[1][5] == e->px[2][5], "delete shifts");
    CHECK(px_doc_frame_delete(d, 0) && !px_doc_frame_delete(d, 0), "cannot delete the last");
    px_doc_load(d, path);

    /* exports: 16x16 at 8, and an 8x8 at 16 with a full table (noise) to
     * push the LZW through several width changes and at least one CLEAR */
    snprintf(path, sizeof(path), "%s.png", base);
    CHECK(px_export_png(d, 2, 8, path), "png");
    CHECK(check_png(path, d, 2, 8) == 0, "png pixels");
    snprintf(path, sizeof(path), "%s.gif", base);
    CHECK(px_export_gif(d, 8, path), "gif");
    CHECK(check_gif(path, d, 8) == 0, "gif pixels");

    px_doc_init(e, 8);
    for (int f2 = 0; f2 < 16; f2++) {
        if (f2) px_doc_frame_blank(e, f2 - 1);
        for (int i = 0; i < 64; i++) { r = r * 1103515245u + 12345u; e->px[f2][i] = (r >> 16) % PX_COLORS; }
    }
    CHECK(e->frames == 16, "16 frames");
    snprintf(path, sizeof(path), "%s8.gif", base);
    CHECK(px_export_gif(e, 32, path), "gif 8x8 x32");
    CHECK(check_gif(path, e, 32) == 0, "gif 8x8 pixels");
    snprintf(path, sizeof(path), "%s8.png", base);
    CHECK(px_export_png(e, 15, 32, path), "png 8x8 x32 (two stored blocks)");
    CHECK(check_png(path, e, 15, 32) == 0, "png 8x8 pixels");

    printf("%s: %d failure(s)\n", fails ? "FAILED" : "ok", fails);
    return fails;
}
