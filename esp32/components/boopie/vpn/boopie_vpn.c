/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_vpn.h"

#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/api.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs.h"

#include "boopie_ss.h"
#include "boopie_store.h"

static const char *TAG = "boopie_vpn";

#define NVS_NS "boopie_net"
#define NVS_SUB "sub"          /* the subscription, saved by phone setup */
#define NVS_ON "vpn_on"
#define NVS_NODE "vpn_node"
#define NODES_FILE "/data/vpn_nodes.bin"
#define NODES_MAGIC 0x564e5031u   /* "VNP1" */

#define RELAY_PORT 443
#define CONNS_MAX 4
#define HELLO_MAX 2048          /* a ClientHello is a few hundred bytes */
#define CONNECT_TIMEOUT_MS 6000
#define SUB_MAX (256 * 1024)
#define ACTIVE_S 30             /* "lately", for the status line */

/* The names that go through the VPN: Muse's, and its server if it's moved. */
static const char *const DOMAINS[] = { "muse.ai", "metaaivm.com" };
static char s_extra_host[64];

static SemaphoreHandle_t s_lock;
static boopie_vpn_node_t *s_nodes;      /* in PSRAM */
static int s_count, s_current = -1;
static int16_t s_latency[BOOPIE_VPN_NODES_MAX];
static atomic_bool s_on, s_relay_up;
static atomic_int_fast64_t s_active_us;
static atomic_int s_busy;
static char s_msg[64];
static TaskHandle_t s_relay;

/* ---- settings and nodes ---- */

static void save_settings(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, NVS_ON, atomic_load(&s_on));
    nvs_set_i16(h, NVS_NODE, (int16_t)s_current);
    nvs_commit(h);
    nvs_close(h);
}

static void save_nodes(void)
{
    FILE *f = fopen(NODES_FILE, "wb");
    if (!f) {
        return;
    }
    uint32_t head[2] = { NODES_MAGIC, (uint32_t)s_count };
    fwrite(head, sizeof head, 1, f);
    fwrite(s_nodes, sizeof *s_nodes, s_count, f);
    fclose(f);
}

static void load_nodes(void)
{
    FILE *f = fopen(NODES_FILE, "rb");
    if (!f) {
        return;
    }
    uint32_t head[2];
    if (fread(head, sizeof head, 1, f) == 1 && head[0] == NODES_MAGIC && head[1] <= BOOPIE_VPN_NODES_MAX) {
        s_count = (int)fread(s_nodes, sizeof *s_nodes, head[1], f);
    }
    fclose(f);
}

static void set_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    vsnprintf(s_msg, sizeof s_msg, fmt, ap);
    xSemaphoreGive(s_lock);
    va_end(ap);
}

/* ---- the resolve hook: Muse's names are the board ---- */

static bool proxied(const char *name)
{
    size_t n = strlen(name);
    for (size_t i = 0; i < sizeof DOMAINS / sizeof DOMAINS[0]; i++) {
        size_t d = strlen(DOMAINS[i]);
        if (n >= d && strcasecmp(name + n - d, DOMAINS[i]) == 0 && (n == d || name[n - d - 1] == '.')) {
            return true;
        }
    }
    return s_extra_host[0] && strcasecmp(name, s_extra_host) == 0;
}

/* lwip asks this before it looks a name up (CONFIG_LWIP_HOOK_NETCONN_EXT_RESOLVE_CUSTOM). */
int lwip_hook_netconn_external_resolve(const char *name, ip_addr_t *addr, u8_t addrtype, err_t *err)
{
    (void)addrtype;
    if (!atomic_load(&s_on) || !atomic_load(&s_relay_up) || !name || !proxied(name)) {
        return 0;
    }
    ip_addr_set_ip4_u32(addr, PP_HTONL(INADDR_LOOPBACK));
    *err = ERR_OK;
    return 1;
}

/* ---- the relay ---- */

typedef struct {
    int cfd, sfd;           /* Muse's side (local), the node's */
    bool tunnel;            /* past the hello: relaying */
    uint8_t *hello;         /* the ClientHello so far */
    size_t hello_n;
    boopie_ss_aead_t up;    /* to the node */
    boopie_ss_decoder_t *down;
    uint8_t key[BOOPIE_SS_KEY_MAX];
    boopie_ss_cipher_t cipher;
    uint8_t salt_in[BOOPIE_SS_KEY_MAX];
    size_t salt_have;
    uint8_t *buf;           /* a sealed chunk going up, or a payload coming down */
} conn_t;

