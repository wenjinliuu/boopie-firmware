/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Shadowsocks AEAD (SIP004), the stream format of every ss:// node: a random
 * salt, then chunks of [length + tag][payload + tag], each sealed with a
 * subkey made from the salt (HKDF-SHA1, "ss-subkey") and a counting nonce.
 * The password becomes the key as OpenSSL's EVP_BytesToKey makes it.
 *
 * And Shadowsocks 2022 (SIP022): the password is the key itself, base64
 * (several, "iPSK:uPSK", for servers behind a relay or with many users), the
 * subkey comes from BLAKE3, and each direction opens with headers: the
 * client's says when (the server refuses old ones) and where to, the
 * server's echoes the client's salt. Chunks may be up to 64 KB.
 *
 * Plain C over PSA Crypto (mbedtls), so the host tests run it too.
 */

typedef enum {
    BOOPIE_SS_AES_128_GCM,
    BOOPIE_SS_AES_192_GCM,
    BOOPIE_SS_AES_256_GCM,
    BOOPIE_SS_CHACHA20_POLY1305,
    BOOPIE_SS_2022_AES_128_GCM,
    BOOPIE_SS_2022_AES_256_GCM,
    BOOPIE_SS_2022_CHACHA20_POLY1305,
    BOOPIE_SS_CIPHER_COUNT,
    BOOPIE_SS_UNSUPPORTED = -1,   /* a cipher we don't have: 2022-*, stream ciphers */
} boopie_ss_cipher_t;

#define BOOPIE_SS_KEY_MAX 32
#define BOOPIE_SS_TAG 16
#define BOOPIE_SS_PAYLOAD_MAX 0x3FFF      /* what we send in a chunk */
#define BOOPIE_SS_OPEN_MAX 0xFFFF         /* what a 2022 server may send in one */
/* A sealed chunk at most: length, payload, both tags. */
#define BOOPIE_SS_CHUNK_MAX (2 + BOOPIE_SS_TAG + BOOPIE_SS_PAYLOAD_MAX + BOOPIE_SS_TAG)

/* The cipher by its name in a node ("aes-256-gcm", "chacha20-ietf-poly1305"). */
boopie_ss_cipher_t boopie_ss_cipher(const char *name);
const char *boopie_ss_cipher_name(boopie_ss_cipher_t c);
int boopie_ss_key_len(boopie_ss_cipher_t c);   /* = the salt's length */

bool boopie_ss_is_2022(boopie_ss_cipher_t c);

/* The master key from a password (EVP_BytesToKey with MD5); for 2022 the
 * user's key (the last of the password's keys). */
bool boopie_ss_password_key(boopie_ss_cipher_t c, const char *password, uint8_t key[BOOPIE_SS_KEY_MAX]);

/* 2022: the password's keys, base64, ':' between them; returns how many
 * (up to max), 0 if one isn't a key of the cipher's length. */
#define BOOPIE_SS_2022_KEYS_MAX 4
int boopie_ss_2022_keys(boopie_ss_cipher_t c, const char *password, uint8_t keys[][BOOPIE_SS_KEY_MAX], int max);

typedef struct {
    boopie_ss_cipher_t cipher;
    uint32_t key_id;         /* the subkey, imported into PSA */
    uint8_t nonce[12];
} boopie_ss_aead_t;

/* One direction of a stream, from the master key and that direction's salt. */
bool boopie_ss_aead_init(boopie_ss_aead_t *a, boopie_ss_cipher_t c, const uint8_t *key, const uint8_t *salt);
void boopie_ss_aead_free(boopie_ss_aead_t *a);

/* Seals n (1..BOOPIE_SS_PAYLOAD_MAX) bytes as one chunk into out (room for
 * n + 2 + 2 tags); returns its size, or 0. */
size_t boopie_ss_seal(boopie_ss_aead_t *a, const uint8_t *in, size_t n, uint8_t *out);

/*
 * 2022: what follows the salt in a request: an identity header for each key
 * but the last (aes only), then the sealed fixed header (now, in Unix
 * seconds) and the sealed variable one (the address and `payload`, which may
 * be empty). `up` is the request's stream, set up with the user's key.
 * Returns the length in out (room for 16 * keys + 2 * 16 + 11 + an + 2 + pn
 * + 900), or 0.
 */
size_t boopie_ss_2022_request(boopie_ss_aead_t *up, const uint8_t keys[][BOOPIE_SS_KEY_MAX], int nkeys,
                              const uint8_t *salt, uint64_t now, const uint8_t *addr, size_t an,
                              const uint8_t *payload, size_t pn, uint8_t *out);

/*
 * Reading a stream: feed what arrives (after the salt) and take out the
 * payloads. A decoder keeps a chunk's worth of bytes it hasn't opened yet.
 */
typedef struct {
    boopie_ss_aead_t aead;
    uint8_t buf[BOOPIE_SS_OPEN_MAX + BOOPIE_SS_TAG];
    size_t have;            /* bytes in buf */
    size_t want_payload;    /* the open chunk's payload length, 0 before its length is read */
    /* 2022: the server's header comes first, echoing our salt. */
    bool header;            /* still to read */
    uint8_t request_salt[BOOPIE_SS_KEY_MAX];
    uint64_t now;
} boopie_ss_decoder_t;

/* A 2022 reply: expect its header first (after aead is set up), checked
 * against our request's salt and the time. */
void boopie_ss_decoder_expect_2022(boopie_ss_decoder_t *d, const uint8_t *request_salt, uint64_t now);

/*
 * Takes up to n bytes of `in` (*used of them) and, once a whole chunk is in,
 * opens its payload into out (room for BOOPIE_SS_OPEN_MAX): returns its
 * length, 0 if more bytes are needed, -1 if the stream is broken (a bad tag:
 * the wrong password or a tampered stream).
 */
int boopie_ss_decode(boopie_ss_decoder_t *d, const uint8_t *in, size_t n, size_t *used, uint8_t *out);

/* The address a stream opens with, SOCKS style: a domain and a port.
 * Returns its length in out (room for 4 + 255), or 0. */
size_t boopie_ss_address(const char *host, uint16_t port, uint8_t *out);
