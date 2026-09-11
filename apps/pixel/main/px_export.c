/*
 * PIXEL ART - PNG and GIF encoders. See px_export.h.
 */
#include "px_export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Shared: a scaled-up frame as a row of indexes
 *
 * Each row of the output is built once into a buffer of scale*size bytes and
 * written scale times: the encoders never see the small frame.
 * -------------------------------------------------------------------------- */

static void scaled_row(const px_doc_t *d, int frame, int y, int scale, uint8_t *out)
{
    const uint8_t *src = d->px[frame] + y * d->size;
    for (int x = 0; x < d->size; x++) {
        memset(out + x * scale, src[x], (size_t)scale);
    }
}

static void put_u32be(FILE *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    fwrite(b, 1, 4, f);
}

static void put_u16le(FILE *f, unsigned v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    fwrite(b, 1, 2, f);
}

/* --------------------------------------------------------------------------
 * PNG
 * -------------------------------------------------------------------------- */

/* The CRC table is built on first use and lives in .bss, which for a dynamic
 * app the loader sends to PSRAM: a kilobyte that costs no internal RAM. */
static uint32_t s_crc_table[256];
static bool     s_crc_ready;

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!s_crc_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            s_crc_table[i] = c;
        }
        s_crc_ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc = s_crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

/* A chunk written in one go: type + data, length before, CRC after. */
static void png_chunk(FILE *f, const char *type, const uint8_t *data, size_t n)
{
    put_u32be(f, (uint32_t)n);
    uint32_t crc = crc32_update(0, (const uint8_t *)type, 4);
    fwrite(type, 1, 4, f);
    if (n) {
        fwrite(data, 1, n, f);
        crc = crc32_update(crc, data, n);
    }
    put_u32be(f, crc);
}

/* The IDAT is streamed: its length is known in advance because stored
 * deflate blocks have a fixed overhead, so the chunk header goes first and
 * the CRC is accumulated as the rows are written. */
typedef struct {
    FILE    *f;
    uint32_t crc;
    uint32_t a, b;          /* adler32 */
    uint32_t block_left;    /* bytes still to write in the open stored block */
    uint32_t total_left;    /* raw bytes still to come */
} idat_t;

static void idat_put(idat_t *s, const uint8_t *p, size_t n)
{
    fwrite(p, 1, n, s->f);
    s->crc = crc32_update(s->crc, p, n);
}

static void idat_raw(idat_t *s, const uint8_t *p, size_t n)
{
    while (n) {
        if (s->block_left == 0) {
            /* A stored block: BFINAL when it is the last, BTYPE 00. */
            uint32_t len = s->total_left > 65535 ? 65535 : s->total_left;
            uint8_t hdr[5] = { (uint8_t)(len == s->total_left ? 1 : 0),
                               (uint8_t)len, (uint8_t)(len >> 8),
                               (uint8_t)~len, (uint8_t)(~len >> 8) };
            idat_put(s, hdr, 5);
            s->block_left = len;
        }
        size_t take = n < s->block_left ? n : s->block_left;
        idat_put(s, p, take);
        for (size_t i = 0; i < take; i++) {
            s->a = (s->a + p[i]) % 65521;
            s->b = (s->b + s->a) % 65521;
        }
        p += take;
        n -= take;
        s->block_left -= (uint32_t)take;
        s->total_left -= (uint32_t)take;
    }
}

