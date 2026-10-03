/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_pixel_font.h"

#define CELL_BYTES (BOOPIE_PIXEL_CELL * BOOPIE_PIXEL_CELL / 8)

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

bool boopie_pixel_dot(int32_t index, int x, int y)
{
    uint32_t bit = (uint32_t)y * BOOPIE_PIXEL_CELL + (uint32_t)x;
    return boopie_pixel_bits[(uint32_t)index * CELL_BYTES + bit / 8] & (0x80 >> (bit % 8));
}

bool boopie_pixel_blank(int32_t index)
{
    const uint8_t *cell = &boopie_pixel_bits[(uint32_t)index * CELL_BYTES];
    for (int i = 0; i < CELL_BYTES; i++) {
        if (cell[i]) {
            return false;
        }
    }
    return true;
}
