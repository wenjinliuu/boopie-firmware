/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_setup_web.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_xiaozhi.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"

#include "boopie_avatar.h"
#include "boopie_sdk_token.h"
#include "boopie_vpn.h"
#include "muse_settings.h"
#include "muse_wifi.h"

static const char *TAG = "boopie_setup";

extern const char setup_html_start[] asm("_binary_setup_html_start");
extern const char setup_html_end[] asm("_binary_setup_html_end");

#define NVS_NS "boopie_net"
#define NVS_SUB "sub"
#define SUB_MAX 1023
#define BODY_MAX 6144        /* a subscription and the rest, URL-encoded */
#define MAX_APS 20

static httpd_handle_t s_http;
static esp_netif_t *s_ap_netif;
static TaskHandle_t s_dns_task;
static atomic_bool s_running;
static atomic_int_fast64_t s_last_us;
static atomic_uint s_saves, s_saved;

/* ---- the hotspot's DNS: every name is the board ---- */

static void dns_task(void *arg)
{
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    struct timeval tv = { .tv_sec = 1 };
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        ESP_LOGW(TAG, "dns: no socket");
        goto out;
    }
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    uint8_t buf[512];
    while (atomic_load(&s_running)) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof from;
        int n = recvfrom(fd, buf, sizeof buf - 16, 0, (struct sockaddr *)&from, &from_len);
        if (n < 12 || (buf[2] & 0x80) || buf[4] != 0 || buf[5] != 1) {
            continue;   /* a timeout, an answer, or not one question */
        }
        /* Walk the question's name to its type and class. */
        int p = 12;
        while (p < n && buf[p] != 0) {
            if (buf[p] & 0xc0) {
                p = n;
                break;
            }
            p += buf[p] + 1;
        }
        if (p + 5 > n) {
            continue;
        }
        p++;
        bool a = buf[p] == 0 && buf[p + 1] == 1;
        p += 4;
        buf[2] = (uint8_t)(0x84 | (buf[2] & 0x79));          /* an answer, authoritative; opcode and RD kept */
        buf[3] = 0x80;                                        /* RA, no error */
        buf[6] = 0, buf[7] = a ? 1 : 0;                       /* answers */
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        if (a) {
            static const uint8_t ANSWER[] = { 0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1 };
            memcpy(buf + p, ANSWER, sizeof ANSWER);
            p += sizeof ANSWER;
        }
        sendto(fd, buf, p, 0, (struct sockaddr *)&from, from_len);
    }
out:
    if (fd >= 0) {
        close(fd);
    }
    s_dns_task = NULL;
    vTaskDeleteWithCaps(NULL);
}

/* ---- small helpers ---- */

static void touch(void)
{
    atomic_store(&s_last_us, esp_timer_get_time());
}

static int hex(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Whether an x-www-form-urlencoded body has the field (values have their
 * '=' and '&' encoded, so the key is found only as a key). */
static bool has(const char *body, const char *key)
{
    size_t klen = strlen(key);
    for (const char *p = body; p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            return true;
        }
    }
    return false;
}

/* A field of an x-www-form-urlencoded body, decoded; false if it isn't there
 * or doesn't fit. */
static bool field(const char *body, const char *key, char *out, size_t cap)
{
    size_t klen = strlen(key);
    for (const char *p = body; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL) {
        if (strncmp(p, key, klen) != 0 || p[klen] != '=') {
            continue;
        }
        size_t n = 0;
        for (p += klen + 1; *p && *p != '&'; p++) {
            int c = (unsigned char)*p;
            if (c == '+') {
                c = ' ';
            } else if (c == '%' && hex(p[1]) >= 0 && hex(p[2]) >= 0) {
                c = hex(p[1]) * 16 + hex(p[2]);
                p += 2;
            }
            if (n + 1 >= cap) {
                return false;
            }
            out[n++] = (char)c;
        }
        out[n] = '\0';
        return true;
    }
    return false;
}

/* A JSON string, escaped, into the response. */
static void send_str(httpd_req_t *req, const char *s)
{
    char buf[96];
    size_t n = 0;
    buf[n++] = '"';
    for (; *s; s++) {
        if (n > sizeof buf - 8) {
            httpd_resp_send_chunk(req, buf, n);
            n = 0;
        }
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            buf[n++] = '\\';
            buf[n++] = (char)c;
        } else if (c < 0x20) {
            n += snprintf(buf + n, sizeof buf - n, "\\u%04x", c);
        } else {
            buf[n++] = (char)c;
        }
    }
    buf[n++] = '"';
    httpd_resp_send_chunk(req, buf, n);
}

