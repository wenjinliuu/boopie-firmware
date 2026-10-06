/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_ota.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

#include "boopie_assets.h"
#include "boopie_store.h"
#include "muse_state.h"
#include "muse_wifi.h"

static const char *TAG = "boopie.ota";

#define BOARD "waveshare-s3-175c"
#define MANIFEST_MAX 8192
#define CHUNK 4096
#define TRIES 6
#define FIRST_CHECK_S 180             /* after Wi-Fi comes up */
#define EVERY_S (24 * 3600)
#define STAGED "/data/ota_assets.bin"
#define PART "/data/ota_assets.part"
#define STAGED_FOR "/data/ota_assets.ver"   /* the app version the staged pack belongs to */
#define HEALTHY_AFTER_US (45LL * 1000000)

typedef struct {
    char file[129];
    uint32_t size;
    uint8_t sha[32];
} release_file_t;

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static boopie_ota_info_t s_info;
static release_file_t s_app, s_assets;
static bool s_need_app, s_need_assets;
static char s_news_for[16];
static atomic_int s_want;              /* 1: check, 2: install */
static atomic_llong s_ui_since_us, s_ui_last_us;
static uint8_t *s_buf;                 /* CHUNK, in PSRAM */

#define WANT_CHECK 1
#define WANT_INSTALL 2

/* ---- small things ---- */

static const char *ota_url(void)
{
    return CONFIG_BOOPIE_OTA_URL;
}

bool boopie_ota_enabled(void)
{
    return CONFIG_BOOPIE_OTA_URL[0] && strlen(CONFIG_BOOPIE_OTA_PUBKEY) == 130;
}

static void set_state(boopie_ota_state_t st, int percent, const char *msg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_info.state = st;
    s_info.percent = percent;
    if (msg) {
        strlcpy(s_info.msg, msg, sizeof s_info.msg);
    }
    xSemaphoreGive(s_lock);
}

static void parse_ver(const char *s, long out[3])
{
    out[0] = out[1] = out[2] = 0;
    if (*s == 'v' || *s == 'V') {
        s++;
    }
    for (int i = 0; i < 3 && *s; i++) {
        char *end;
        out[i] = strtol(s, &end, 10);
        if (end == s || *end != '.') {
            break;
        }
        s = end + 1;
    }
}

static bool newer(const char *cand, const char *cur)
{
    long a[3], b[3];
    parse_ver(cand, a);
    parse_ver(cur, b);
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) {
            return a[i] > b[i];
        }
    }
    return false;
}

