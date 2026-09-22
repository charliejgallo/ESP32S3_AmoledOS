/*
 * MONSTER HOP - an LZ4 block compressor for the packer (stdin -> stdout).
 * Greedy, one candidate per 4-byte hash; the output is a plain LZ4 block
 * (no frame), what mh_art.c decodes. Built by pack_assets.py on first use:
 *     cc -O2 -o /tmp/mh_lz4blk lz4blk.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HBITS 16
#define MINMATCH 4
#define LASTLITERALS 5
#define MFLIMIT 12

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint32_t hash4(uint32_t v) { return (v * 2654435761u) >> (32 - HBITS); }

static uint8_t *out;
static size_t on;

static void put(uint8_t b) { out[on++] = b; }

static void emit(const uint8_t *lit, size_t nlit, size_t off, size_t mlen)
{
    size_t tl = nlit < 15 ? nlit : 15;
    size_t tm = mlen ? (mlen - 4 < 15 ? mlen - 4 : 15) : 0;
    put((uint8_t)((tl << 4) | tm));
    if (nlit >= 15) {
        size_t r = nlit - 15;
        while (r >= 255) { put(255); r -= 255; }
        put((uint8_t)r);
    }
    memcpy(out + on, lit, nlit);
    on += nlit;
    if (!mlen) return;
    put((uint8_t)(off & 255));
    put((uint8_t)(off >> 8));
    if (mlen - 4 >= 15) {
        size_t r = mlen - 4 - 15;
        while (r >= 255) { put(255); r -= 255; }
        put((uint8_t)r);
    }
}

int main(void)
{
    size_t cap = 1 << 20, n = 0;
    uint8_t *in = malloc(cap);
    for (;;) {
        if (n == cap) in = realloc(in, cap *= 2);
        size_t r = fread(in + n, 1, cap - n, stdin);
        if (!r) break;
        n += r;
    }
    out = malloc(n + n / 255 + 64);
    static int32_t tab[1 << HBITS];
    for (int i = 0; i < (1 << HBITS); i++) tab[i] = -1;
    size_t anchor = 0, i = 0;
    if (n >= MFLIMIT) {
        size_t limit = n - MFLIMIT;
        while (i < limit) {
            uint32_t h = hash4(rd32(in + i));
            int32_t c = tab[h];
            tab[h] = (int32_t)i;
            if (c >= 0 && i - (size_t)c <= 65535 && rd32(in + c) == rd32(in + i)) {
                size_t m = MINMATCH, maxm = n - LASTLITERALS - i;
                while (m < maxm && in[c + m] == in[i + m]) m++;
                /* extend backwards over pending literals */
                while (i > anchor && c > 0 && in[c - 1] == in[i - 1]) { i--; c--; m++; }
                emit(in + anchor, i - anchor, i - (size_t)c, m);
                for (size_t k = i + 1; k < i + m && k + 4 <= n && k < limit; k += 2) tab[hash4(rd32(in + k))] = (int32_t)k;
                i += m;
                anchor = i;
            } else {
                i++;
            }
        }
    }
    emit(in + anchor, n - anchor, 0, 0);
    fwrite(out, 1, on, stdout);
    return 0;
}
