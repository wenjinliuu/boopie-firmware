/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_partition.h"
#define PARTITION_SUBTYPE 0x50   /* partitions_boopie_32mb.csv */
#define MAX_MAPPED 16
static const char *TAG = "boopie_assets";
#endif

static bool s_ready;
static boopie_assets_header_t s_hdr;
static boopie_asset_entry_t *s_entries;
#ifndef ESP_PLATFORM
static uint8_t *s_pack;   /* the simulator's, read whole */
#endif

uint32_t boopie_assets_crc32(const void *data, size_t n)
{
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

/* The header and entries are sane: the magic, the entries' CRC, every asset inside the pack. */
static bool valid(const boopie_assets_header_t *h, const boopie_asset_entry_t *e, size_t room)
{
    if (h->magic != BOOPIE_ASSETS_MAGIC || h->size > room || h->count > 4096
        || sizeof(*h) + (size_t)h->count * sizeof(*e) > h->size) {
        return false;
    }
    if (boopie_assets_crc32(e, (size_t)h->count * sizeof(*e)) != h->crc) {
        return false;
    }
    for (uint32_t i = 0; i < h->count; i++) {
        if (e[i].name[BOOPIE_ASSET_NAME - 1] != '\0' || e[i].offset > h->size || e[i].size > h->size - e[i].offset) {
            return false;
        }
    }
    return true;
}

bool boopie_assets_parse(const uint8_t *pack, size_t n, boopie_assets_header_t *hdr)
{
    if (n < sizeof(*hdr)) {
        return false;
    }
    memcpy(hdr, pack, sizeof(*hdr));
    if (hdr->count > 4096 || sizeof(*hdr) + (size_t)hdr->count * sizeof(boopie_asset_entry_t) > n) {
        return false;
    }
    return valid(hdr, (const boopie_asset_entry_t *)(pack + sizeof(*hdr)), n);
}

#ifdef ESP_PLATFORM
static const esp_partition_t *s_part;
static struct {
    uint32_t index;
    const void *ptr;
    esp_partition_mmap_handle_t handle;
} s_mapped[MAX_MAPPED];
static int s_nmapped;

bool boopie_assets_init(void)
{
    if (s_ready) {
        return true;
    }
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, PARTITION_SUBTYPE, "assets");
    if (!s_part || esp_partition_read(s_part, 0, &s_hdr, sizeof(s_hdr)) != ESP_OK) {
        ESP_LOGW(TAG, "no assets partition");
        return false;
    }
    if (s_hdr.magic != BOOPIE_ASSETS_MAGIC || s_hdr.count > 4096) {
        ESP_LOGW(TAG, "no assets: the firmware's own font and tones meanwhile");
        return false;
    }
    size_t n = (size_t)s_hdr.count * sizeof(boopie_asset_entry_t);
    s_entries = malloc(n ? n : 1);
    if (!s_entries || esp_partition_read(s_part, sizeof(s_hdr), s_entries, n) != ESP_OK
        || !valid(&s_hdr, s_entries, s_part->size)) {
        ESP_LOGW(TAG, "assets damaged: the firmware's own font and tones meanwhile");
        free(s_entries);
        s_entries = NULL;
        return false;
    }
    s_ready = true;
    ESP_LOGI(TAG, "assets v%u: %u files, %u KB", (unsigned)s_hdr.version, (unsigned)s_hdr.count,
             (unsigned)(s_hdr.size / 1024));
    return true;
}

const void *boopie_assets_get(const char *name, size_t *size)
{
    if (!s_ready && !boopie_assets_init()) {
        return NULL;
    }
    for (uint32_t i = 0; i < s_hdr.count; i++) {
        if (strcmp(s_entries[i].name, name) != 0) {
            continue;
        }
        if (size) {
            *size = s_entries[i].size;
        }
        for (int k = 0; k < s_nmapped; k++) {
            if (s_mapped[k].index == i) {
                return s_mapped[k].ptr;
            }
        }
        if (s_nmapped == MAX_MAPPED) {
            ESP_LOGW(TAG, "too many assets mapped for %s", name);
            return NULL;
        }
        const void *ptr;
        esp_partition_mmap_handle_t h;
        if (esp_partition_mmap(s_part, s_entries[i].offset, s_entries[i].size, ESP_PARTITION_MMAP_DATA, &ptr, &h)
            != ESP_OK) {
            ESP_LOGW(TAG, "can't map %s", name);
            return NULL;
        }
        s_mapped[s_nmapped].index = i;
        s_mapped[s_nmapped].ptr = ptr;
        s_mapped[s_nmapped].handle = h;
        s_nmapped++;
        return ptr;
    }
    return NULL;
}
#else
bool boopie_assets_init(void)
{
    if (s_ready) {
        return true;
    }
    const char *path = getenv("BOOPIE_ASSETS");
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    s_pack = n > 0 ? malloc((size_t)n) : NULL;
    bool ok = s_pack && fread(s_pack, 1, (size_t)n, f) == (size_t)n && boopie_assets_parse(s_pack, (size_t)n, &s_hdr);
    fclose(f);
    if (!ok) {
        free(s_pack);
        s_pack = NULL;
        return false;
    }
    s_entries = (boopie_asset_entry_t *)(s_pack + sizeof(s_hdr));
    s_ready = true;
    return true;
}

const void *boopie_assets_get(const char *name, size_t *size)
{
    if (!s_ready && !boopie_assets_init()) {
        return NULL;
    }
    for (uint32_t i = 0; i < s_hdr.count; i++) {
        if (strcmp(s_entries[i].name, name) == 0) {
            if (size) {
                *size = s_entries[i].size;
            }
            return s_pack + s_entries[i].offset;
        }
    }
    return NULL;
}
#endif

bool boopie_assets_ready(void)
{
    return s_ready || boopie_assets_init();
}

uint32_t boopie_assets_version(void)
{
    return boopie_assets_ready() ? s_hdr.version : 0;
}
