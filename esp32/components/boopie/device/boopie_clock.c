/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_clock.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "boopie_clock";

#define NS "boopie"
#define KEY_TIME "time"
#define KEY_TZ "tz"
#define NTP_SERVER "ntp.aliyun.com"     /* reachable in and outside China */
#define SAVE_EVERY_S (10 * 60)
/* Anything before this was never set: the chip boots in 1970. */
#define EARLIEST 1767225600             /* 2026-01-01 */

static volatile bool s_synced, s_restored;

static void on_sync(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    ESP_LOGI(TAG, "time set by NTP");
    boopie_clock_save();
}

bool boopie_clock_known(void)
{
    return (s_synced || s_restored) && time(NULL) >= EARLIEST;
}

bool boopie_clock_synced(void)
{
    return s_synced;
}

bool boopie_clock_local(struct tm *out)
{
    memset(out, 0, sizeof(*out));
    if (!boopie_clock_known()) {
        return false;
    }
    time_t now = time(NULL);
    localtime_r(&now, out);
    return true;
}

void boopie_clock_save(void)
{
    time_t now = time(NULL);
    if (now < EARLIEST) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_i64(h, KEY_TIME, (int64_t)now);
    nvs_commit(h);
    nvs_close(h);
}

static void restore(void)
{
    char tz[32] = "CST-8";
    nvs_handle_t h;
    int64_t saved = 0;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(tz);
        if (nvs_get_str(h, KEY_TZ, tz, &n) != ESP_OK) {
            strcpy(tz, "CST-8");
        }
        nvs_get_i64(h, KEY_TIME, &saved);
        nvs_close(h);
    }
    setenv("TZ", tz, 1);
    tzset();
    if (time(NULL) < EARLIEST && saved >= EARLIEST) {
        struct timeval tv = { .tv_sec = (time_t)saved };
        settimeofday(&tv, NULL);
        s_restored = true;
        ESP_LOGI(TAG, "time restored from the last save; NTP will correct it");
    }
}

/* Home Link brings the network up on its own schedule: wait for an address,
 * then start NTP, and keep saving the time. */
static void clock_task(void *arg)
{
    (void)arg;
    bool started = false;
    int since_save = 0;
    for (;;) {
        if (!started) {
            esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip;
            if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0) {
                esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
                cfg.sync_cb = on_sync;
                if (esp_netif_sntp_init(&cfg) == ESP_OK) {
                    started = true;
                    ESP_LOGI(TAG, "NTP started (%s)", NTP_SERVER);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(started ? 60000 : 3000));
        since_save += started ? 60 : 3;
        if (since_save >= SAVE_EVERY_S) {
            since_save = 0;
            boopie_clock_save();
        }
    }
}

void boopie_clock_start(void)
{
    static bool s_started;
    if (s_started) {
        return;
    }
    s_started = true;
    restore();
    xTaskCreate(clock_task, "boopie_clock", 3072, NULL, 2, NULL);
}
