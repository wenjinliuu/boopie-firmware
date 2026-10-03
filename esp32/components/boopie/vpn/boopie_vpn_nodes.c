/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* memmem */
#endif
#include "boopie_vpn_nodes.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_ss.h"

static int b64_value(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

int boopie_vpn_base64(const char *in, size_t n, uint8_t *out)
{
    uint32_t acc = 0;
    int bits = 0, len = 0;
    for (size_t i = 0; i < n; i++) {
        int c = (unsigned char)in[i];
        if (c == '=' ) {
            break;
        }
        if (isspace(c)) {
            continue;
        }
        int v = b64_value(c);
        if (v < 0) {
            return -1;
        }
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[len++] = (uint8_t)(acc >> bits);
        }
    }
    return len;
}

static int hexv(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Percent-decodes n bytes of in into out (cap), NUL-terminated; false if it doesn't fit. */
static bool unpercent(const char *in, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        int c = (unsigned char)in[i];
        if (c == '%' && i + 2 < n && hexv(in[i + 1]) >= 0 && hexv(in[i + 2]) >= 0) {
            c = hexv(in[i + 1]) * 16 + hexv(in[i + 2]);
            i += 2;
        }
        if (o + 1 >= cap) {
            return false;
        }
        out[o++] = (char)c;
    }
    out[o] = '\0';
    return true;
}

static bool copy(char *dst, size_t cap, const char *src, size_t n)
{
    if (n >= cap) {
        return false;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
    return true;
}

/* "method:password" into the node. */
static bool set_userinfo(boopie_vpn_node_t *node, const char *s, size_t n)
{
    const char *colon = memchr(s, ':', n);
    if (!colon) {
        return false;
    }
    return copy(node->cipher, sizeof node->cipher, s, colon - s)
        && copy(node->password, sizeof node->password, colon + 1, s + n - colon - 1);
}

/* "host:port" (or "[v6]:port") into the node. */
static bool set_hostport(boopie_vpn_node_t *node, const char *s, size_t n)
{
    const char *colon = NULL;
    for (size_t i = n; i-- > 0;) {
        if (s[i] == ':') {
            colon = s + i;
            break;
        }
    }
    if (!colon || colon == s) {
        return false;
    }
    const char *h = s;
    size_t hn = colon - s;
    if (hn >= 2 && h[0] == '[' && h[hn - 1] == ']') {
        h++;
        hn -= 2;
    }
    char port[8];
    if (!copy(port, sizeof port, colon + 1, s + n - colon - 1) || !copy(node->host, sizeof node->host, h, hn)) {
        return false;
    }
    char *end;
    long p = strtol(port, &end, 10);
    if (*end || p <= 0 || p > 65535) {
        return false;
    }
    node->port = (uint16_t)p;
    return true;
}

static void finish(boopie_vpn_node_t *node)
{
    node->supported = boopie_ss_cipher(node->cipher) != BOOPIE_SS_UNSUPPORTED;
    if (!node->name[0]) {
        snprintf(node->name, sizeof node->name, "%.40s:%u", node->host, node->port);
    }
}

bool boopie_vpn_parse_link(const char *link, size_t n, boopie_vpn_node_t *node)
{
    memset(node, 0, sizeof *node);
    while (n && isspace((unsigned char)*link)) {
        link++, n--;
    }
    while (n && isspace((unsigned char)link[n - 1])) {
        n--;
    }
    if (n < 6 || strncmp(link, "ss://", 5) != 0) {
        return false;
    }
    const char *p = link + 5, *end = link + n;
    const char *hash = memchr(p, '#', end - p);
    if (hash) {
        unpercent(hash + 1, end - hash - 1, node->name, sizeof node->name);
        end = hash;
    }
    /* Query (?plugin=...) and a trailing slash. */
    const char *q = memchr(p, '?', end - p);
    if (q) {
        if (memmem(q, end - q, "plugin=", 7)) {
            return false;   /* no plugins */
        }
        end = q;
    }
    if (end > p && end[-1] == '/') {
        end--;
    }
    const char *at = NULL;
    for (const char *c = end; c-- > p;) {
        if (*c == '@') {
            at = c;
            break;
        }
    }
    char buf[256];
    if (at) {
        /* SIP002: userinfo is base64url, or percent-encoded method:password (SS2022). */
        uint8_t dec[192];
        int dn = boopie_vpn_base64(p, at - p, dec);
        if (dn > 0 && memchr(dec, ':', dn) && dn < (int)sizeof buf) {
            if (!set_userinfo(node, (const char *)dec, dn)) {
                return false;
            }
        } else {
            if (!unpercent(p, at - p, buf, sizeof buf) || !set_userinfo(node, buf, strlen(buf))) {
                return false;
            }
        }
        if (!set_hostport(node, at + 1, end - at - 1)) {
            return false;
        }
    } else {
        /* Legacy: the whole method:password@host:port in base64. */
        int dn = boopie_vpn_base64(p, end - p, (uint8_t *)buf);
        if (dn <= 0 || dn >= (int)sizeof buf) {
            return false;
        }
        buf[dn] = '\0';
        char *a = strrchr(buf, '@');
        if (!a || !set_userinfo(node, buf, a - buf) || !set_hostport(node, a + 1, strlen(a + 1))) {
            return false;
        }
    }
    finish(node);
    return true;
}

/* ---- Clash YAML: just enough for proxies of type ss ---- */

/* A value, unquoted, into out. */
static void yaml_value(const char *s, size_t n, char *out, size_t cap)
{
    while (n && (*s == ' ' || *s == '\t')) {
        s++, n--;
    }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        n--;
    }
    if (n >= 2 && (s[0] == '"' || s[0] == '\'') && s[n - 1] == s[0]) {
        s++;
        n -= 2;
    }
    copy(out, cap, s, n < cap ? n : cap - 1);
}