bool px_export_png(const px_doc_t *d, int frame, int scale, const char *path)
{
    if (frame < 0 || frame >= d->frames || scale < 1 || scale > 32) {
        return false;
    }
    int w = d->size * scale;
    uint8_t *row = malloc((size_t)w + 1);
    if (!row) {
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(row);
        return false;
    }

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13] = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                         (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                         8, 3, 0, 0, 0 };     /* 8 bits, indexed colour */
    png_chunk(f, "IHDR", ihdr, sizeof(ihdr));
    png_chunk(f, "PLTE", &px_palette[0][0], sizeof(px_palette));

    /* IDAT: zlib header (2) + stored blocks (5 each) + raw + adler (4). */
    uint32_t raw = (uint32_t)(w + 1) * (uint32_t)w;
    uint32_t blocks = (raw + 65534) / 65535;
    uint32_t idat_len = 2 + blocks * 5 + raw + 4;
    put_u32be(f, idat_len);
    idat_t s = { .f = f, .crc = crc32_update(0, (const uint8_t *)"IDAT", 4),
                 .a = 1, .b = 0, .block_left = 0, .total_left = raw };
    fwrite("IDAT", 1, 4, f);
    static const uint8_t zhdr[2] = { 0x78, 0x01 };
    idat_put(&s, zhdr, 2);

    row[0] = 0;                             /* filter: none */
    for (int y = 0; y < d->size; y++) {
        scaled_row(d, frame, y, scale, row + 1);
        for (int k = 0; k < scale; k++) {
            idat_raw(&s, row, (size_t)w + 1);
        }
    }
    uint8_t adler[4] = { (uint8_t)(s.b >> 8), (uint8_t)s.b, (uint8_t)(s.a >> 8), (uint8_t)s.a };
    idat_put(&s, adler, 4);
    put_u32be(f, s.crc);

    png_chunk(f, "IEND", NULL, 0);
    bool ok = fclose(f) == 0;
    free(row);
    if (!ok) {
        remove(path);
    }
    return ok;
}

/* --------------------------------------------------------------------------
 * GIF
 *
 * LZW as the format specifies it: codes start at min_code_size + 1 bits and
 * widen when the table reaches a power of two; the table is reset with a
 * CLEAR when it is full. The decoder's table always runs one entry behind the
 * encoder's, which is why the width grows right AFTER adding entry 2^n here:
 * the decoder reaches 2^n on reading that same code and switches too.
 * -------------------------------------------------------------------------- */

#define LZW_HASH    5003            /* prime, > 4096 */
#define LZW_MAX     4096

typedef struct {
    FILE    *f;
    uint8_t  block[255];
    int      blen;
    uint32_t acc;
    int      nbits;
    int32_t *hkey;                  /* (prefix << 8 | pixel), or -1 */
    uint16_t *hcode;
} lzw_t;

static void lzw_flush_block(lzw_t *z)
{
    if (z->blen) {
        uint8_t n = (uint8_t)z->blen;
        fwrite(&n, 1, 1, z->f);
        fwrite(z->block, 1, (size_t)z->blen, z->f);
        z->blen = 0;
    }
}

static void lzw_put(lzw_t *z, unsigned code, int width)
{
    z->acc |= code << z->nbits;
    z->nbits += width;
    while (z->nbits >= 8) {
        z->block[z->blen++] = (uint8_t)z->acc;
        z->acc >>= 8;
        z->nbits -= 8;
        if (z->blen == 255) {
            lzw_flush_block(z);
        }
    }
}

static void lzw_reset(lzw_t *z)
{
    for (int i = 0; i < LZW_HASH; i++) {
        z->hkey[i] = -1;
    }
}

static int lzw_find(const lzw_t *z, int32_t key)
{
    unsigned h = ((unsigned)key * 2654435761u) % LZW_HASH;
    while (z->hkey[h] != -1) {
        if (z->hkey[h] == key) {
            return z->hcode[h];
        }
        h = (h + 1) % LZW_HASH;
    }
    return -1;
}

static void lzw_add(lzw_t *z, int32_t key, unsigned code)
{
    unsigned h = ((unsigned)key * 2654435761u) % LZW_HASH;
    while (z->hkey[h] != -1) {
        h = (h + 1) % LZW_HASH;
    }
    z->hkey[h] = key;
    z->hcode[h] = (uint16_t)code;
}

