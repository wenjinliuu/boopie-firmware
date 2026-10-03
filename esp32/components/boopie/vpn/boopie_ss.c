/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_ss.h"

#include <string.h>

#include "psa/crypto.h"

static const struct {
    const char *name;
    int key_len;
} CIPHERS[BOOPIE_SS_CIPHER_COUNT] = {
    [BOOPIE_SS_AES_128_GCM] = { "aes-128-gcm", 16 },
    [BOOPIE_SS_AES_192_GCM] = { "aes-192-gcm", 24 },
    [BOOPIE_SS_AES_256_GCM] = { "aes-256-gcm", 32 },
    [BOOPIE_SS_CHACHA20_POLY1305] = { "chacha20-ietf-poly1305", 32 },
};

boopie_ss_cipher_t boopie_ss_cipher(const char *name)
{
    for (int i = 0; i < BOOPIE_SS_CIPHER_COUNT; i++) {
        if (name && strcmp(name, CIPHERS[i].name) == 0) {
            return (boopie_ss_cipher_t)i;
        }
    }
    if (name && strcmp(name, "chacha20-poly1305") == 0) {   /* how some subscriptions spell it */
        return BOOPIE_SS_CHACHA20_POLY1305;
    }
    return BOOPIE_SS_UNSUPPORTED;
}

const char *boopie_ss_cipher_name(boopie_ss_cipher_t c)
{
    return c >= 0 && c < BOOPIE_SS_CIPHER_COUNT ? CIPHERS[c].name : "";
}

int boopie_ss_key_len(boopie_ss_cipher_t c)
{
    return c >= 0 && c < BOOPIE_SS_CIPHER_COUNT ? CIPHERS[c].key_len : 0;
}

static bool hash(psa_algorithm_t alg, const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    size_t len;
    return psa_hash_compute(alg, in, n, out, cap, &len) == PSA_SUCCESS;
}

bool boopie_ss_password_key(boopie_ss_cipher_t c, const char *password, uint8_t key[BOOPIE_SS_KEY_MAX])
{
    int need = boopie_ss_key_len(c);
    size_t pw = strlen(password);
    if (!need || pw > 128 || psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    /* D1 = MD5(password), Di = MD5(Di-1 || password), key = D1 || D2 ... */
    uint8_t buf[16 + 128], d[16];
    size_t have = 0, prev = 0;
    while ((int)have < need) {
        memcpy(buf + prev, password, pw);
        if (!hash(PSA_ALG_MD5, buf, prev + pw, d, sizeof d)) {
            return false;
        }
        size_t take = need - have < 16 ? need - have : 16;
        memcpy(key + have, d, take);
        have += take;
        memcpy(buf, d, 16);
        prev = 16;
    }
    return true;
}

/* HMAC-SHA1, made from the hash. */
static bool hmac_sha1(const uint8_t *key, size_t key_len, const uint8_t *msg, size_t n, uint8_t out[20])
{
    uint8_t k[64] = { 0 }, inner[64 + 64 + 1 + 20], outer[64 + 20];
    if (key_len > 64 || n > 64 + 1 + 20) {
        return false;
    }
    memcpy(k, key, key_len);
    for (int i = 0; i < 64; i++) {
        inner[i] = k[i] ^ 0x36;
        outer[i] = k[i] ^ 0x5c;
    }
    memcpy(inner + 64, msg, n);
    return hash(PSA_ALG_SHA_1, inner, 64 + n, outer + 64, 20) && hash(PSA_ALG_SHA_1, outer, sizeof outer, out, 20);
}

/* HKDF-SHA1 (RFC 5869) with info "ss-subkey". */
static bool subkey(const uint8_t *key, const uint8_t *salt, int len, uint8_t *out)
{
    static const char INFO[] = "ss-subkey";
    uint8_t prk[20], t[20 + sizeof INFO - 1 + 1], block[20];
    if (!hmac_sha1(salt, len, key, len, prk)) {
        return false;
    }
    size_t prev = 0;
    for (int i = 1, have = 0; have < len; i++) {
        memcpy(t, block, prev);
        memcpy(t + prev, INFO, sizeof INFO - 1);
        t[prev + sizeof INFO - 1] = (uint8_t)i;
        if (!hmac_sha1(prk, sizeof prk, t, prev + sizeof INFO, block)) {
            return false;
        }
        int take = len - have < 20 ? len - have : 20;
        memcpy(out + have, block, take);
        have += take;
        prev = 20;
    }
    return true;
}

static psa_algorithm_t alg_of(boopie_ss_cipher_t c)
{
    return c == BOOPIE_SS_CHACHA20_POLY1305 ? PSA_ALG_CHACHA20_POLY1305 : PSA_ALG_GCM;
}

bool boopie_ss_aead_init(boopie_ss_aead_t *a, boopie_ss_cipher_t c, const uint8_t *key, const uint8_t *salt)
{
    memset(a, 0, sizeof *a);
    a->cipher = c;
    int len = boopie_ss_key_len(c);
    uint8_t sub[BOOPIE_SS_KEY_MAX];
    if (!len || psa_crypto_init() != PSA_SUCCESS || !subkey(key, salt, len, sub)) {
        return false;
    }
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attr, alg_of(c));
    psa_set_key_type(&attr, c == BOOPIE_SS_CHACHA20_POLY1305 ? PSA_KEY_TYPE_CHACHA20 : PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attr, (size_t)len * 8);
    psa_key_id_t id;
    psa_status_t st = psa_import_key(&attr, sub, len, &id);
    psa_reset_key_attributes(&attr);
    memset(sub, 0, sizeof sub);
    if (st != PSA_SUCCESS) {
        return false;
    }
    a->key_id = id;
    return true;
}