static bool unhex(const char *hex, uint8_t *out, size_t n)
{
    if (!hex || strlen(hex) != n * 2) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (!isxdigit((unsigned char)hex[2 * i]) || !isxdigit((unsigned char)hex[2 * i + 1])
            || sscanf(hex + 2 * i, "%2x", &v) != 1) {
            return false;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

static bool wifi_up(void)
{
    muse_wifi_status_t w;
    muse_wifi_status(&w);
    return w.state == MUSE_WIFI_CONNECTED;
}

/* ---- HTTP ---- */

/* A small file (the manifest, its signature) into buf; its length, or -1. */
static int get_small(const char *name, uint8_t *buf, size_t cap)
{
    char url[192];
    snprintf(url, sizeof url, "%s/%s", ota_url(), name);
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "Boopie",
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    int n = -1;
    if (h && esp_http_client_open(h, 0) == ESP_OK) {
        esp_http_client_fetch_headers(h);
        int status = esp_http_client_get_status_code(h);
        if (status == 200) {
            n = 0;
            int r;
            while ((size_t)n < cap && (r = esp_http_client_read(h, (char *)buf + n, cap - n)) > 0) {
                n += r;
            }
            if ((size_t)n >= cap) {
                n = -1;   /* too big to be ours */
            }
        } else {
            ESP_LOGW(TAG, "%s: HTTP %d", name, status);
        }
    } else {
        ESP_LOGW(TAG, "%s: can't connect", name);
    }
    if (h) {
        esp_http_client_cleanup(h);
    }
    return n;
}

typedef bool (*sink_fn)(void *ctx, uint32_t at, const uint8_t *p, size_t n);

/* A release file from `from` to its end into sink, hashing as it goes, with
 * Range requests to carry on after a dropped connection. */
static bool download(const release_file_t *f, uint32_t from, psa_hash_operation_t *hash, sink_fn sink,
                     void *ctx, int pct_lo, int pct_hi, const char **why)
{
    char url[256];
    snprintf(url, sizeof url, "%s/files/%s", ota_url(), f->file);
    uint32_t at = from;
    for (int attempt = 0; at < f->size && attempt < TRIES; attempt++) {
        if (attempt) {
            vTaskDelay(pdMS_TO_TICKS(2000 * attempt));
        }
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = 20000,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .user_agent = "Boopie",
            .buffer_size = 2048,
        };
        esp_http_client_handle_t h = esp_http_client_init(&cfg);
        if (!h) {
            *why = "内存不够";
            return false;
        }
        char range[40];
        if (at) {
            snprintf(range, sizeof range, "bytes=%u-", (unsigned)at);
            esp_http_client_set_header(h, "Range", range);
        }
        if (esp_http_client_open(h, 0) != ESP_OK) {
            ESP_LOGW(TAG, "%s: can't connect (try %d)", f->file, attempt + 1);
            *why = "连不上更新服务器";
            esp_http_client_cleanup(h);
            continue;
        }
        esp_http_client_fetch_headers(h);
        int status = esp_http_client_get_status_code(h);
        if (!(status == 206 && at) && !(status == 200 && !at)) {
            ESP_LOGW(TAG, "%s: HTTP %d at %u", f->file, status, (unsigned)at);
            *why = "更新服务器出错";
            esp_http_client_cleanup(h);
            if (status == 404) {
                return false;
            }
            continue;
        }
        int r;
        while (at < f->size && (r = esp_http_client_read(h, (char *)s_buf, CHUNK)) > 0) {
            size_t n = (size_t)r;
            if (n > f->size - at) {
                n = f->size - at;
            }
            if (psa_hash_update(hash, s_buf, n) != PSA_SUCCESS || !sink(ctx, at, s_buf, n)) {
                *why = "写不进去";
                esp_http_client_cleanup(h);
                return false;
            }
            at += n;
            set_state(BOOPIE_OTA_DOWNLOADING, pct_lo + (int)((int64_t)(pct_hi - pct_lo) * at / f->size), NULL);
        }
        esp_http_client_cleanup(h);
        *why = "下载断了";
    }
    if (at < f->size) {
        return false;
    }
    uint8_t got[32];
    size_t len;
    if (psa_hash_finish(hash, got, sizeof got, &len) != PSA_SUCCESS || memcmp(got, f->sha, 32) != 0) {
        ESP_LOGW(TAG, "%s: SHA-256 doesn't match", f->file);
        *why = "下载的文件校验不过";
        return false;
    }
    return true;
}

/* ---- the manifest ---- */

static bool verify(const uint8_t *msg, size_t n, const uint8_t sig[64])
{
    uint8_t pub[65];
    if (!unhex(CONFIG_BOOPIE_OTA_PUBKEY, pub, sizeof pub) || psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t key;
    if (psa_import_key(&attr, pub, sizeof pub, &key) != PSA_SUCCESS) {
        return false;
    }
    bool ok = psa_verify_message(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), msg, n, sig, 64) == PSA_SUCCESS;
    psa_destroy_key(key);
    return ok;
}

static bool file_entry(const cJSON *o, release_file_t *f)
{
    const cJSON *file = cJSON_GetObjectItem(o, "file"), *size = cJSON_GetObjectItem(o, "size"),
                *sha = cJSON_GetObjectItem(o, "sha256");
    if (!cJSON_IsString(file) || !cJSON_IsNumber(size) || !cJSON_IsString(sha) || size->valuedouble <= 0
        || strlen(file->valuestring) >= sizeof f->file || strspn(file->valuestring,
           "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") != strlen(file->valuestring)) {
        return false;
    }
    strlcpy(f->file, file->valuestring, sizeof f->file);
    f->size = (uint32_t)size->valuedouble;
    return unhex(sha->valuestring, f->sha, 32);
}

/* The assets partition's first n bytes, hashed: does the pack match? */
static bool assets_match(const release_file_t *f)
{
    if (!boopie_assets_ready() || boopie_assets_size() != f->size) {
        return false;
    }
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        return true;   /* can't tell: leave it */
    }
    for (uint32_t at = 0; at < f->size; at += CHUNK) {
        size_t n = f->size - at < CHUNK ? f->size - at : CHUNK;
        if (!boopie_assets_read(at, s_buf, n)) {
            psa_hash_abort(&op);
            return false;
        }
        psa_hash_update(&op, s_buf, n);
    }
    uint8_t got[32];
    size_t len;
    return psa_hash_finish(&op, got, sizeof got, &len) == PSA_SUCCESS && memcmp(got, f->sha, 32) == 0;
}

