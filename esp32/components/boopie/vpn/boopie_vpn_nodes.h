/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * VPN nodes from a subscription (docs/boopie-interaction.md): Shadowsocks
 * only, no plugins. Reads what subscriptions serve:
 *   - ss:// links, one a line, SIP002 (ss://base64(method:password)@host:port#name)
 *     or the older ss://base64(method:password@host:port)#name;
 *   - the same lines base64-encoded as a whole (most subscriptions);
 *   - Clash's YAML, its `proxies:` of `type: ss`.
 * Nodes with a cipher Boopie lacks (the old stream ciphers, 2022's chacha8)
 * or a 2022 key that doesn't decode are kept, marked so the list can say so. Plain C: the host tests run it.
 */

#define BOOPIE_VPN_NAME_MAX 48
#define BOOPIE_VPN_HOST_MAX 64
#define BOOPIE_VPN_PASS_MAX 96
#define BOOPIE_VPN_CIPHER_MAX 32

typedef struct {
    char name[BOOPIE_VPN_NAME_MAX];
    char host[BOOPIE_VPN_HOST_MAX];
    char password[BOOPIE_VPN_PASS_MAX];
    char cipher[BOOPIE_VPN_CIPHER_MAX];   /* as the node names it */
    uint16_t port;
    bool supported;                       /* a cipher Boopie has */
} boopie_vpn_node_t;

/* An entry that only carries a line of text (traffic left, expiry), not a
 * server: never usable. */
bool boopie_vpn_is_info(const boopie_vpn_node_t *node);

/* Parses one ss:// link into *node; false if it isn't one Boopie can use
 * (not ss://, broken, or with a plugin). */
bool boopie_vpn_parse_link(const char *link, size_t n, boopie_vpn_node_t *node);

/* Parses a subscription's body: up to max nodes into out; returns how many. */
int boopie_vpn_parse(const char *text, size_t n, boopie_vpn_node_t *out, int max);

/* Base64 (standard or URL-safe, padding optional, whitespace skipped) into
 * out (room for n * 3 / 4 + 3); returns the length, or -1 if it isn't base64. */
int boopie_vpn_base64(const char *in, size_t n, uint8_t *out);
