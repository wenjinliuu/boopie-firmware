/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_sdk_token.h"

#include <ctype.h>
#include <string.h>

#include "nvs.h"

#define NVS_NS "boopie_net"
#define NVS_KEY "sdk"

bool boopie_sdk_token_valid(const char *t)
{
    if (!t || strlen(t) != BOOPIE_SDK_TOKEN_LEN || strncmp(t, "mgst_", 5) != 0) {
        return false;
    }
    for (const char *p = t + 5; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_') {
            return false;
        }
    }
    /* The last of 43 base64url characters carries 2 bits: one of these. */
    return strchr("AEIMQUYcgkosw048", t[BOOPIE_SDK_TOKEN_LEN - 1]) != NULL;
}

bool boopie_sdk_token(char *out)
{
    nvs_handle_t h;
    size_t n = BOOPIE_SDK_TOKEN_LEN + 1;
    out[0] = '\0';
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_get_str(h, NVS_KEY, out, &n) == ESP_OK && boopie_sdk_token_valid(out);
    nvs_close(h);
    if (!ok) {
        out[0] = '\0';
    }
    return ok;
}

bool boopie_sdk_token_set(const char *t)
{
    if (*t && !boopie_sdk_token_valid(t)) {
        return false;
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = *t ? nvs_set_str(h, NVS_KEY, t) : nvs_erase_key(h, NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}