static void check(void)
{
    set_state(BOOPIE_OTA_CHECKING, 0, "正在检查……");
    uint8_t *json = heap_caps_malloc(MANIFEST_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t sig[64];
    int n = json ? get_small("manifest.json", json, MANIFEST_MAX) : -1;
    if (n <= 0 || get_small("manifest.sig", sig, sizeof sig + 1) != (int)sizeof sig) {
        free(json);
        set_state(BOOPIE_OTA_FAILED, 0, "连不上更新服务器，稍后再试");
        return;
    }
    if (!verify(json, (size_t)n, sig)) {
        ESP_LOGW(TAG, "manifest signature doesn't verify");
        free(json);
        set_state(BOOPIE_OTA_FAILED, 0, "更新的签名不对，没有安装");
        return;
    }
    json[n] = '\0';
    cJSON *m = cJSON_Parse((const char *)json);
    free(json);
    const cJSON *board = cJSON_GetObjectItem(m, "board"), *ver = cJSON_GetObjectItem(m, "version"),
                *notes = cJSON_GetObjectItem(m, "notes");
    release_file_t app = { 0 }, assets = { 0 };
    if (!m || !cJSON_IsString(board) || strcmp(board->valuestring, BOARD) != 0 || !cJSON_IsString(ver)
        || strlen(ver->valuestring) >= sizeof s_info.version || !file_entry(cJSON_GetObjectItem(m, "app"), &app)
        || !file_entry(cJSON_GetObjectItem(m, "assets"), &assets)) {
        cJSON_Delete(m);
        set_state(BOOPIE_OTA_FAILED, 0, "更新信息看不懂");
        return;
    }
    const char *running = esp_app_get_description()->version;
    bool need_app = newer(ver->valuestring, running);
    bool broken = !boopie_assets_ready();
    bool need_assets = (need_app || broken) && !assets_match(&assets);
    ESP_LOGI(TAG, "latest %s, running %s%s%s", ver->valuestring, running, need_app ? ": newer" : "",
             need_assets ? ", assets differ" : "");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_app = app;
    s_assets = assets;
    s_need_app = need_app;
    s_need_assets = need_assets;
    strlcpy(s_info.version, ver->valuestring, sizeof s_info.version);
    strlcpy(s_info.notes, cJSON_IsString(notes) ? notes->valuestring : "", sizeof s_info.notes);
    xSemaphoreGive(s_lock);
    cJSON_Delete(m);
    if (need_app) {
        set_state(BOOPIE_OTA_FOUND, 0, "有新版本");
    } else if (need_assets) {
        /* The fonts and pictures are broken: put them right without asking. */
        ESP_LOGW(TAG, "assets pack broken: repairing");
        set_state(BOOPIE_OTA_FOUND, 0, "资源包坏了，正在修复");
        atomic_store(&s_want, WANT_INSTALL);
    } else {
        set_state(BOOPIE_OTA_LATEST, 0, "已经是最新版");
    }
}

/* ---- installing ---- */

static bool file_sink(void *ctx, uint32_t at, const uint8_t *p, size_t n)
{
    (void)at;
    return fwrite(p, 1, n, (FILE *)ctx) == n;
}

typedef struct {
    const esp_partition_t *part;
    uint32_t erased;
} slot_t;

static bool slot_sink(void *ctx, uint32_t at, const uint8_t *p, size_t n)
{
    slot_t *s = ctx;
    while (s->erased < at + n) {
        if (esp_partition_erase_range(s->part, s->erased, 64 * 1024) != ESP_OK) {
            return false;
        }
        s->erased += 64 * 1024;
    }
    return esp_partition_write(s->part, at, p, n) == ESP_OK;
}

static void staged_for(char *out, size_t cap)
{
    out[0] = '\0';
    FILE *f = fopen(STAGED_FOR, "r");
    if (f) {
        if (fgets(out, (int)cap, f)) {
            out[strcspn(out, "\r\n")] = '\0';
        }
        fclose(f);
    }
}

/* The assets pack into the user data partition, for the first boot of the app
 * version it belongs to (an app that then fails to download mustn't get it). */
static bool fetch_assets(const release_file_t *f, const char *for_version, int lo, int hi, const char **why)
{
    struct stat st;
    char was[16];
    staged_for(was, sizeof was);
    if (stat(STAGED, &st) == 0 && (uint32_t)st.st_size == f->size && strcmp(was, for_version) == 0) {
        return true;   /* already waiting (checked when it was renamed) */
    }
    unlink(STAGED);
    unlink(PART);
    if (!boopie_store_ready() || boopie_store_total() - boopie_store_used() < f->size + 256 * 1024) {
        *why = "存储空间不够，先清理一下";
        return false;
    }
    FILE *out = fopen(PART, "wb");
    if (!out) {
        *why = "存储写不进去";
        return false;
    }
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    bool ok = psa_hash_setup(&op, PSA_ALG_SHA_256) == PSA_SUCCESS
              && download(f, 0, &op, file_sink, out, lo, hi, why);
    psa_hash_abort(&op);
    ok = fclose(out) == 0 && ok;
    FILE *v = ok ? fopen(STAGED_FOR, "w") : NULL;
    ok = v && fprintf(v, "%s\n", for_version) > 0;
    ok = (v && fclose(v) == 0) && ok;
    if (!ok || rename(PART, STAGED) != 0) {
        unlink(PART);
        *why = **why ? *why : "存储写不进去";
        return false;
    }
    return true;
}

/* esp_ota_set_boot_partition maps flash to check the image, which a PSRAM
 * stack mustn't: it runs on a small internal one. */
static void set_boot_task(void *arg)
{
    void **a = arg;
    *(esp_err_t *)a[1] = esp_ota_set_boot_partition((const esp_partition_t *)a[0]);
    xTaskNotifyGive((TaskHandle_t)a[2]);
    vTaskDelete(NULL);
}

static bool fetch_app(const release_file_t *f, int lo, int hi, const char **why)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = NULL;
    for (int sub = ESP_PARTITION_SUBTYPE_APP_OTA_0; sub <= ESP_PARTITION_SUBTYPE_APP_OTA_1; sub++) {
        const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, sub, NULL);
        if (p && p != running) {
            target = p;
            break;
        }
    }
    if (!target || f->size > target->size) {
        *why = "放不下新固件";
        return false;
    }
    slot_t slot = { target, 0 };
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    bool ok = psa_hash_setup(&op, PSA_ALG_SHA_256) == PSA_SUCCESS
              && download(f, 0, &op, slot_sink, &slot, lo, hi, why);
    psa_hash_abort(&op);
    if (!ok) {
        return false;
    }
    esp_err_t err = ESP_FAIL;
    void *args[3] = { (void *)target, &err, xTaskGetCurrentTaskHandle() };
    if (xTaskCreate(set_boot_task, "ota_boot", 4096, args, 5, NULL) != pdPASS) {
        *why = "内存不够";
        return false;
    }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set boot partition: %s", esp_err_to_name(err));
        *why = "新固件校验不过";
        return false;
    }
    return true;
}