void boopie_ss_aead_free(boopie_ss_aead_t *a)
{
    if (a->key_id) {
        psa_destroy_key(a->key_id);
    }
    memset(a, 0, sizeof *a);
}

static void next_nonce(uint8_t nonce[12])
{
    for (int i = 0; i < 12 && ++nonce[i] == 0; i++) {
    }
}

static bool seal(boopie_ss_aead_t *a, const uint8_t *in, size_t n, uint8_t *out)
{
    size_t len;
    psa_status_t st = psa_aead_encrypt(a->key_id, alg_of(a->cipher), a->nonce, 12, NULL, 0, in, n, out,
                                       n + BOOPIE_SS_TAG, &len);
    next_nonce(a->nonce);
    return st == PSA_SUCCESS && len == n + BOOPIE_SS_TAG;
}

static bool open_(boopie_ss_aead_t *a, const uint8_t *in, size_t n, uint8_t *out)
{
    size_t len;
    psa_status_t st = psa_aead_decrypt(a->key_id, alg_of(a->cipher), a->nonce, 12, NULL, 0, in, n, out,
                                       n - BOOPIE_SS_TAG, &len);
    next_nonce(a->nonce);
    return st == PSA_SUCCESS && len == n - BOOPIE_SS_TAG;
}

size_t boopie_ss_seal(boopie_ss_aead_t *a, const uint8_t *in, size_t n, uint8_t *out)
{
    if (n == 0 || n > BOOPIE_SS_PAYLOAD_MAX) {
        return 0;
    }
    uint8_t len[2] = { (uint8_t)(n >> 8), (uint8_t)n };
    if (!seal(a, len, 2, out) || !seal(a, in, n, out + 2 + BOOPIE_SS_TAG)) {
        return 0;
    }
    return 2 + BOOPIE_SS_TAG + n + BOOPIE_SS_TAG;
}

int boopie_ss_decode(boopie_ss_decoder_t *d, const uint8_t *in, size_t n, size_t *used, uint8_t *out)
{
    *used = 0;
    size_t need = d->want_payload ? d->want_payload + BOOPIE_SS_TAG : 2 + BOOPIE_SS_TAG;
    size_t take = need - d->have < n ? need - d->have : n;
    memcpy(d->buf + d->have, in, take);
    d->have += take;
    *used = take;
    if (d->have < need) {
        return 0;
    }
    if (!d->want_payload) {
        uint8_t len[2];
        if (!open_(&d->aead, d->buf, d->have, len)) {
            return -1;
        }
        d->want_payload = ((size_t)len[0] << 8 | len[1]) & BOOPIE_SS_PAYLOAD_MAX;
        d->have = 0;
        if (!d->want_payload) {
            return -1;
        }
        size_t more;
        int r = boopie_ss_decode(d, in + take, n - take, &more, out);
        *used += more;
        return r;
    }
    size_t plen = d->want_payload;
    if (!open_(&d->aead, d->buf, d->have, out)) {
        return -1;
    }
    d->want_payload = 0;
    d->have = 0;
    return (int)plen;
}

size_t boopie_ss_address(const char *host, uint16_t port, uint8_t *out)
{
    size_t n = strlen(host);
    if (n == 0 || n > 255) {
        return 0;
    }
    out[0] = 3;   /* a domain name: the far end looks it up, not us */
    out[1] = (uint8_t)n;
    memcpy(out + 2, host, n);
    out[2 + n] = (uint8_t)(port >> 8);
    out[3 + n] = (uint8_t)port;
    return 4 + n;
}
