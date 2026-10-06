/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "boopie_crash.h"

#include <inttypes.h>
#include <stdio.h>

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
    /* The saved crash, read back (the coredump partition sits below 16 MB so
     * it can be): what crashed, where, and the backtrace, in the form
     * addr2line takes. Then it's erased, so it's told once. Called early from
     * app_main, before anything writes flash. */
    if (esp_core_dump_image_check() != ESP_OK) {
        return;
    }
    static esp_core_dump_summary_t sum;   /* ~300 bytes, off the main stack */
    char reason[160] = "";
    if (esp_core_dump_get_panic_reason(reason, sizeof(reason)) == ESP_OK) {
        ESP_LOGW(TAG, "last crash: %s", reason);
    }
    if (esp_core_dump_get_summary(&sum) == ESP_OK) {
        char bt[16 * 23 + 1];
        size_t o = 0;
        for (uint32_t i = 0; i < sum.exc_bt_info.depth && i < 16 && o + 23 < sizeof(bt); i++) {
            o += (size_t)snprintf(bt + o, sizeof(bt) - o, " 0x%08" PRIx32 ":0x0", sum.exc_bt_info.bt[i]);
        }
        ESP_LOGW(TAG, "last crash in task %.16s at PC 0x%08" PRIx32 " (cause %" PRIu32 ", address 0x%08" PRIx32 ")",
                 sum.exc_task, sum.exc_pc, sum.ex_info.exc_cause, sum.ex_info.exc_vaddr);
        ESP_LOGW(TAG, "Backtrace:%s%s", bt, sum.exc_bt_info.corrupted ? " |<-CORRUPTED" : "");
    } else {
        ESP_LOGW(TAG, "a crash was saved but can't be read; its backtrace was on the console when it happened");
    }
    esp_core_dump_image_erase();
}