static void install(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    release_file_t app = s_app, assets = s_assets;
    bool need_app = s_need_app, need_assets = s_need_assets;
    char version[16];
    strlcpy(version, need_app ? s_info.version : esp_app_get_description()->version, sizeof version);
    xSemaphoreGive(s_lock);
    const char *why = "";
    int split = need_app && need_assets ? 45 : need_assets ? 100 : 0;
    set_state(BOOPIE_OTA_DOWNLOADING, 0, need_app ? "正在下载新版本……" : "正在下载资源包……");
    ESP_LOGI(TAG, "installing%s%s", need_app ? " app" : "", need_assets ? " assets" : "");
    if (need_assets && !fetch_assets(&assets, version, 0, split, &why)) {
        set_state(BOOPIE_OTA_FAILED, 0, why);
        return;
    }
    if (need_app && !fetch_app(&app, split, 100, &why)) {
        set_state(BOOPIE_OTA_FAILED, 0, why);
        return;
    }
    ESP_LOGI(TAG, "installed: restarting");
    set_state(BOOPIE_OTA_RESTARTING, 100, "装好了，马上重启");
    muse_state_set_caption("%s", "更新好了，重启一下……");
    vTaskDelay(pdMS_TO_TICKS(2500));
    esp_restart();
}

/* ---- the task ---- */

