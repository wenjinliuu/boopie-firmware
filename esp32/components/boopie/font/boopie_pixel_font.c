/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_pixel_font.h"

#define CELL_BYTES BOOPIE_PIXEL_CELL_BYTES

int32_t boopie_pixel_find(uint32_t cp)
{
    if (cp > 0xFFFF) {
        return -1;
    }
    uint32_t lo = 0, hi = boopie_pixel_glyph_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (boopie_pixel_cps[mid] == cp) {
            return (int32_t)mid;
        }
        if (boopie_pixel_cps[mid] < cp) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return -1;
}

int boopie_pixel_width(int32_t index)
{
    bool wide = boopie_pixel_wide[index / 8] & (0x80 >> (index % 8));
    return wide ? BOOPIE_PIXEL_CELL : BOOPIE_PIXEL_HALF;
}

#ifdef BOOPIE_DATA_IN_ASSETS
/*
 * On the device the bitmaps (354 KB) stay in the assets partition
 * (fonts/pixel12.bits, tools/boopie/pack_assets.py --firmware-data) and are
 * read a cell at a time into a small cache, rather than sitting in PSRAM.
 * Any task may draw with the font, so the cache has a lock. Without the
 * assets every glyph reads blank.
 */
#include <string.h>

#include "boopie_assets.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define SLOTS 256   /* direct-mapped by index: a screen's worth of glyphs */

EXT_RAM_BSS_ATTR static uint8_t s_cells[SLOTS][CELL_BYTES];
static int32_t s_tags[SLOTS];
static uint32_t s_base = UINT32_MAX;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;

__attribute__((constructor)) static void cells_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    for (int i = 0; i < SLOTS; i++) {
        s_tags[i] = -1;
    }
}

static void cell(int32_t index, uint8_t out[CELL_BYTES])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int slot = (int)((uint32_t)index % SLOTS);
    if (s_tags[slot] != index) {
        uint32_t size;
        if (s_base == UINT32_MAX && !boopie_assets_find("fonts/pixel12.bits", &s_base, &size)) {
            s_base = UINT32_MAX;
        }
        if (s_base == UINT32_MAX
            || !boopie_assets_read(s_base + (uint32_t)index * CELL_BYTES, s_cells[slot], CELL_BYTES)) {
            memset(s_cells[slot], 0, CELL_BYTES);
            s_tags[slot] = -1;
        } else {
            s_tags[slot] = index;
        }
    }
    memcpy(out, s_cells[slot], CELL_BYTES);
    xSemaphoreGive(s_lock);
}
#else
static void cell(int32_t index, uint8_t out[CELL_BYTES])
{
    for (int i = 0; i < CELL_BYTES; i++) {
        out[i] = boopie_pixel_bits[(uint32_t)index * CELL_BYTES + i];
    }
}
#endif

void boopie_pixel_cell(int32_t index, uint8_t out[BOOPIE_PIXEL_CELL_BYTES])
{
    cell(index, out);
}

bool boopie_pixel_dot(int32_t index, int x, int y)
{
    uint8_t c[CELL_BYTES];
    cell(index, c);
    uint32_t bit = (uint32_t)y * BOOPIE_PIXEL_CELL + (uint32_t)x;
    return c[bit / 8] & (0x80 >> (bit % 8));
}

bool boopie_pixel_blank(int32_t index)
{
    uint8_t c[CELL_BYTES];
    cell(index, c);
    const uint8_t *cell_bytes = c;
    for (int i = 0; i < CELL_BYTES; i++) {
        if (cell_bytes[i]) {
            return false;
        }
    }
    return true;
}
