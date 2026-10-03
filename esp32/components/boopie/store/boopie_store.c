/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_store.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include "esp_littlefs.h"
#include "esp_log.h"
#define BASE "/data"
#define LABEL "userdata"
static const char *TAG = "boopie_store";
#else
#define SIM_TOTAL (0x5B0000)   /* as the partition */
#endif

static const char *const DIRS[BOOPIE_STORE_COUNT] = { "chat", "album", "notes", "logs", "games" };
static const char *const NAMES[BOOPIE_STORE_COUNT] = { "聊天记录", "相册", "留言", "日志", "游戏存档" };

static bool s_ready;
static char s_base[64];
static char s_dirs[BOOPIE_STORE_COUNT][80];

static void make_dirs(void)
{
    for (int i = 0; i < BOOPIE_STORE_COUNT; i++) {
        snprintf(s_dirs[i], sizeof s_dirs[i], "%s/%s", s_base, DIRS[i]);
        mkdir(s_dirs[i], 0755);
    }
}

bool boopie_store_mount(void)
{
    if (s_ready) {
        return true;
    }
#ifdef ESP_PLATFORM
    snprintf(s_base, sizeof s_base, "%s", BASE);
    esp_vfs_littlefs_conf_t conf = {
        .base_path = BASE,
        .partition_label = LABEL,
        .format_if_mount_failed = true,   /* blank on the first boot, or damaged */
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "user data unavailable (%s)", esp_err_to_name(err));
        return false;
    }
#else
    const char *base = getenv("BOOPIE_DATA");
    snprintf(s_base, sizeof s_base, "%s", base ? base : "/tmp/boopie-data");
    mkdir(s_base, 0755);
#endif
    make_dirs();
    s_ready = true;
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG, "user data: %u of %u KB used", (unsigned)(boopie_store_used() / 1024),
             (unsigned)(boopie_store_total() / 1024));
#endif
    return true;
}

bool boopie_store_ready(void)
{
    return s_ready;
}

const char *boopie_store_dir(boopie_store_kind_t kind)
{
    return (int)kind >= 0 && kind < BOOPIE_STORE_COUNT && s_ready ? s_dirs[kind] : NULL;
}

const char *boopie_store_name(boopie_store_kind_t kind)
{
    return (int)kind >= 0 && kind < BOOPIE_STORE_COUNT ? NAMES[kind] : NULL;
}

/* Files directly in a folder: their bytes, or deleting them. */
static size_t walk(const char *dir, bool remove_them)
{
    size_t n = 0;
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    struct dirent *e;
    char path[96 + 256];   /* the folder, and a name as long as dirent allows */
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            n += (size_t)st.st_size;
            if (remove_them) {
                unlink(path);
            }
        }
    }
    closedir(d);
    return n;
}

size_t boopie_store_kind_bytes(boopie_store_kind_t kind)
{
    const char *dir = boopie_store_dir(kind);
    return dir ? walk(dir, false) : 0;
}

size_t boopie_store_used(void)
{
#ifdef ESP_PLATFORM
    size_t total = 0, used = 0;
    if (s_ready && esp_littlefs_info(LABEL, &total, &used) == ESP_OK) {
        return used;
    }
    return 0;
#else
    size_t n = 0;
    for (int i = 0; i < BOOPIE_STORE_COUNT; i++) {
        n += boopie_store_kind_bytes((boopie_store_kind_t)i);
    }
    return n;
#endif
}

size_t boopie_store_total(void)
{
#ifdef ESP_PLATFORM
    size_t total = 0, used = 0;
    if (s_ready && esp_littlefs_info(LABEL, &total, &used) == ESP_OK) {
        return total;
    }
    return 0;
#else
    return SIM_TOTAL;
#endif
}

bool boopie_store_clear(boopie_store_kind_t kind)
{
    const char *dir = boopie_store_dir(kind);
    if (!dir) {
        return false;
    }
    walk(dir, true);
    return true;
}

bool boopie_store_wipe(void)
{
#ifdef ESP_PLATFORM
    if (esp_littlefs_format(LABEL) != ESP_OK) {
        return false;
    }
    if (s_ready) {
        make_dirs();
    }
    return true;
#else
    for (int i = 0; i < BOOPIE_STORE_COUNT; i++) {
        boopie_store_clear((boopie_store_kind_t)i);
    }
    return true;
#endif
}
