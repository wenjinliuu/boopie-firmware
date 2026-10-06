/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "boopie_crash.h"

#include "esp_core_dump.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "boopie_crash";

static const char *reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "external pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    default: return "unknown";
    }
}

void boopie_crash_report(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    ESP_LOGI(TAG, "last reset: %s (%d)", reason_name(r), (int)r);
    /* The saved crash itself can't be read back here: the coredump partition
     * sits past 16 MB, which the cache can't map without an experimental
     * bootloader option. Its backtrace was on the console when it happened. */
    if (esp_core_dump_image_check() == ESP_OK) {
        ESP_LOGW(TAG, "a crash was saved; its backtrace is in the console log just before this boot");
        esp_core_dump_image_erase();
    }
}