static void send_text(httpd_req_t *req, const char *s)
{
    httpd_resp_send_chunk(req, s, HTTPD_RESP_USE_STRLEN);
}

/* ---- the subscription ---- */

size_t boopie_setup_subscription(char *out, size_t cap)
{
    nvs_handle_t h;
    size_t n = cap;
    if (cap) {
        out[0] = '\0';
    }
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }
    if (nvs_get_str(h, NVS_SUB, out, &n) != ESP_OK) {
        n = 0;
        if (cap) {
            out[0] = '\0';
        }
    } else if (n) {
        n--;   /* without the terminator */
    }
    nvs_close(h);
    return n;
}

static bool set_subscription(const char *sub)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = *sub ? nvs_set_str(h, NVS_SUB, sub) : nvs_erase_key(h, NVS_SUB);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

/* ---- handlers ---- */

static esp_err_t on_page(httpd_req_t *req)
{
    touch();
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, setup_html_start, setup_html_end - setup_html_start);
}

static esp_err_t on_state(httpd_req_t *req)
{
    touch();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    muse_wifi_status_t w;
    muse_wifi_status(&w);
    char line[96], sdk[BOOPIE_SDK_TOKEN_LEN + 1];
    bool has_sdk = boopie_sdk_token(sdk);
    memset(sdk, 0, sizeof sdk);
    bool has_sub = false;
    nvs_handle_t h;
    size_t sub_len = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        has_sub = nvs_get_str(h, NVS_SUB, NULL, &sub_len) == ESP_OK && sub_len > 1;
        nvs_close(h);
    }

    send_text(req, "{\"wifi\":");
    send_str(req, w.state == MUSE_WIFI_CONNECTED ? w.ssid : "");
    /* Secrets never go back to the phone: only whether they're there. The
     * device token comes with pairing; the page just says if it has. */
    snprintf(line, sizeof line, ",\"brain\":%d,\"sdk\":%s,\"paired\":%s,\"sub\":%s", (int)boopie_avatar_brain(),
             has_sdk ? "true" : "false", muse_settings_hatch_token_len() ? "true" : "false",
             has_sub ? "true" : "false");
    send_text(req, line);
    send_text(req, ",\"name\":");
    send_str(req, boopie_avatar_has_own_name() ? boopie_avatar_pet_name() : "");
    send_text(req, ",\"aps\":[");
    muse_wifi_ap_t *aps = heap_caps_malloc(MAX_APS * sizeof *aps, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint32_t gen;
    int n = aps ? muse_wifi_scan_results(aps, MAX_APS, &gen) : 0;
    for (int i = 0; i < n; i++) {
        send_text(req, i ? ",{\"s\":" : "{\"s\":");
        send_str(req, aps[i].ssid);
        snprintf(line, sizeof line, ",\"r\":%d,\"l\":%d}", aps[i].rssi, aps[i].secure ? 1 : 0);
        send_text(req, line);
    }
    free(aps);
    send_text(req, "]}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t on_scan(httpd_req_t *req)
{
    touch();
    muse_wifi_scan();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* Joining a network can move the hotspot to the router's channel and drop the
 * phone, so it waits until the answer has gone out. */
static char s_join_ssid[MUSE_SSID_MAX + 1], s_join_pass[MUSE_PASS_MAX + 1];

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(4000));
    esp_restart();
}

static void join_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));
    muse_settings_set_wifi_on(true);
    muse_settings_set_wifi(s_join_ssid, s_join_pass);
    memset(s_join_pass, 0, sizeof s_join_pass);
    vTaskDelete(NULL);
}