static conn_t s_conns[CONNS_MAX];
static uint8_t *s_up_in, *s_down_in;   /* read buffers, in PSRAM */
#define DOWN_IN 4096

static void conn_close(conn_t *c)
{
    if (c->cfd >= 0) {
        close(c->cfd);
    }
    if (c->sfd >= 0) {
        close(c->sfd);
    }
    boopie_ss_aead_free(&c->up);
    if (c->down) {
        boopie_ss_aead_free(&c->down->aead);
    }
    free(c->hello);
    free(c->down);
    free(c->buf);
    memset(c->key, 0, sizeof c->key);
    memset(c, 0, sizeof *c);
    c->cfd = c->sfd = -1;
}

/* The name a TLS ClientHello asks for (its SNI), into host. */
static int sni(const uint8_t *p, size_t n, char *host, size_t cap)
{
    if (n < 5) {
        return 0;   /* more, please */
    }
    if (p[0] != 0x16) {
        return -1;   /* not TLS */
    }
    size_t rec = (size_t)p[3] << 8 | p[4];
    if (rec + 5 > HELLO_MAX) {
        return -1;
    }
    if (n < rec + 5) {
        return 0;
    }
    size_t i = 5, end = 5 + rec;
    if (p[i] != 1) {
        return -1;   /* not a ClientHello */
    }
    i += 4 + 2 + 32;                                   /* header, version, random */
    if (i >= end) return -1;
    i += 1 + p[i];                                     /* session id */
    if (i + 2 > end) return -1;
    i += 2 + ((size_t)p[i] << 8 | p[i + 1]);           /* cipher suites */
    if (i >= end) return -1;
    i += 1 + p[i];                                     /* compression */
    if (i + 2 > end) return -1;
    size_t ext_end = i + 2 + ((size_t)p[i] << 8 | p[i + 1]);
    i += 2;
    if (ext_end > end) return -1;
    while (i + 4 <= ext_end) {
        size_t type = (size_t)p[i] << 8 | p[i + 1], len = (size_t)p[i + 2] << 8 | p[i + 3];
        i += 4;
        if (i + len > ext_end) return -1;
        if (type == 0 && len >= 5 && p[i + 2] == 0) {   /* server_name: host_name */
            size_t hn = (size_t)p[i + 3] << 8 | p[i + 4];
            if (hn == 0 || hn >= cap || i + 5 + hn > i + len) return -1;
            memcpy(host, p + i + 5, hn);
            host[hn] = '\0';
            return 1;
        }
        i += len;
    }
    return -1;
}

static bool send_all(int fd, const uint8_t *p, size_t n)
{
    while (n) {
        int w = send(fd, p, n, 0);
        if (w <= 0) {
            return false;
        }
        p += w;
        n -= w;
    }
    return true;
}

