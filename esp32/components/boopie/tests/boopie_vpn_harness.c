/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Drives Boopie's Shadowsocks code for test_boopie_vpn.py:
 *   seal CIPHER PASSWORD SALTHEX   stdin is plaintext: prints the stream
 *                                  (salt, then chunks) as hex
 *   open CIPHER PASSWORD STEP      stdin is a stream as hex: prints its
 *                                  plaintext, fed STEP bytes at a time;
 *                                  exits 3 on a bad tag
 *   parse                          stdin is a subscription: prints its nodes
 *                                  as JSON lines
 *   address HOST PORT              prints the address header as hex */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_ss.h"
#include "boopie_vpn_nodes.h"

static uint8_t s_in[1 << 20];

static size_t read_all(void)
{
    return fread(s_in, 1, sizeof s_in, stdin);
}

static int unhex(const char *h, uint8_t *out)
{
    int n = 0;
    while (h[0] && h[1]) {
        unsigned v;
        if (sscanf(h, "%2x", &v) != 1) {
            break;
        }
        out[n++] = (uint8_t)v;
        h += 2;
    }
    return n;
}

static void hex(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        printf("%02x", p[i]);
    }
}

static void json_str(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') {
            putchar('\\');
        }
        putchar(*s);
    }
    putchar('"');
}

int main(int argc, char **argv)
{
    if (argc > 4 && !strcmp(argv[1], "seal")) {
        boopie_ss_cipher_t c = boopie_ss_cipher(argv[2]);
        uint8_t key[BOOPIE_SS_KEY_MAX], salt[BOOPIE_SS_KEY_MAX];
        int sn = unhex(argv[4], salt);
        boopie_ss_aead_t a;
        if (sn != boopie_ss_key_len(c) || !boopie_ss_password_key(c, argv[3], key)
            || !boopie_ss_aead_init(&a, c, key, salt)) {
            return 2;
        }
        size_t n = read_all();
        hex(salt, sn);
        static uint8_t out[BOOPIE_SS_CHUNK_MAX];
        for (size_t off = 0; off < n;) {
            size_t take = n - off < BOOPIE_SS_PAYLOAD_MAX ? n - off : BOOPIE_SS_PAYLOAD_MAX;
            size_t m = boopie_ss_seal(&a, s_in + off, take, out);
            if (!m) {
                return 2;
            }
            hex(out, m);
            off += take;
        }
        boopie_ss_aead_free(&a);
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "open")) {
        boopie_ss_cipher_t c = boopie_ss_cipher(argv[2]);
        int step = atoi(argv[4]);
        uint8_t key[BOOPIE_SS_KEY_MAX];
        size_t hn = read_all();
        s_in[hn] = 0;
        static uint8_t bin[1 << 19], out[BOOPIE_SS_PAYLOAD_MAX];
        int n = unhex((const char *)s_in, bin);
        int kl = boopie_ss_key_len(c);
        static boopie_ss_decoder_t d;
        if (!kl || n < kl || !boopie_ss_password_key(c, argv[3], key) || !boopie_ss_aead_init(&d.aead, c, key, bin)) {
            return 2;
        }
        for (int off = kl; off < n;) {
            size_t avail = (size_t)(n - off) < (size_t)step ? (size_t)(n - off) : (size_t)step;
            while (avail) {
                size_t used;
                int r = boopie_ss_decode(&d, bin + off, avail, &used, out);
                if (r < 0) {
                    return 3;
                }
                fwrite(out, 1, r, stdout);
                off += used;
                avail -= used;
                if (!used && !r) {
                    break;
                }
            }
        }
        boopie_ss_aead_free(&d.aead);
        return d.have || d.want_payload ? 4 : 0;   /* a chunk left half read */
    }
    if (argc > 1 && !strcmp(argv[1], "parse")) {
        size_t n = read_all();
        static boopie_vpn_node_t nodes[64];
        int count = boopie_vpn_parse((const char *)s_in, n, nodes, 64);
        for (int i = 0; i < count; i++) {
            printf("{\"name\":");
            json_str(nodes[i].name);
            printf(",\"host\":");
            json_str(nodes[i].host);
            printf(",\"port\":%u,\"cipher\":", nodes[i].port);
            json_str(nodes[i].cipher);
            printf(",\"password\":");
            json_str(nodes[i].password);
            printf(",\"supported\":%s}\n", nodes[i].supported ? "true" : "false");
        }
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "address")) {
        uint8_t out[300];
        size_t n = boopie_ss_address(argv[2], (uint16_t)atoi(argv[3]), out);
        hex(out, n);
        return n ? 0 : 1;
    }
    return 2;
}