static esp_err_t answer(httpd_req_t *req, bool ok, const char *msg)
{
    httpd_resp_set_type(req, "application/json");
    send_text(req, ok ? "{\"ok\":true,\"msg\":" : "{\"ok\":false,\"msg\":");
    send_str(req, msg);
    send_text(req, "}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t on_save(httpd_req_t *req)
{
    touch();
    if (req->content_len == 0 || req->content_len > BODY_MAX) {
        return answer(req, false, "内容太长了");
    }
    char *body = heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *val = heap_caps_malloc(BODY_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body || !val) {
        free(body);
        free(val);
        return answer(req, false, "内存不够，再试一次");
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            free(body);
            free(val);
            return ESP_FAIL;
        }
        got += n;
    }
    body[got] = '\0';

    /* Check everything first, so a bad field saves nothing. */
    const char *error = NULL;
    char ssid[MUSE_SSID_MAX + 1] = "", pass[MUSE_PASS_MAX + 1] = "";
    bool has_ssid = has(body, "ssid");
    if (has_ssid) {
        if (!field(body, "ssid", ssid, sizeof ssid)) {
            error = "Wi-Fi 名称太长";
        } else if (!*ssid) {
            error = "Wi-Fi 名称是空的";
        } else if (has(body, "pass") && !field(body, "pass", pass, sizeof pass)) {
            error = "Wi-Fi 密码太长";
        } else if (*pass && strlen(pass) < 8) {
            error = "Wi-Fi 密码至少 8 位";
        }
    }
    int brain = -1;
    if (!error && field(body, "brain", val, 4)) {
        brain = atoi(val);
        if (brain < 0 || brain >= BOOPIE_BRAIN_COUNT) {
            error = "不认识这个 AI 助手";
        }
    }
    char sdk[BOOPIE_SDK_TOKEN_LEN + 2] = "";
    bool has_sdk = !error && has(body, "sdk");
    if (has_sdk && (!field(body, "sdk", sdk, sizeof sdk) || (*sdk && !boopie_sdk_token_valid(sdk)))) {
        error = "开发者 token 不对：应是 mgst_ 开头的 48 个字符，重新复制一次";
    }
    if (!error && field(body, "sub", val, SUB_MAX + 1)) {
        if (*val && strncmp(val, "https://", 8) != 0 && strncmp(val, "http://", 7) != 0
            && strncmp(val, "ss://", 5) != 0) {
            error = "订阅要以 https://、http:// 或 ss:// 开头";
        }
    } else if (!error && has(body, "sub")) {
        error = "订阅链接太长";
    }
    char name[BOOPIE_PET_NAME_MAX];
    bool has_name = false;
    if (!error && has(body, "name")) {
        has_name = field(body, "name", name, sizeof name);
        if (!has_name) {
            error = "名字太长，最多 8 个字";
        }
    }

    uint32_t saved = 0;
    if (!error && has_name) {
        const char *why;
        if (boopie_avatar_set_pet_name(name, &why)) {
            saved |= BOOPIE_SETUP_SAVED_NAME;
        } else {
            error = strstr(why, "long") ? "名字太长，最多 8 个字" : "名字里有显示不了的字";
        }
    }
    if (!error) {
        if (brain >= 0) {
            boopie_avatar_set_brain((boopie_brain_t)brain);
            boopie_xiaozhi_start();   /* if that was 小智 */
            saved |= BOOPIE_SETUP_SAVED_BRAIN;
        }
        if (has_sdk) {
            if (boopie_sdk_token_set(sdk)) {
                saved |= BOOPIE_SETUP_SAVED_MUSE;
            } else {
                error = "开发者 token 没存上";
            }
        }
        if (!error && field(body, "sub", val, SUB_MAX + 1)) {
            if (set_subscription(val)) {
                saved |= BOOPIE_SETUP_SAVED_PROXY;
                if (*val) {
                    /* Imported: on, and the nodes fetched now (or after the restart, if one's coming). */
                    boopie_vpn_set_on(true);
                    boopie_vpn_update();
                }
            } else {
                error = "订阅没存上";
            }
        }
    }
    /* Secrets don't linger in the heap. */
    memset(val, 0, BODY_MAX + 1);
    memset(body, 0, got);
    free(val);
    free(body);

    if (!error && has_ssid) {
        strlcpy(s_join_ssid, ssid, sizeof s_join_ssid);
        strlcpy(s_join_pass, pass, sizeof s_join_pass);
        if (xTaskCreate(join_task, "boopie_join", 3072, NULL, 4, NULL) == pdPASS) {
            saved |= BOOPIE_SETUP_SAVED_WIFI;
        } else {
            error = "Wi-Fi 没存上";
        }
    }
    memset(pass, 0, sizeof pass);
    memset(sdk, 0, sizeof sdk);
    if (saved & BOOPIE_SETUP_SAVED_MUSE) {
        /* Pairing reads the developer token once, at start: restart once the
         * answer is out (and the Wi-Fi, if any, saved). */
        xTaskCreate(restart_task, "boopie_restart", 2048, NULL, 3, NULL);
    }
    if (saved) {
        atomic_fetch_or(&s_saved, saved);
        atomic_fetch_add(&s_saves, 1);
    }
    if (error) {
        char line[96];
        snprintf(line, sizeof line, "%s%s", saved ? "前面的项存上了，但是" : "", error);
        return answer(req, false, line);
    }
    if (saved & BOOPIE_SETUP_SAVED_MUSE) {
        return answer(req, true, "已保存。Boopie 马上重启一下，开发者 token 就生效了");
    }
    return answer(req, true, has_ssid ? "已保存。Boopie 正在连 Wi-Fi，热点可能会断开一下" : "已保存");
}

/* Anything else (a phone checking for a captive portal) goes to the page. iOS
 * needs a body with the redirect to pop the page up. */
static esp_err_t on_other(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    touch();
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", BOOPIE_SETUP_URL);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "Boopie");
}

/* ---- start and stop ---- */

static void make_ap(boopie_setup_ap_t *ap)
{
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(ap->ssid, sizeof ap->ssid, "Boopie-%02X%02X", mac[4], mac[5]);
    static const char LETTERS[] = "abcdefghjkmnpqrstuvwxyz23456789";   /* no 0/o, 1/l/i */
    for (int i = 0; i < 8; i++) {
        ap->pass[i] = LETTERS[esp_random() % (sizeof LETTERS - 1)];
    }
    ap->pass[8] = '\0';
}

bool boopie_setup_web_start(boopie_setup_ap_t *ap)
{
    if (atomic_load(&s_running)) {
        return true;
    }
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (!s_ap_netif) {
            return false;
        }
        /* DHCP option 114 points phones that read it straight at the page. */
        static const char URI[] = "http://192.168.4.1";
        esp_netif_dhcps_stop(s_ap_netif);
        esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, (void *)URI, strlen(URI));
        esp_netif_dhcps_start(s_ap_netif);
    }
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
        return false;
    }
    make_ap(ap);
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.ap.ssid, ap->ssid, sizeof wc.ap.ssid);
    strlcpy((char *)wc.ap.password, ap->pass, sizeof wc.ap.password);
    wc.ap.ssid_len = strlen(ap->ssid);
    wc.ap.max_connection = 2;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wc.ap.pmf_cfg.capable = true;
    if (esp_wifi_set_config(WIFI_IF_AP, &wc) != ESP_OK) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        return false;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);   /* the hotspot needs the radio awake */

    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 6;
    esp_err_t err = httpd_start(&s_http, &cfg);
    if (err != ESP_OK) {
        /* After a failed start the server's port can stay taken until a restart. */
        ESP_LOGE(TAG, "web server not started: %s (internal free %u, largest %u)", esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        esp_wifi_set_mode(WIFI_MODE_STA);
        return false;
    }
    static const httpd_uri_t URIS[] = {
        { .uri = "/", .method = HTTP_GET, .handler = on_page },
        { .uri = "/api/state", .method = HTTP_GET, .handler = on_state },
        { .uri = "/api/scan", .method = HTTP_GET, .handler = on_scan },
        { .uri = "/api/save", .method = HTTP_POST, .handler = on_save },
    };
    for (size_t i = 0; i < sizeof URIS / sizeof URIS[0]; i++) {
        httpd_register_uri_handler(s_http, &URIS[i]);
    }
    httpd_register_err_handler(s_http, HTTPD_404_NOT_FOUND, on_other);

    atomic_store(&s_running, true);
    atomic_store(&s_saves, 0);
    atomic_store(&s_saved, 0);
    touch();
    /* Its stack in PSRAM: it only answers on a UDP socket. */
    if (xTaskCreateWithCaps(dns_task, "boopie_dns", 3072, NULL, 4, &s_dns_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGW(TAG, "no DNS: phones won't pop the page up by themselves");
    }
    muse_wifi_scan();   /* fresh networks for the page */
    ESP_LOGI(TAG, "phone setup on %s", ap->ssid);
    return true;
}

void boopie_setup_web_stop(void)
{
    if (!atomic_load(&s_running)) {
        return;
    }
    atomic_store(&s_running, false);   /* the DNS task leaves within a second */
    for (int i = 0; i < 30 && s_dns_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_http) {
        httpd_stop(s_http);
        s_http = NULL;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "phone setup off");
}

int boopie_setup_web_clients(void)
{
    if (!atomic_load(&s_running)) {
        return 0;
    }
    wifi_sta_list_t list;
    return esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}

uint32_t boopie_setup_web_saves(uint32_t *saved)
{
    if (saved) {
        *saved = atomic_load(&s_saved);
    }
    return atomic_load(&s_saves);
}

int boopie_setup_web_idle_s(void)
{
    return (int)((esp_timer_get_time() - atomic_load(&s_last_us)) / 1000000);
}
