/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * The check-in and activation follow 78/xiaozhi-esp32 (main/ota.cc,
 * main/application.cc), Copyright (c) 2025 Shenzhen Xinzhi Future Technology
 * Co., Ltd. and contributors, MIT License.
 */

#include "boopie_xiaozhi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "boopie_avatar.h"
#include "boopie_clock.h"
#include "boopie_sound.h"
#include "boopie_xz_proto.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "muse_state.h"
#include "muse_wifi.h"
#include "nvs.h"

static const char *TAG = "boopie_xz";

#define NS "xiaozhi"
#define MAX_REPLY 4096
#define POLL_MS 3000            /* activate, while the code's up */
#define RECHECK_MS (6 * 3600 * 1000)   /* bound: check in now and then for a fresh token */

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static boopie_xz_state_t s_state;
static char s_code[BOOPIE_XZ_CODE_MAX];
static char s_note[96];
static char s_url[BOOPIE_XZ_URL_MAX], s_token[BOOPIE_XZ_TOKEN_MAX];
static char s_uuid[37], s_mac[18];
static volatile bool s_recheck;

static void set_state(boopie_xz_state_t st, const char *code, const char *note)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state = st;
    snprintf(s_code, sizeof s_code, "%s", code ? code : "");
    snprintf(s_note, sizeof s_note, "%s", note ? note : "");
    xSemaphoreGive(s_lock);
}

/* ---- kept in NVS: the device's own UUID, and where to talk ---- */

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof s_uuid;
        nvs_get_str(h, "uuid", s_uuid, &n);
        n = sizeof s_url;
        nvs_get_str(h, "ws_url", s_url, &n);
        n = sizeof s_token;
        nvs_get_str(h, "ws_token", s_token, &n);
        nvs_close(h);
    }
    if (strlen(s_uuid) != 36) {
        uint8_t rnd[16];
        esp_fill_random(rnd, sizeof rnd);
        boopie_xz_uuid(rnd, s_uuid);
        if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, "uuid", s_uuid);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_mac, sizeof s_mac, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void save_endpoint(const char *url, const char *token)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool same = !strcmp(url, s_url) && !strcmp(token, s_token);
    snprintf(s_url, sizeof s_url, "%s", url);
    snprintf(s_token, sizeof s_token, "%s", token);
    xSemaphoreGive(s_lock);
    nvs_handle_t h;
    if (!same && nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ws_url", url);
        nvs_set_str(h, "ws_token", token);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---- HTTP ---- */

typedef struct {
    char *buf;
    size_t len;
} reply_t;

static esp_err_t on_http(esp_http_client_event_t *evt)
{
    reply_t *r = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && r->len + evt->data_len < MAX_REPLY) {
        memcpy(r->buf + r->len, evt->data, evt->data_len);
        r->len += evt->data_len;
        r->buf[r->len] = '\0';
    }
    return ESP_OK;
}

/* POST body to url; the status (0 if it never got there), the reply into r. */
static int post(const char *url, const char *body, reply_t *r)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .event_handler = on_http,
        .user_data = r,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return 0;
    }
    char ua[48];
    snprintf(ua, sizeof ua, "boopie/%s", esp_app_get_description()->version);
    esp_http_client_set_header(c, "Activation-Version", "1");
    esp_http_client_set_header(c, "Device-Id", s_mac);
    esp_http_client_set_header(c, "Client-Id", s_uuid);
    esp_http_client_set_header(c, "User-Agent", ua);
    esp_http_client_set_header(c, "Accept-Language", "zh-CN");
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, body, (int)strlen(body));
    r->len = 0;
    r->buf[0] = '\0';
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    return err == ESP_OK ? status : 0;
}

/* ---- the check-in, and waiting to be bound ---- */

static bool wanted(void)
{
    return boopie_avatar_brain() == BOOPIE_BRAIN_XIAOZHI;
}