/* A TCP connection to host:port, within a timeout. */
static int dial(const char *host, uint16_t port, int timeout_ms)
{
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, *res = NULL;
    char ps[8];
    snprintf(ps, sizeof ps, "%u", port);
    if (getaddrinfo(host, ps, &hints, &res) != 0 || !res) {
        return -1;
    }
    int fd = socket(res->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int r = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (r < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    fd_set w;
    FD_ZERO(&w);
    FD_SET(fd, &w);
    struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = timeout_ms % 1000 * 1000 };
    int err = 0;
    socklen_t len = sizeof err;
    if (select(fd + 1, NULL, &w, NULL, &tv) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);
    struct timeval to = { .tv_sec = 15 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

/* The hello is in: open the tunnel and send it the address and the hello. */
static bool open_tunnel(conn_t *c, const char *host)
{
    boopie_vpn_node_t node;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool have = s_current >= 0 && s_current < s_count;
    if (have) {
        node = s_nodes[s_current];
    }
    xSemaphoreGive(s_lock);
    if (!have || !node.supported) {
        ESP_LOGW(TAG, "%s: no usable node chosen; import a subscription", host);
        return false;
    }
    c->cipher = boopie_ss_cipher(node.cipher);
    int kl = boopie_ss_key_len(c->cipher);
    uint8_t salt[BOOPIE_SS_KEY_MAX];
    esp_fill_random(salt, kl);
    bool is2022 = boopie_ss_is_2022(c->cipher);
    uint8_t keys[BOOPIE_SS_2022_KEYS_MAX][BOOPIE_SS_KEY_MAX];
    int nkeys = is2022 ? boopie_ss_2022_keys(c->cipher, node.password, keys, BOOPIE_SS_2022_KEYS_MAX) : 0;
    bool ok = boopie_ss_password_key(c->cipher, node.password, c->key) && boopie_ss_aead_init(&c->up, c->cipher, c->key, salt);
    memset(node.password, 0, sizeof node.password);
    if (!ok || (is2022 && !nkeys)) {
        memset(keys, 0, sizeof keys);
        return false;
    }
    if (is2022 && time(NULL) < 1700000000) {
        ESP_LOGW(TAG, "2022 nodes need the time, and the clock isn't set yet");
        memset(keys, 0, sizeof keys);
        return false;
    }
    c->sfd = dial(node.host, node.port, CONNECT_TIMEOUT_MS);
    if (c->sfd < 0) {
        ESP_LOGW(TAG, "can't reach the node %s", node.name);
        return false;
    }
    /* The address and the hello together: what the node sees first. */
    uint8_t *first = c->buf;   /* plaintext here, sealed after it */
    size_t an = boopie_ss_address(host, RELAY_PORT, first);
    if (!an || an + c->hello_n > BOOPIE_SS_PAYLOAD_MAX) {
        memset(keys, 0, sizeof keys);
        return false;
    }
    memcpy(first + an, c->hello, c->hello_n);
    uint8_t *sealed = heap_caps_malloc(BOOPIE_SS_CHUNK_MAX + BOOPIE_SS_KEY_MAX + 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sealed) {
        memset(keys, 0, sizeof keys);
        return false;
    }
    memcpy(sealed, salt, kl);
    size_t m;
    if (is2022) {
        /* The request's headers carry the address and the hello; the reply
         * must echo this salt, within half a minute of now. */
        uint64_t now = (uint64_t)time(NULL);
        m = boopie_ss_2022_request(&c->up, (const uint8_t (*)[BOOPIE_SS_KEY_MAX])keys, nkeys, salt, now, first, an,
                                   c->hello, c->hello_n, sealed + kl);
        boopie_ss_decoder_expect_2022(c->down, salt, now);
    } else {
        m = boopie_ss_seal(&c->up, first, an + c->hello_n, sealed + kl);
    }
    memset(keys, 0, sizeof keys);
    ok = m && send_all(c->sfd, sealed, kl + m);
    free(sealed);
    free(c->hello);
    c->hello = NULL;
    c->tunnel = ok;
    ESP_LOGI(TAG, "%s through %s", host, node.name);
    return ok;
}

/* Muse's side has bytes. */
static bool from_client(conn_t *c)
{
    if (!c->tunnel) {
        int r = recv(c->cfd, c->hello + c->hello_n, HELLO_MAX - c->hello_n, 0);
        if (r <= 0) {
            return false;
        }
        c->hello_n += r;
        char host[256];
        int s = sni(c->hello, c->hello_n, host, sizeof host);
        if (s < 0 || (s == 0 && c->hello_n >= HELLO_MAX)) {
            return false;
        }
        return s == 0 || open_tunnel(c, host);
    }
    int r = recv(c->cfd, s_up_in, BOOPIE_SS_PAYLOAD_MAX, 0);
    if (r <= 0) {
        return false;
    }
    size_t m = boopie_ss_seal(&c->up, s_up_in, r, c->buf);
    return m && send_all(c->sfd, c->buf, m);
}

/* The node has bytes: its salt first, then chunks to open. */
static bool from_server(conn_t *c)
{
    uint8_t *in = s_down_in;
    int r = recv(c->sfd, in, DOWN_IN, 0);
    if (r <= 0) {
        return false;
    }
    size_t off = 0;
    int kl = boopie_ss_key_len(c->cipher);
    if (c->salt_have < (size_t)kl) {
        size_t take = kl - c->salt_have < (size_t)r ? kl - c->salt_have : (size_t)r;
        memcpy(c->salt_in + c->salt_have, in, take);
        c->salt_have += take;
        off = take;
        if (c->salt_have == (size_t)kl) {
            /* Set up the reading side, keeping a 2022 reply's expectations. */
            boopie_ss_aead_t a;
            if (!boopie_ss_aead_init(&a, c->cipher, c->key, c->salt_in)) {
                return false;
            }
            c->down->aead = a;
        }
    }
    while (off < (size_t)r) {
        size_t used;
        int n = boopie_ss_decode(c->down, in + off, r - off, &used, c->buf);
        if (n < 0) {
            ESP_LOGW(TAG, "the node's stream doesn't open: wrong password?");
            return false;
        }
        if (n > 0 && !send_all(c->cfd, c->buf, n)) {
            return false;
        }
        off += used;
        atomic_store(&s_active_us, esp_timer_get_time());
    }
    return true;
}

static void relay_task(void *arg)
{
    (void)arg;
    /* VPN on from the last run starts this at boot, before Link has brought
     * lwIP up: a socket before then trips an assert in its core lock. */
    while (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    for (int i = 0; i < CONNS_MAX; i++) {
        s_conns[i].cfd = s_conns[i].sfd = -1;
    }
    s_up_in = heap_caps_malloc(BOOPIE_SS_PAYLOAD_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_down_in = heap_caps_malloc(DOWN_IN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int lfd = s_up_in && s_down_in ? socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) : -1;
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(RELAY_PORT),
                             .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (lfd < 0 || bind(lfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(lfd, CONNS_MAX) < 0) {
        ESP_LOGE(TAG, "relay: can't listen on 127.0.0.1:%d", RELAY_PORT);
        set_msg("VPN 启动失败");
        if (lfd >= 0) {
            close(lfd);
        }
        free(s_up_in);
        free(s_down_in);
        s_relay = NULL;
        vTaskDeleteWithCaps(NULL);
        return;
    }
    atomic_store(&s_relay_up, true);
    ESP_LOGI(TAG, "relay up");
    while (true) {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(lfd, &r);
        int top = lfd;
        for (int i = 0; i < CONNS_MAX; i++) {
            conn_t *c = &s_conns[i];
            if (c->cfd >= 0) {
                FD_SET(c->cfd, &r);
                top = c->cfd > top ? c->cfd : top;
            }
            if (c->sfd >= 0) {
                FD_SET(c->sfd, &r);
                top = c->sfd > top ? c->sfd : top;
            }
        }
        struct timeval tv = { .tv_sec = 1 };
        if (select(top + 1, &r, NULL, NULL, &tv) <= 0) {
            continue;
        }
        if (FD_ISSET(lfd, &r)) {
            int fd = accept(lfd, NULL, NULL);
            conn_t *c = NULL;
            for (int i = 0; fd >= 0 && i < CONNS_MAX && !c; i++) {
                c = s_conns[i].cfd < 0 ? &s_conns[i] : NULL;
            }
            if (c) {
                c->cfd = fd;
                c->hello = heap_caps_malloc(HELLO_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                c->down = heap_caps_calloc(1, sizeof *c->down, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                c->buf = heap_caps_malloc(BOOPIE_SS_OPEN_MAX + BOOPIE_SS_TAG, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!c->hello || !c->down || !c->buf) {
                    conn_close(c);
                }
            } else if (fd >= 0) {
                close(fd);   /* full */
            }
        }
        for (int i = 0; i < CONNS_MAX; i++) {
            conn_t *c = &s_conns[i];
            bool ok = true;
            if (c->cfd >= 0 && FD_ISSET(c->cfd, &r)) {
                ok = from_client(c);
            }
            if (ok && c->sfd >= 0 && FD_ISSET(c->sfd, &r)) {
                ok = from_server(c);
            }
            if (!ok) {
                conn_close(c);
            }
        }
    }
}

static void start_relay(void)
{
    if (!s_relay) {
        xTaskCreateWithCaps(relay_task, "boopie_vpn", 6144, NULL, 5, &s_relay, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
}

/* ---- the subscription and the speed test, in the background ---- */

/* The bundle's check, with the chain it saw in the log: what a failure needs
 * to tell a missing intermediate from a root the bundle hasn't got. Only
 * names, which the certificates make public anyway. */
static int (*s_bundle_vrfy)(void *, mbedtls_x509_crt *, int, uint32_t *);
static void *s_bundle_vrfy_arg;

static int logged_verify(void *arg, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    (void)arg;
    int r = s_bundle_vrfy ? s_bundle_vrfy(s_bundle_vrfy_arg, crt, depth, flags) : 0;
    char subj[96], iss[96];
    if (mbedtls_x509_dn_gets(subj, sizeof subj, &crt->subject) < 0) {
        strlcpy(subj, "?", sizeof subj);
    }
    if (mbedtls_x509_dn_gets(iss, sizeof iss, &crt->issuer) < 0) {
        strlcpy(iss, "?", sizeof iss);
    }
    ESP_LOGI(TAG, "subscription cert %d: %s, issued by %s%s", depth, subj, iss, *flags ? " (not trusted)" : "");
    return r;
}

static esp_err_t bundle_attach_logged(void *conf)
{
    esp_err_t err = esp_crt_bundle_attach(conf);
    if (err == ESP_OK && conf) {
        mbedtls_ssl_config *c = conf;
        s_bundle_vrfy = c->MBEDTLS_PRIVATE(f_vrfy);
        s_bundle_vrfy_arg = c->MBEDTLS_PRIVATE(p_vrfy);
        mbedtls_ssl_conf_verify(c, logged_verify, NULL);
    }
    return err;
}

/* Just the host, for the log: the rest of a subscription URL is its secret. */
static void url_host(const char *url, char *out, size_t cap)
{
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t n = strcspn(p, "/?#:@");
    if (p[n] == '@') {   /* user:pass@host: skip the credentials */
        p += n + 1;
        n = strcspn(p, "/?#:");
    }
    snprintf(out, cap, "%.*s", (int)(n < cap ? n : cap - 1), p);
}

static char *fetch(const char *url, size_t *len, const char **why)
{
    char *body = heap_caps_malloc(SUB_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        *why = "内存不够";
        return NULL;
    }
    char host[64];
    url_host(url, host, sizeof host);
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = bundle_attach_logged,
        .user_agent = "ClashForAndroid/2.5.12",   /* some subscriptions answer clients they know */
        .max_redirection_count = 5,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    size_t n = 0;
    int status = 0;
    *why = "订阅下载失败：检查链接和网络";
    for (int hop = 0; h && hop <= 5; hop++) {
        esp_err_t err = esp_http_client_open(h, 0);
        if (err != ESP_OK) {
            int tls_err = 0, tls_flags = 0;
            esp_http_client_get_and_clear_last_tls_error(h, &tls_err, &tls_flags);
            ESP_LOGW(TAG, "subscription: can't connect to %s (%s, tls 0x%x, cert flags 0x%x)", host,
                     esp_err_to_name(err), tls_err, tls_flags);
            if (tls_flags) {
                *why = "订阅网站的证书验证不过：换备用地址试试";
            } else {
                *why = "连不上订阅网站：换备用地址试试";
            }
            break;
        }
        esp_http_client_fetch_headers(h);
        status = esp_http_client_get_status_code(h);
        /* open() doesn't follow redirects by itself (perform() does). */
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            esp_http_client_set_redirection(h);
            esp_http_client_close(h);
            continue;
        }
        int r;
        while (status == 200 && n < SUB_MAX && (r = esp_http_client_read(h, body + n, SUB_MAX - n)) > 0) {
            n += r;
        }
        if (status != 200) {
            ESP_LOGW(TAG, "subscription: %s answered HTTP %d", host, status);
            *why = status == 403 || status == 404 ? "订阅链接失效或被拒绝：重新复制一次" : "订阅网站出错：稍后再试";
            n = 0;
        } else if (!n) {
            *why = "订阅是空的";
        } else {
            ESP_LOGI(TAG, "subscription: %u bytes from %s", (unsigned)n, host);
        }
        break;
    }
    if (h) {
        esp_http_client_cleanup(h);
    }
    if (!n) {
        free(body);
        return NULL;
    }
    body[n] = '\0';
    *len = n;
    return body;
}

/* New nodes in: the current one kept if it's still there, else the first usable. */
static void apply_nodes(const boopie_vpn_node_t *got, int n)
{
    int usable = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    char current[BOOPIE_VPN_NAME_MAX] = "";
    if (s_current >= 0 && s_current < s_count) {
        strlcpy(current, s_nodes[s_current].name, sizeof current);
    }
    memcpy(s_nodes, got, n * sizeof *got);
    s_count = n;
    s_current = -1;
    for (int i = 0; i < n; i++) {
        s_latency[i] = -1;
        usable += s_nodes[i].supported;
        if (s_current < 0 && current[0] && strcmp(s_nodes[i].name, current) == 0) {
            s_current = i;   /* the same node as before, if it's still there */
        }
    }
    for (int i = 0; s_current < 0 && i < n; i++) {
        s_current = s_nodes[i].supported ? i : -1;
    }
    save_nodes();
    xSemaphoreGive(s_lock);
    save_settings();
    if (usable < n) {
        set_msg("已更新：%d 个节点，%d 个能用", n, usable);
    } else {
        set_msg("已更新：%d 个节点", n);
    }
}

static void update_task(void *arg)
{
    (void)arg;
    char *sub = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    size_t sn = 1024;
    nvs_handle_t h;
    bool have = false;
    if (sub && nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        have = nvs_get_str(h, NVS_SUB, sub, &sn) == ESP_OK && sn > 1;
        nvs_close(h);
    }
    boopie_vpn_node_t *got = heap_caps_calloc(BOOPIE_VPN_NODES_MAX, sizeof *got, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int n = 0;
    const char *why = NULL;
    if (!have) {
        why = "还没有订阅：用 手机扫码设置 导入";
    } else if (!got) {
        why = "内存不够";
    } else if (strncmp(sub, "ss://", 5) == 0) {
        n = boopie_vpn_parse(sub, strlen(sub), got, BOOPIE_VPN_NODES_MAX);   /* a node pasted itself */
    } else {
        size_t len = 0;
        char *body = fetch(sub, &len, &why);
        if (body) {
            why = NULL;
            n = boopie_vpn_parse(body, len, got, BOOPIE_VPN_NODES_MAX);
            ESP_LOGI(TAG, "subscription: %d Shadowsocks nodes", n);
            if (n == 0 && (memmem(body, len, "vmess", 5) || memmem(body, len, "trojan", 6) || memmem(body, len, "vless", 5)
                           || memmem(body, len, "hysteria", 8))) {
                why = "订阅里只有 vmess/trojan 等节点，我只支持 Shadowsocks";
            }
            memset(body, 0, len);
            free(body);
        }
    }
    if (!why && n == 0) {
        why = "订阅里没有 Shadowsocks 节点";
    }
    if (why) {
        set_msg("%s", why);
    }
    if (n > 0) {
        apply_nodes(got, n);
    }
    if (got) {
        memset(got, 0, BOOPIE_VPN_NODES_MAX * sizeof *got);
        free(got);
    }
    free(sub);
    atomic_store(&s_busy, BOOPIE_VPN_IDLE);
    vTaskDeleteWithCaps(NULL);
}

static void test_task(void *arg)
{
    (void)arg;
    int n = boopie_vpn_count(), fastest = -1, best = 0;
    for (int i = 0; i < n; i++) {
        boopie_vpn_node_t node;
        if (!boopie_vpn_node(i, &node) || !node.supported) {
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        int fd = dial(node.host, node.port, 3000);
        int ms = fd >= 0 ? (int)((esp_timer_get_time() - t0) / 1000) : -2;
        if (fd >= 0) {
            close(fd);
            if (fastest < 0 || ms < best) {
                fastest = i;
                best = ms;
            }
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (i < s_count) {
            s_latency[i] = (int16_t)(ms > 9999 ? 9999 : ms);
        }
        xSemaphoreGive(s_lock);
    }
    if (fastest >= 0) {
        set_msg("测速完成：最快 %d ms", best);
    } else {
        set_msg("所有节点都连不上");
    }
    atomic_store(&s_busy, BOOPIE_VPN_IDLE);
    vTaskDeleteWithCaps(NULL);
}

static void run(boopie_vpn_busy_t what, TaskFunction_t fn, const char *name)
{
    int idle = BOOPIE_VPN_IDLE;
    if (!s_lock || !atomic_compare_exchange_strong(&s_busy, &idle, what)) {
        return;
    }
    set_msg("%s", what == BOOPIE_VPN_UPDATING ? "正在更新订阅…" : "正在测速…");
    /* A TLS handshake (the subscription) wants more than 6 KB of stack; PSRAM is plenty. */
    if (xTaskCreateWithCaps(fn, name, 12 * 1024, NULL, 4, NULL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        atomic_store(&s_busy, BOOPIE_VPN_IDLE);
        set_msg("内存不够");
    }
}

/* ---- the API ---- */

void boopie_vpn_init(const char *extra_host)
{
    if (s_lock) {
        return;
    }
    s_lock = xSemaphoreCreateMutex();
    s_nodes = heap_caps_calloc(BOOPIE_VPN_NODES_MAX, sizeof *s_nodes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_lock || !s_nodes) {
        return;
    }
    if (extra_host && !proxied(extra_host) && strcmp(extra_host, "") != 0) {
        strlcpy(s_extra_host, extra_host, sizeof s_extra_host);
    }
    for (int i = 0; i < BOOPIE_VPN_NODES_MAX; i++) {
        s_latency[i] = -1;
    }
    load_nodes();
    nvs_handle_t h;
    uint8_t on = 0;
    int16_t node = -1;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_ON, &on);
        nvs_get_i16(h, NVS_NODE, &node);
        nvs_close(h);
    }
    s_current = node >= 0 && node < s_count ? node : -1;
    atomic_store(&s_on, on != 0);
    if (on) {
        start_relay();
    }
}

bool boopie_vpn_on(void)
{
    return atomic_load(&s_on);
}

void boopie_vpn_set_on(bool on)
{
    if (!s_lock) {
        return;
    }
    atomic_store(&s_on, on);
    save_settings();
    if (on) {
        start_relay();
        if (boopie_vpn_count() == 0) {
            boopie_vpn_update();   /* no nodes yet: fetch them */
        }
    }
    /* Off, the relay stays up but nothing resolves to it; open
     * connections finish and new ones go direct. */
}

int boopie_vpn_count(void)
{
    if (!s_lock) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_count;
    xSemaphoreGive(s_lock);
    return n;
}

bool boopie_vpn_node(int index, boopie_vpn_node_t *out)
{
    if (!s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = index >= 0 && index < s_count;
    if (ok) {
        *out = s_nodes[index];
        memset(out->password, 0, sizeof out->password);
    }
    xSemaphoreGive(s_lock);
    return ok;
}

int boopie_vpn_current(void)
{
    return s_current;
}

void boopie_vpn_select(int index)
{
    if (!s_lock || index < 0 || index >= boopie_vpn_count()) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_current = s_nodes[index].supported ? index : s_current;
    xSemaphoreGive(s_lock);
    save_settings();
}

int boopie_vpn_latency(int index)
{
    return index >= 0 && index < BOOPIE_VPN_NODES_MAX ? s_latency[index] : -1;
}

boopie_vpn_busy_t boopie_vpn_busy(char *msg, size_t cap)
{
    if (msg && cap) {
        msg[0] = '\0';
        if (s_lock) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            strlcpy(msg, s_msg, cap);
            xSemaphoreGive(s_lock);
        }
    }
    return (boopie_vpn_busy_t)atomic_load(&s_busy);
}

void boopie_vpn_update(void)
{
    run(BOOPIE_VPN_UPDATING, update_task, "boopie_vpn_sub");
}

int boopie_vpn_import(const char *text, size_t len)
{
    if (!s_lock || !text || !len) {
        return 0;
    }
    boopie_vpn_node_t *got = heap_caps_calloc(BOOPIE_VPN_NODES_MAX, sizeof *got, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!got) {
        return 0;
    }
    int n = boopie_vpn_parse(text, len, got, BOOPIE_VPN_NODES_MAX);
    ESP_LOGI(TAG, "pasted: %d Shadowsocks nodes", n);
    if (n > 0) {
        apply_nodes(got, n);
    }
    memset(got, 0, BOOPIE_VPN_NODES_MAX * sizeof *got);
    free(got);
    return n;
}

void boopie_vpn_net_up(void)
{
    if (!s_lock || boopie_vpn_count() > 0) {
        return;
    }
    nvs_handle_t h;
    size_t n = 0;
    bool have = false;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        have = nvs_get_str(h, NVS_SUB, NULL, &n) == ESP_OK && n > 1;
        nvs_close(h);
    }
    if (have) {
        boopie_vpn_update();
    }
}

void boopie_vpn_test(void)
{
    run(BOOPIE_VPN_TESTING, test_task, "boopie_vpn_ping");
}

bool boopie_vpn_active(void)
{
    int64_t at = atomic_load(&s_active_us);
    return atomic_load(&s_on) && at && esp_timer_get_time() - at < (int64_t)ACTIVE_S * 1000000;
}