static void ota_task(void *arg)
{
    (void)arg;
    int64_t up_since = 0, next = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30 * 1000));
        int64_t now = esp_timer_get_time() / 1000000;
        bool up = wifi_up();
        if (!up) {
            up_since = 0;
        } else if (!up_since) {
            up_since = now;
            if (!next) {
                next = now + FIRST_CHECK_S;
            }
        }
        int want = atomic_exchange(&s_want, 0);
        boopie_ota_state_t st = s_info.state;
        if (want == WANT_INSTALL && (st == BOOPIE_OTA_FOUND || st == BOOPIE_OTA_FAILED) && s_info.version[0]) {
            install();
        } else if (want == WANT_CHECK || (up && next && now >= next && st != BOOPIE_OTA_FOUND)) {
            if (!up) {
                set_state(BOOPIE_OTA_FAILED, 0, "还没联网");
                continue;
            }
            next = now + EVERY_S;
            check();
        }
    }
}

void boopie_ota_start(void)
{
    if (s_lock) {
        return;
    }
    s_lock = xSemaphoreCreateMutex();
    if (!boopie_ota_enabled()) {
        s_info.state = BOOPIE_OTA_OFF;
        strlcpy(s_info.msg, "这个版本没有开在线更新", sizeof s_info.msg);
        return;
    }
    s_info.state = BOOPIE_OTA_IDLE;
    s_buf = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    /* TLS and LittleFS on a PSRAM stack are fine where code runs from PSRAM;
     * the one call that maps flash runs on its own (set_boot_task). */
#if CONFIG_SPIRAM_FETCH_INSTRUCTIONS && CONFIG_SPIRAM_RODATA
    BaseType_t ok = xTaskCreateWithCaps(ota_task, "boopie_ota", 8192, NULL, 3, &s_task,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    BaseType_t ok = xTaskCreate(ota_task, "boopie_ota", 8192, NULL, 3, &s_task);
#endif
    if (ok != pdPASS || !s_buf) {
        ESP_LOGE(TAG, "can't start");
        s_info.state = BOOPIE_OTA_OFF;
        strlcpy(s_info.msg, "内存不够，在线更新没有启动", sizeof s_info.msg);
    }
}

static void poke(int want)
{
    if (s_task) {
        atomic_store(&s_want, want);
        xTaskNotifyGive(s_task);
    }
}

void boopie_ota_check(void)
{
    if (s_task && s_info.state != BOOPIE_OTA_DOWNLOADING && s_info.state != BOOPIE_OTA_RESTARTING) {
        set_state(BOOPIE_OTA_CHECKING, 0, "正在检查……");
        poke(WANT_CHECK);
    }
}

void boopie_ota_install(void)
{
    if (s_task && (s_info.state == BOOPIE_OTA_FOUND || s_info.state == BOOPIE_OTA_FAILED) && s_info.version[0]) {
        set_state(BOOPIE_OTA_DOWNLOADING, 0, "准备下载……");
        poke(WANT_INSTALL);
    }
}

void boopie_ota_info(boopie_ota_info_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof *out);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_info;
    if (out->state == BOOPIE_OTA_FAILED && !s_need_app && !s_need_assets) {
        out->version[0] = '\0';   /* a failed check: nothing to retry installing */
    }
    xSemaphoreGive(s_lock);
}

