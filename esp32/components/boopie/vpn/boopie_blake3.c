/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_blake3.h"

#include <string.h>

enum {
    CHUNK_START = 1 << 0,
    CHUNK_END = 1 << 1,
    ROOT = 1 << 3,
    DERIVE_KEY_CONTEXT = 1 << 5,
    DERIVE_KEY_MATERIAL = 1 << 6,
};

static const uint32_t IV[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A, 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,
};

static const uint8_t PERMUTE[16] = { 2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8 };

static uint32_t rotr(uint32_t x, int n)
{
    return x >> n | x << (32 - n);
}

static void g(uint32_t *s, int a, int b, int c, int d, uint32_t x, uint32_t y)
{
    s[a] += s[b] + x;
    s[d] = rotr(s[d] ^ s[a], 16);
    s[c] += s[d];
    s[b] = rotr(s[b] ^ s[c], 12);
    s[a] += s[b] + y;
    s[d] = rotr(s[d] ^ s[a], 8);
    s[c] += s[d];
    s[b] = rotr(s[b] ^ s[c], 7);
}

/* The compression function: all 16 words out (the first 8 are the next cv). */
static void compress(const uint32_t cv[8], const uint8_t block[64], uint32_t block_len, uint32_t flags,
                     uint32_t out[16])
{
    uint32_t m[16], t[16], s[16];
    for (int i = 0; i < 16; i++) {
        m[i] = (uint32_t)block[4 * i] | (uint32_t)block[4 * i + 1] << 8 | (uint32_t)block[4 * i + 2] << 16
             | (uint32_t)block[4 * i + 3] << 24;
    }
    memcpy(s, cv, 32);
    memcpy(s + 8, IV, 16);
    s[12] = 0;   /* the chunk counter: always the first chunk here */
    s[13] = 0;
    s[14] = block_len;
    s[15] = flags;
    for (int r = 0; r < 7; r++) {
        g(s, 0, 4, 8, 12, m[0], m[1]);
        g(s, 1, 5, 9, 13, m[2], m[3]);
        g(s, 2, 6, 10, 14, m[4], m[5]);
        g(s, 3, 7, 11, 15, m[6], m[7]);
        g(s, 0, 5, 10, 15, m[8], m[9]);
        g(s, 1, 6, 11, 12, m[10], m[11]);
        g(s, 2, 7, 8, 13, m[12], m[13]);
        g(s, 3, 4, 9, 14, m[14], m[15]);
        if (r < 6) {
            for (int i = 0; i < 16; i++) {
                t[i] = m[PERMUTE[i]];
            }
            memcpy(m, t, sizeof m);
        }
    }
    for (int i = 0; i < 8; i++) {
        out[i] = s[i] ^ s[i + 8];
        out[i + 8] = s[i + 8] ^ cv[i];
    }
}

/* One chunk under key words `key` and `flags`, to out_len bytes of root output. */
static int one_chunk(const uint32_t key[8], uint32_t flags, const uint8_t *in, size_t n, uint8_t *out, size_t out_len)
{
    if (n > BOOPIE_BLAKE3_INPUT_MAX || out_len > 64) {
        return -1;
    }
    uint32_t cv[8], w[16];
    memcpy(cv, key, 32);
    size_t blocks = n ? (n + 63) / 64 : 1;
    for (size_t b = 0; b < blocks; b++) {
        uint8_t block[64] = { 0 };
        size_t len = n - b * 64 < 64 ? n - b * 64 : 64;
        memcpy(block, in + b * 64, len);
        uint32_t f = flags | (b == 0 ? CHUNK_START : 0);
        if (b + 1 == blocks) {
            compress(cv, block, (uint32_t)len, f | CHUNK_END | ROOT, w);
            for (size_t i = 0; i < out_len; i++) {
                out[i] = (uint8_t)(w[i / 4] >> (8 * (i % 4)));
            }
        } else {
            compress(cv, block, 64, f, w);
            memcpy(cv, w, 32);
        }
    }
    return 0;
}

int boopie_blake3_hash(const void *in, size_t n, uint8_t *out, size_t out_len)
{
    return one_chunk(IV, 0, in, n, out, out_len);
}

int boopie_blake3_derive_key(const char *context, const void *material, size_t n, uint8_t *out, size_t out_len)
{
    uint8_t ck[32];
    uint32_t key[8];
    if (one_chunk(IV, DERIVE_KEY_CONTEXT, (const uint8_t *)context, strlen(context), ck, 32) < 0) {
        return -1;
    }
    for (int i = 0; i < 8; i++) {
        key[i] = (uint32_t)ck[4 * i] | (uint32_t)ck[4 * i + 1] << 8 | (uint32_t)ck[4 * i + 2] << 16
               | (uint32_t)ck[4 * i + 3] << 24;
    }
    return one_chunk(key, DERIVE_KEY_MATERIAL, material, n, out, out_len);
}
