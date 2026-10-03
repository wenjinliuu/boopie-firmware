/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/* Boopie: the version the settings page shows. */
typedef struct {
    char version[32];
} esp_app_desc_t;

const esp_app_desc_t *esp_app_get_description(void);