static void gif_frame(lzw_t *z, const px_doc_t *d, int frame, int scale)
{
    const int min_code = 5;                 /* 32 colours */
    const unsigned clear = 1u << min_code, eoi = clear + 1;
    int width = min_code + 1;
    unsigned free_code = eoi + 1;
    int w = d->size * scale;

    uint8_t byte = (uint8_t)min_code;
    fwrite(&byte, 1, 1, z->f);

    z->blen = 0;
    z->acc = 0;
    z->nbits = 0;
    lzw_reset(z);
    lzw_put(z, clear, width);

    int prefix = -1;
    uint8_t *row = (uint8_t *)z->block;     /* reused below, not here */
    (void)row;
    uint8_t line[PX_MAX_SIZE * 32];
    for (int y = 0; y < d->size; y++) {
        scaled_row(d, frame, y, scale, line);
        for (int k = 0; k < scale; k++) {
            for (int x = 0; x < w; x++) {
                int c = line[x];
                if (prefix < 0) {
                    prefix = c;
                    continue;
                }
                int32_t key = (prefix << 8) | c;
                int found = lzw_find(z, key);
                if (found >= 0) {
                    prefix = found;
                    continue;
                }
                lzw_put(z, (unsigned)prefix, width);
                if (free_code < LZW_MAX) {
                    lzw_add(z, key, free_code);
                    free_code++;
                    if (free_code > (1u << width) && width < 12) {
                        width++;
                    }
                } else {
                    lzw_put(z, clear, width);
                    lzw_reset(z);
                    width = min_code + 1;
                    free_code = eoi + 1;
                }
                prefix = c;
            }
        }
    }
    if (prefix >= 0) {
        lzw_put(z, (unsigned)prefix, width);
    }
    lzw_put(z, eoi, width);
    if (z->nbits > 0) {
        z->block[z->blen++] = (uint8_t)z->acc;
        if (z->blen == 255) {
            lzw_flush_block(z);
        }
    }
    lzw_flush_block(z);
    byte = 0;                               /* block terminator */
    fwrite(&byte, 1, 1, z->f);
}

bool px_export_gif(const px_doc_t *d, int scale, const char *path)
{
    if (scale < 1 || scale > 32) {
        return false;
    }
    lzw_t z;
    memset(&z, 0, sizeof(z));
    z.hkey  = malloc(LZW_HASH * sizeof(int32_t));
    z.hcode = malloc(LZW_HASH * sizeof(uint16_t));
    if (!z.hkey || !z.hcode) {
        free(z.hkey);
        free(z.hcode);
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(z.hkey);
        free(z.hcode);
        return false;
    }
    z.f = f;
    int w = d->size * scale;

    fwrite("GIF89a", 1, 6, f);
    put_u16le(f, (unsigned)w);
    put_u16le(f, (unsigned)w);
    /* global colour table present, 8 bits of colour resolution, 2^(4+1) = 32 entries */
    uint8_t lsd[3] = { 0xF0 | 4, 0, 0 };
    fwrite(lsd, 1, 3, f);
    fwrite(px_palette, 1, sizeof(px_palette), f);

    if (d->frames > 1) {
        /* Netscape loop extension: loop for ever. */
        static const uint8_t loop[19] = { 0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P',
                                          'E', '2', '.', '0', 0x03, 0x01, 0, 0, 0x00 };
        fwrite(loop, 1, sizeof(loop), f);
    }

    unsigned delay_cs = (d->delay_ms + 5) / 10;
    if (delay_cs < 2) delay_cs = 2;         /* browsers treat < 2 as 10 */
    for (int fr = 0; fr < d->frames; fr++) {
        /* graphic control: disposal "do not dispose", no transparency */
        uint8_t gce[8] = { 0x21, 0xF9, 0x04, 0x04, (uint8_t)delay_cs, (uint8_t)(delay_cs >> 8), 0, 0 };
        fwrite(gce, 1, sizeof(gce), f);
        uint8_t desc = 0x2C;
        fwrite(&desc, 1, 1, f);
        put_u16le(f, 0);
        put_u16le(f, 0);
        put_u16le(f, (unsigned)w);
        put_u16le(f, (unsigned)w);
        uint8_t packed = 0;                 /* no local colour table */
        fwrite(&packed, 1, 1, f);
        gif_frame(&z, d, fr, scale);
    }
    uint8_t trailer = 0x3B;
    fwrite(&trailer, 1, 1, f);

    bool ok = fclose(f) == 0;
    free(z.hkey);
    free(z.hcode);
    if (!ok) {
        remove(path);
    }
    return ok;
}
