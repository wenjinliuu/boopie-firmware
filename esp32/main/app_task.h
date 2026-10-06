/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/*
 * Boopie: short-lived Link tasks (BLE setup, scans, provisioning, VM lookups)
 * with their stacks in PSRAM where the board allows it. With Wi-Fi, BLE, the
 * display and audio up, internal RAM is left in 2-4 KB pieces, and a 4-8 KB
 * internal stack for, say, provisioning failed: the Muse app's Wi-Fi was
 * answered "error_operation_in_progress". On boards whose code and rodata run
 * from PSRAM, flash writes leave the cache on, so a PSRAM stack can do all
 * these tasks do (NVS, TLS, BLE). Not one that maps flash (esp_partition_mmap,
 * and so esp_ota_* on the running image): mapping freezes the cache, which
 * asserts on a PSRAM stack whatever the board. A task made with
 * app_task_spawn() must end with app_task_exit(), and only that.
 */
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_SPIRAM_FETCH_INSTRUCTIONS && CONFIG_SPIRAM_RODATA
#define APP_TASK_PSRAM 1
#else
#define APP_TASK_PSRAM 0
#endif

static inline BaseType_t app_task_spawn(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                                        UBaseType_t prio, TaskHandle_t *out)
{
#if APP_TASK_PSRAM
    return xTaskCreateWithCaps(fn, name, stack, arg, prio, out, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return xTaskCreate(fn, name, stack, arg, prio, out);
#endif
}

static inline void app_task_exit(void)
{
#if APP_TASK_PSRAM
    vTaskDeleteWithCaps(NULL);
#else
    vTaskDelete(NULL);
#endif
}
