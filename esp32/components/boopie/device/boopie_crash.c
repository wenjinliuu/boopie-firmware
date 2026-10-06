/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "boopie_crash.h"

#include <stdio.h>
#include <stdlib.h>

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
    if (esp_core_dump_image_check() != ESP_OK) {
        return;
    }
    esp_core_dump_summary_t *s = malloc(sizeof *s);
    if (s && esp_core_dump_get_summary(s) == ESP_OK) {
        char bt[16 * 11 + 1];
        size_t n = 0;
        for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++) {
            n += snprintf(bt + n, sizeof bt - n, " 0x%08lx", (unsigned long)s->exc_bt_info.bt[i]);
        }
        bt[n] = '\0';
        ESP_LOGW(TAG, "saved crash: task %s, pc 0x%08lx, cause %lu, vaddr 0x%08lx", s->exc_task,
                 (unsigned long)s->exc_pc, (unsigned long)s->ex_info.exc_cause, (unsigned long)s->ex_info.exc_vaddr);
        ESP_LOGW(TAG, "backtrace%s%s", bt, s->exc_bt_info.corrupted ? " (corrupted)" : "");
    } else {
        ESP_LOGW(TAG, "a crash was saved but couldn't be read");
    }
    free(s);
    esp_core_dump_image_erase();
}