/* One "key: value" into the node being read; type says whether it's ss. */
static void yaml_field(const char *k, size_t kn, const char *v, size_t vn, boopie_vpn_node_t *node, bool *is_ss)
{
    char val[BOOPIE_VPN_PASS_MAX];
    yaml_value(v, vn, val, sizeof val);
    while (kn && (*k == ' ' || *k == '-' || *k == '{' || *k == ',')) {
        k++, kn--;
    }
    while (kn && k[kn - 1] == ' ') {
        kn--;
    }
#define KEY(s) (kn == sizeof(s) - 1 && memcmp(k, s, kn) == 0)
    if (KEY("name")) {
        copy(node->name, sizeof node->name, val, strlen(val) < sizeof node->name ? strlen(val) : sizeof node->name - 1);
    } else if (KEY("type")) {
        *is_ss = strcmp(val, "ss") == 0;
    } else if (KEY("server")) {
        copy(node->host, sizeof node->host, val, strlen(val) < sizeof node->host ? strlen(val) : 0);
    } else if (KEY("port")) {
        node->port = (uint16_t)atoi(val);
    } else if (KEY("cipher")) {
        copy(node->cipher, sizeof node->cipher, val, strlen(val) < sizeof node->cipher ? strlen(val) : 0);
    } else if (KEY("password")) {
        copy(node->password, sizeof node->password, val, strlen(val));
    } else if (KEY("plugin")) {
        *is_ss = false;   /* no plugins */
    }
#undef KEY
}

/* "{ name: a, type: ss, ... }" on one line. */
static void yaml_flow(const char *s, size_t n, boopie_vpn_node_t *node, bool *is_ss)
{
    const char *end = s + n, *field = s;
    bool quote = false;
    char q = 0;
    for (const char *c = s; c <= end; c++) {
        if (c < end && (*c == '"' || *c == '\'') && (!quote || *c == q)) {
            quote = !quote;
            q = *c;
            continue;
        }
        if (c == end || (!quote && (*c == ',' || *c == '}'))) {
            const char *colon = memchr(field, ':', c - field);
            if (colon) {
                yaml_field(field, colon - field, colon + 1, c - colon - 1, node, is_ss);
            }
            field = c + 1;
        }
    }
}

static bool node_ok(const boopie_vpn_node_t *node)
{
    return node->host[0] && node->port && node->cipher[0];
}

static int parse_clash(const char *text, size_t n, boopie_vpn_node_t *out, int max)
{
    const char *p = text, *end = text + n;
    const char *start = memmem(text, n, "proxies:", 8);
    if (!start) {
        return 0;
    }
    p = start + 8;
    int count = 0;
    boopie_vpn_node_t node;
    bool open = false, is_ss = false;
    int item_indent = -1;
    while (p < end && count < max) {
        const char *eol = memchr(p, '\n', end - p);
        if (!eol) {
            eol = end;
        }
        const char *s = p;
        while (s < eol && *s == ' ') {
            s++;
        }
        int indent = (int)(s - p);
        if (s < eol && *s != '#') {
            if (indent == 0 && *s != '-') {
                break;   /* the next top-level key: proxies are over */
            }
            if (*s == '-' && (item_indent < 0 || indent <= item_indent)) {
                if (open && is_ss && node_ok(&node)) {
                    finish(&node);
                    out[count++] = node;
                }
                memset(&node, 0, sizeof node);
                open = true;
                is_ss = false;
                item_indent = indent;
                const char *brace = memchr(s, '{', eol - s);
                if (brace) {
                    yaml_flow(brace + 1, eol - brace - 1, &node, &is_ss);
                } else {
                    const char *colon = memchr(s, ':', eol - s);
                    if (colon) {
                        yaml_field(s + 1, colon - s - 1, colon + 1, eol - colon - 1, &node, &is_ss);
                    }
                }
            } else if (open) {
                const char *colon = memchr(s, ':', eol - s);
                if (colon) {
                    yaml_field(s, colon - s, colon + 1, eol - colon - 1, &node, &is_ss);
                }
            }
        }
        p = eol + 1;
    }
    if (open && is_ss && node_ok(&node) && count < max) {
        finish(&node);
        out[count++] = node;
    }
    return count;
}

static int parse_lines(const char *text, size_t n, boopie_vpn_node_t *out, int max)
{
    int count = 0;
    const char *p = text, *end = text + n;
    while (p < end && count < max) {
        const char *eol = p;
        while (eol < end && *eol != '\n' && *eol != '\r' && *eol != ' ') {
            eol++;
        }
        if (eol > p && boopie_vpn_parse_link(p, eol - p, &out[count])) {
            count++;
        }
        p = eol + 1;
    }
    return count;
}

int boopie_vpn_parse(const char *text, size_t n, boopie_vpn_node_t *out, int max)
{
    if (memmem(text, n, "proxies:", 8)) {
        return parse_clash(text, n, out, max);
    }
    if (memmem(text, n, "://", 3)) {
        return parse_lines(text, n, out, max);
    }
    uint8_t *dec = malloc(n * 3 / 4 + 4);
    if (!dec) {
        return 0;
    }
    int dn = boopie_vpn_base64(text, n, dec);
    int count = dn > 0 ? parse_lines((const char *)dec, dn, out, max) : 0;
    free(dec);
    return count;
}