/* Sleeps ms, or less if a re-check is asked for or 小智 isn't wanted any more. */
static void nap(uint32_t ms)
{
    for (uint32_t t = 0; t < ms && !s_recheck && wanted(); t += 250) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/* Checks in; true when that went through (*ota filled). */
static bool check_in(reply_t *r, boopie_xz_ota_t *ota)
{
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    boopie_xz_info_t info = { s_mac, s_uuid, esp_app_get_description()->version, flash,
                              (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) };
    char body[640];
    if (boopie_xz_info_json(&info, body, sizeof body) < 0) {
        return false;
    }
    int status = post(BOOPIE_XZ_OTA_URL, body, r);
    if (status != 200 || !boopie_xz_parse_ota(r->buf, ota)) {
        ESP_LOGW(TAG, "check-in failed: HTTP %d", status);
        return false;
    }
    if (ota->offered_firmware) {
        ESP_LOGI(TAG, "the server offered its own firmware: ignored");
    }
    if (ota->has_time && !boopie_clock_synced()) {
        /* Before NTP has: the server's clock is near enough for the pet. */
        struct timeval tv = { .tv_sec = (time_t)(ota->time_ms / 1000) };
        settimeofday(&tv, NULL);
    }
    return true;
}

/* The code's up: activate, every few seconds, till bound (true), it's run out, or it's not wanted. */
static bool await_binding(reply_t *r, const boopie_xz_ota_t *ota)
{
    char url[64];
    snprintf(url, sizeof url, "%sactivate", BOOPIE_XZ_OTA_URL);
    int64_t until = (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS + (ota->timeout_ms > 0 ? ota->timeout_ms : 120000);
    while (wanted() && !s_recheck && (int64_t)xTaskGetTickCount() * portTICK_PERIOD_MS < until) {
        if (!muse_wifi_connected()) {
            return false;
        }
        if (ota->has_challenge) {
            int status = post(url, "{}", r);
            if (status == 200) {
                return true;
            }
            nap(status == 202 ? POLL_MS : 10000);   /* 202: not yet */
        } else {
            nap(POLL_MS * 2);   /* no challenge: just check in again to see */
            return false;
        }
    }
    return false;
}

static void xz_task(void *arg)
{
    (void)arg;
    char *buf = heap_caps_malloc(MAX_REPLY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    boopie_xz_ota_t *ota = heap_caps_malloc(sizeof *ota, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf || !ota) {
        ESP_LOGE(TAG, "no memory");
        vTaskDelete(NULL);
        return;
    }
    reply_t r = { buf, 0 };
    uint32_t backoff = 10000;
    bool told_code = false;
    for (;;) {
        if (!wanted()) {
            set_state(BOOPIE_XZ_OFF, NULL, NULL);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        if (!muse_wifi_connected()) {
            set_state(BOOPIE_XZ_NO_NET, NULL, "等 Wi-Fi 连上");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        s_recheck = false;
        set_state(BOOPIE_XZ_CHECKING, NULL, "正在连接小智……");
        if (!check_in(&r, ota)) {
            char note[64];
            snprintf(note, sizeof note, "连不上小智服务器，%u 秒后再试", (unsigned)(backoff / 1000));
            set_state(BOOPIE_XZ_ERROR, NULL, note);
            nap(backoff);
            backoff = backoff * 2 > 300000 ? 300000 : backoff * 2;
            continue;
        }
        backoff = 10000;
        if (ota->has_code || ota->has_challenge) {
            set_state(BOOPIE_XZ_CODE, ota->code, "到 xiaozhi.me 添加设备，输入它");
            if (!told_code && ota->has_code) {
                told_code = true;   /* once a power-up on the face: it's in settings after */
                boopie_sound_play(BOOPIE_SOUND_NOTIFY);
                muse_state_set_caption("小智激活码 %s\n在 xiaozhi.me 添加设备", ota->code);
            }
            ESP_LOGI(TAG, "activation code shown; waiting to be bound");
            if (await_binding(&r, ota)) {
                ESP_LOGI(TAG, "bound");
                boopie_sound_play(BOOPIE_SOUND_SCORE);
                muse_state_set_caption("已连接小智！");
            }
            continue;   /* check in again: bound now, or a fresh code */
        }
        if (ota->has_websocket) {
            save_endpoint(ota->ws_url, ota->ws_token);
            set_state(BOOPIE_XZ_READY, NULL, "已连接小智");
            ESP_LOGI(TAG, "bound; where to talk is known");
        } else {
            set_state(BOOPIE_XZ_ERROR, NULL, "小智没给对话地址，稍后再试");
        }
        nap(RECHECK_MS);
    }
}

void boopie_xiaozhi_start(void)
{
    if (s_task) {
        return;
    }
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        load();
    }
    /* Only once it's the brain: internal RAM is scarce with Wi-Fi and BLE up.
     * Choosing 小智 later calls this again. */
    if (!wanted()) {
        return;
    }
    /* Its stack in internal RAM: it writes to NVS, which a PSRAM stack can't. */
    if (xTaskCreate(xz_task, "boopie_xz", 7168, NULL, 3, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "task not started");
    }
}

boopie_xz_state_t boopie_xiaozhi_status(char *code, size_t code_cap, char *note, size_t note_cap)
{
    if (!s_lock) {
        if (code_cap) {
            code[0] = '\0';
        }
        if (note_cap) {
            note[0] = '\0';
        }
        return BOOPIE_XZ_OFF;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    boopie_xz_state_t st = s_state;
    if (code_cap) {
        snprintf(code, code_cap, "%s", s_code);
    }
    if (note_cap) {
        snprintf(note, note_cap, "%s", s_note);
    }
    xSemaphoreGive(s_lock);
    return st;
}

void boopie_xiaozhi_recheck(void)
{
    s_recheck = true;
}

bool boopie_xiaozhi_endpoint(char *url, size_t url_cap, char *token, size_t token_cap)
{
    if (!s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = s_url[0] != '\0';
    snprintf(url, url_cap, "%s", s_url);
    snprintf(token, token_cap, "%s", s_token);
    xSemaphoreGive(s_lock);
    return ok;
}

void boopie_xiaozhi_ids(char *mac, size_t mac_cap, char *uuid, size_t uuid_cap)
{
    snprintf(mac, mac_cap, "%s", s_mac);
    snprintf(uuid, uuid_cap, "%s", s_uuid);
}
