/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 小智's official server, the protocol's plain parts (docs/boopie-xiaozhi.md),
 * after 78/xiaozhi-esp32 (MIT): what the device says about itself when it
 * checks in, and what the check-in's answer holds. Plain C (cJSON).
 */

#define BOOPIE_XZ_OTA_URL "https://api.tenclass.net/xiaozhi/ota/"
#define BOOPIE_XZ_CODE_MAX 16
#define BOOPIE_XZ_URL_MAX 160
#define BOOPIE_XZ_TOKEN_MAX 192
#define BOOPIE_XZ_MSG_MAX 160

typedef struct {
    const char *mac;          /* "aa:bb:cc:dd:ee:ff" */
    const char *uuid;         /* the device's own, kept in NVS */
    const char *version;      /* the firmware's */
    uint32_t flash_size;
    uint32_t free_heap;
} boopie_xz_info_t;

/* The check-in's body, JSON, into out; its length, or -1 if it didn't fit. */
int boopie_xz_info_json(const boopie_xz_info_t *info, char *out, size_t cap);

/* A UUID v4 from 16 random bytes: "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx". */
void boopie_xz_uuid(const uint8_t rnd[16], char out[37]);

typedef struct {
    bool has_code;            /* not bound yet: show this code */
    char code[BOOPIE_XZ_CODE_MAX];
    char message[BOOPIE_XZ_MSG_MAX];
    bool has_challenge;       /* ... and wait on activate with it */
    char challenge[64];
    int timeout_ms;           /* how long the code holds (0: not said) */
    bool has_websocket;       /* where to talk, once bound */
    char ws_url[BOOPIE_XZ_URL_MAX];
    char ws_token[BOOPIE_XZ_TOKEN_MAX];
    bool has_time;
    int64_t time_ms;          /* the server's clock, epoch milliseconds (UTC) */
    bool offered_firmware;    /* it offered its own firmware: never taken */
} boopie_xz_ota_t;

/* The check-in's answer; false if it isn't JSON. */
bool boopie_xz_parse_ota(const char *json, boopie_xz_ota_t *out);