const char *boopie_ota_news(void)
{
    if (!s_lock || s_info.state != BOOPIE_OTA_FOUND || !s_need_app || strcmp(s_news_for, s_info.version) == 0) {
        return NULL;
    }
    strlcpy(s_news_for, s_info.version, sizeof s_news_for);
    return s_news_for;
}

/* ---- boot: the staged assets pack ---- */

void boopie_ota_boot(void)
{
    struct stat st;
    if (stat(STAGED, &st) != 0) {
        unlink(PART);   /* a download cut short by a restart: start it again */
        return;
    }
    /* Only with the app it came with: an older one rolled back to keeps its own. */
    char was[16];
    staged_for(was, sizeof was);
    const char *running = esp_app_get_description()->version;
    if (strcmp(was, running) != 0) {
        if (!newer(was, running)) {
            ESP_LOGW(TAG, "staged assets pack is for %s, not %s: dropped", was[0] ? was : "?", running);
            unlink(STAGED);
            unlink(STAGED_FOR);
        }
        return;   /* (a newer app's, waiting for it to be installed) */
    }
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "assets");
    FILE *f = fopen(STAGED, "rb");
    uint8_t *buf = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    boopie_assets_header_t hdr;
    bool ok = part && f && buf && fread(&hdr, sizeof hdr, 1, f) == 1 && hdr.magic == BOOPIE_ASSETS_MAGIC
              && hdr.size == (uint32_t)st.st_size && hdr.size <= part->size;
    if (ok) {
        ESP_LOGI(TAG, "writing the new assets pack (%u bytes)", (unsigned)hdr.size);
        uint32_t span = (hdr.size + 0xFFFF) & ~0xFFFFu;
        ok = esp_partition_erase_range(part, 0, span < part->size ? span : part->size) == ESP_OK;
        /* Everything but the header, then the header: a pack cut short doesn't count. */
        for (uint32_t at = sizeof hdr; ok && at < hdr.size; at += CHUNK) {
            size_t n = hdr.size - at < CHUNK ? hdr.size - at : CHUNK;
            ok = fread(buf, 1, n, f) == n && esp_partition_write(part, at, buf, n) == ESP_OK;
        }
        ok = ok && esp_partition_write(part, 0, &hdr, sizeof hdr) == ESP_OK;
        ESP_LOGI(TAG, "assets pack %s", ok ? "written" : "write failed");
    } else {
        ESP_LOGW(TAG, "staged assets pack isn't valid: dropped");
    }
    if (f) {
        fclose(f);
    }
    free(buf);
    unlink(STAGED);
    unlink(STAGED_FOR);
}

/* ---- keeping a new app ---- */

void boopie_ota_ui_alive(void)
{
    int64_t now = esp_timer_get_time();
    int64_t last = atomic_load(&s_ui_last_us);
    if (!last || now - last > 2000000) {
        atomic_store(&s_ui_since_us, now);   /* (again) since now */
    }
    atomic_store(&s_ui_last_us, now);
}

bool boopie_ota_healthy(void)
{
    int64_t now = esp_timer_get_time(), since = atomic_load(&s_ui_since_us);
    if (!since || now - atomic_load(&s_ui_last_us) > 2000000 || now - since < HEALTHY_AFTER_US) {
        return false;
    }
    muse_wifi_status_t w;
    muse_wifi_status(&w);
    return w.state == MUSE_WIFI_CONNECTED || w.state == MUSE_WIFI_OFF || w.state == MUSE_WIFI_NO_NETWORK;
}
