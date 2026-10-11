/*
 * BLE - AES-128 and CCM for the sensors that encrypt their advertisements
 * (bl_crypt.c). Pure C, tested on the Mac by apps/ble/test.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

void bl_aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

/* CCM (RFC 3610 / NIST SP 800-38C): nonce of 7..13 bytes, tag of mlen
 * bytes (4..16, even). Writes clen bytes to out and returns true when the
 * tag matches; on a mismatch out is zeroed and it returns false. */
bool bl_ccm_decrypt(const uint8_t key[16], const uint8_t *nonce, int nlen, const uint8_t *aad, int alen,
                    const uint8_t *c, int clen, const uint8_t *tag, int mlen, uint8_t *out);

/* 32 hex digits (spaces, colons and dashes allowed) into 16 bytes. */
bool bl_parse_key(const char *s, uint8_t key[16]);
