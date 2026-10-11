/*
 * BLE - AES-128 and CCM, to read the sensors that encrypt what they
 * advertise: Xiaomi's MiBeacon v4/v5 and BTHome v2, both AES-CCM with a
 * 4-byte tag and the device's own 16-byte key.
 *
 * Plain C of our own rather than mbedTLS: the firmware has it, but its
 * symbols are not in the table the apps link against, and CCM decryption
 * only needs AES's forward direction. Checked against FIPS-197 and NIST
 * SP 800-38C's examples (apps/ble/test). A few packets every few seconds:
 * speed does not matter, size does.
 */
#include "bl_crypt.h"

#include <string.h>

static const uint8_t SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }

void bl_aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
    uint8_t rk[176];
    memcpy(rk, key, 16);
    uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
        uint8_t t[4] = { rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1] };
        if (i % 16 == 0) {
            uint8_t a = t[0];
            t[0] = SBOX[t[1]] ^ rcon;
            t[1] = SBOX[t[2]];
            t[2] = SBOX[t[3]];
            t[3] = SBOX[a];
            rcon = xtime(rcon);
        }
        for (int k = 0; k < 4; k++) rk[i + k] = rk[i - 16 + k] ^ t[k];
    }
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
    for (int round = 1; round <= 10; round++) {
        uint8_t t[16];
        /* SubBytes and ShiftRows: column c of row r takes column c + r */
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) t[c * 4 + r] = SBOX[s[((c + r) % 4) * 4 + r]];
        if (round < 10) {
            for (int c = 0; c < 4; c++) {           /* MixColumns */
                uint8_t *p = t + c * 4;
                uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3], all = a0 ^ a1 ^ a2 ^ a3;
                p[0] ^= all ^ xtime(a0 ^ a1);
                p[1] ^= all ^ xtime(a1 ^ a2);
                p[2] ^= all ^ xtime(a2 ^ a3);
                p[3] ^= all ^ xtime(a3 ^ a0);
            }
        }
        for (int i = 0; i < 16; i++) s[i] = t[i] ^ rk[round * 16 + i];
    }
    memcpy(out, s, 16);
}

/* Counter block i: flags (L - 1), the nonce, i in L bytes big endian. */
static void ctr_block(uint8_t b[16], const uint8_t *nonce, int nlen, uint32_t i)
{
    int L = 15 - nlen;
    memset(b, 0, 16);
    b[0] = (uint8_t)(L - 1);
    memcpy(b + 1, nonce, nlen);
    for (int k = 0; k < L && k < 4; k++) b[15 - k] = (uint8_t)(i >> (8 * k));
}

bool bl_ccm_decrypt(const uint8_t key[16], const uint8_t *nonce, int nlen, const uint8_t *aad, int alen,
                    const uint8_t *c, int clen, const uint8_t *tag, int mlen, uint8_t *out)
{
    if (nlen < 7 || nlen > 13 || mlen < 4 || mlen > 16 || (mlen & 1) || clen < 0 || alen < 0 || alen > 62)
        return false;
    int L = 15 - nlen;
    if (L < 4 && clen >= (1 << (8 * L))) return false;
    uint8_t a[16], s[16];
    /* the payload: P = C xor S_i, i from 1 */
    for (int off = 0, i = 1; off < clen; off += 16, i++) {
        ctr_block(a, nonce, nlen, (uint32_t)i);
        bl_aes128_encrypt(key, a, s);
        int n = clen - off < 16 ? clen - off : 16;
        for (int k = 0; k < n; k++) out[off + k] = c[off + k] ^ s[k];
    }
    /* the CBC-MAC over B0, the associated data and the payload */
    uint8_t x[16], b[16];
    memset(b, 0, 16);
    b[0] = (uint8_t)((alen ? 0x40 : 0) | (((mlen - 2) / 2) << 3) | (L - 1));
    memcpy(b + 1, nonce, nlen);
    for (int k = 0; k < L && k < 4; k++) b[15 - k] = (uint8_t)((uint32_t)clen >> (8 * k));
    bl_aes128_encrypt(key, b, x);
    if (alen) {
        /* two bytes of length, then the data, in blocks padded with zeros */
        uint8_t ab[2 + 62 + 14];
        int an = 2 + alen;
        memset(ab, 0, sizeof ab);
        ab[0] = (uint8_t)(alen >> 8);
        ab[1] = (uint8_t)alen;
        memcpy(ab + 2, aad, alen);
        for (int off = 0; off < an; off += 16) {
            for (int k = 0; k < 16; k++) x[k] ^= ab[off + k];
            bl_aes128_encrypt(key, x, x);
        }
    }
    for (int off = 0; off < clen; off += 16) {
        int n = clen - off < 16 ? clen - off : 16;
        for (int k = 0; k < n; k++) x[k] ^= out[off + k];
        bl_aes128_encrypt(key, x, x);
    }
    /* the tag: the MAC's first bytes xor S_0 */
    ctr_block(a, nonce, nlen, 0);
    bl_aes128_encrypt(key, a, s);
    uint8_t diff = 0;
    for (int k = 0; k < mlen; k++) diff |= (uint8_t)((x[k] ^ s[k]) ^ tag[k]);
    if (diff) memset(out, 0, clen);
    return diff == 0;
}

bool bl_parse_key(const char *s, uint8_t key[16])
{
    int n = 0, hi = -1;
    for (; *s; s++) {
        int v;
        if (*s >= '0' && *s <= '9') v = *s - '0';
        else if (*s >= 'a' && *s <= 'f') v = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') v = *s - 'A' + 10;
        else if (*s == ' ' || *s == ':' || *s == '-') continue;
        else return false;
        if (hi < 0) hi = v;
        else {
            if (n >= 16) return false;
            key[n++] = (uint8_t)(hi << 4 | v);
            hi = -1;
        }
    }
    return n == 16 && hi < 0;
}
