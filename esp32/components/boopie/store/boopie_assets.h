/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The assets partition (docs/boopie-storage.md): fonts, sounds, themes, packed
 * by tools/boopie/pack_assets.py. One copy, read-only; updated as a whole, and
 * only when the assets change. Its header is written last, so a pack half
 * written doesn't count: everything that uses an asset falls back to what's
 * built into the firmware (the pixel font, simple tones).
 *
 * The simulator reads the pack from the file BOOPIE_ASSETS names.
 */

#define BOOPIE_ASSETS_MAGIC 0x314B5042u   /* "BPK1" */
#define BOOPIE_ASSET_NAME 48

typedef struct {
    uint32_t magic;
    uint32_t version;       /* the assets' version: the firmware asks for at least one */
    uint32_t count;         /* entries after the header */
    uint32_t size;          /* the whole pack */
    uint32_t crc;           /* CRC-32 of the entries */
    uint32_t reserved[3];
} boopie_assets_header_t;

typedef struct {
    char name[BOOPIE_ASSET_NAME];   /* "fonts/ui.ttf" */
    uint32_t offset;                /* from the pack's start */
    uint32_t size;
    uint32_t crc;                   /* CRC-32 of the data */
    uint32_t reserved;
} boopie_asset_entry_t;

/* Reads the pack's header; false if there's no valid pack. Cheap to call again. */
bool boopie_assets_init(void);
bool boopie_assets_ready(void);
uint32_t boopie_assets_version(void);

/* An asset's bytes, mapped (read-only, stays valid), and its size; NULL if
 * it isn't there. */
const void *boopie_assets_get(const char *name, size_t *size);

/* For tests and tools: a pack in memory. */
bool boopie_assets_parse(const uint8_t *pack, size_t n, boopie_assets_header_t *hdr);
uint32_t boopie_assets_crc32(const void *data, size_t n);
