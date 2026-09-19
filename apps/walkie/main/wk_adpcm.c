#include "wk_adpcm.h"

static const int8_t INDEX_TABLE[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

static const int16_t STEP_TABLE[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

static uint8_t encode_one(wk_adpcm_t *st, int16_t sample)
{
    int step = STEP_TABLE[st->index];
    int diff = (int)sample - (int)st->pred;
    uint8_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    int delta = 0;
    if (diff >= step) { code |= 4; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 2; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 1; delta += step; }
    delta += STEP_TABLE[st->index] >> 3;
    int pred = (int)st->pred + ((code & 8) ? -delta : delta);
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    st->pred = (int16_t)pred;
    int idx = (int)st->index + INDEX_TABLE[code];
    if (idx < 0) idx = 0;
    if (idx > 88) idx = 88;
    st->index = (uint8_t)idx;
    return code;
}

static int16_t decode_one(wk_adpcm_t *st, uint8_t code)
{
    int step = STEP_TABLE[st->index];
    int delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    int pred = (int)st->pred + ((code & 8) ? -delta : delta);
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    st->pred = (int16_t)pred;
    int idx = (int)st->index + INDEX_TABLE[code];
    if (idx < 0) idx = 0;
    if (idx > 88) idx = 88;
    st->index = (uint8_t)idx;
    return st->pred;
}

void wk_adpcm_encode(wk_adpcm_t *st, const int16_t *pcm, int n, uint8_t *out)
{
    for (int i = 0; i + 1 < n; i += 2) {
        uint8_t lo = encode_one(st, pcm[i]);
        uint8_t hi = encode_one(st, pcm[i + 1]);
        out[i / 2] = (uint8_t)(lo | (hi << 4));
    }
}

void wk_adpcm_decode(wk_adpcm_t *st, const uint8_t *in, int n, int16_t *pcm)
{
    for (int i = 0; i + 1 < n; i += 2) {
        pcm[i]     = decode_one(st, in[i / 2] & 0x0F);
        pcm[i + 1] = decode_one(st, in[i / 2] >> 4);
    }
}
