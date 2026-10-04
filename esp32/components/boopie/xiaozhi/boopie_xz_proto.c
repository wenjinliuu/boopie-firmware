/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * After 78/xiaozhi-esp32 (main/ota.cc, main/boards/common/board.cc),
 * Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd. and
 * contributors, MIT License.
 */

#include "boopie_xz_proto.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

int boopie_xz_info_json(const boopie_xz_info_t *in, char *out, size_t cap)
{
    /* As the official firmware sends it, but as itself: its own application and
     * board, so the server never offers the official firmware for it. */
    int n = snprintf(out, cap,
                     "{\"version\":2,\"language\":\"zh-CN\",\"flash_size\":%lu,\"minimum_free_heap_size\":\"%lu\","
                     "\"mac_address\":\"%s\",\"uuid\":\"%s\",\"chip_model_name\":\"esp32s3\","
                     "\"application\":{\"name\":\"boopie\",\"version\":\"%s\"},"
                     "\"board\":{\"type\":\"boopie\",\"name\":\"boopie-amoled-1.75c\",\"mac\":\"%s\"}}",
                     (unsigned long)in->flash_size, (unsigned long)in->free_heap, in->mac, in->uuid, in->version,
                     in->mac);
    return n > 0 && (size_t)n < cap ? n : -1;
}

void boopie_xz_uuid(const uint8_t rnd[16], char out[37])
{
    uint8_t b[16];
    memcpy(b, rnd, 16);
    b[6] = (uint8_t)((b[6] & 0x0f) | 0x40);   /* version 4 */
    b[8] = (uint8_t)((b[8] & 0x3f) | 0x80);   /* variant 10 */
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3],
             b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

static void copy_str(const cJSON *obj, const char *key, char *out, size_t cap, bool *has)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v) && v->valuestring) {
        snprintf(out, cap, "%s", v->valuestring);
        if (has) {
            *has = true;
        }
    }
}

bool boopie_xz_parse_ota(const char *json, boopie_xz_ota_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *act = cJSON_GetObjectItemCaseSensitive(root, "activation");
    if (cJSON_IsObject(act)) {
        copy_str(act, "message", out->message, sizeof out->message, NULL);
        copy_str(act, "code", out->code, sizeof out->code, &out->has_code);
        copy_str(act, "challenge", out->challenge, sizeof out->challenge, &out->has_challenge);
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(act, "timeout_ms");
        if (cJSON_IsNumber(t)) {
            out->timeout_ms = t->valueint;
        }
    }
    const cJSON *ws = cJSON_GetObjectItemCaseSensitive(root, "websocket");
    if (cJSON_IsObject(ws)) {
        bool url = false;
        copy_str(ws, "url", out->ws_url, sizeof out->ws_url, &url);
        copy_str(ws, "token", out->ws_token, sizeof out->ws_token, NULL);
        out->has_websocket = url;
    }
    const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "server_time");
    if (cJSON_IsObject(st)) {
        const cJSON *ts = cJSON_GetObjectItemCaseSensitive(st, "timestamp");
        if (cJSON_IsNumber(ts) && ts->valuedouble > 1.6e12) {   /* milliseconds, and sane */
            out->time_ms = (int64_t)ts->valuedouble;
            out->has_time = true;
        }
    }
    out->offered_firmware = cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root, "firmware"));
    cJSON_Delete(root);
    return true;
}
