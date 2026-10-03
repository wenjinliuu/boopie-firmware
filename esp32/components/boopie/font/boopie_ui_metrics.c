/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_ui_metrics.h"

static int32_t find(uint32_t cp)
{
    int32_t lo = 0, hi = (int32_t)boopie_ui_count - 1;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        if (boopie_ui_cps[mid] == cp) {
            return mid;
        }
        if (boopie_ui_cps[mid] < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

bool boopie_ui_has(uint32_t cp)
{
    return find(cp) >= 0;
}

/* stb_truetype's scale, as TinyTTF takes it. */
static float scale(int size)
{
    return (float)size / (float)boopie_ui_units_per_em;
}

int boopie_ui_line_height(int size)
{
    return (int)(scale(size) * (float)(boopie_ui_ascent - boopie_ui_descent + boopie_ui_line_gap));
}

int boopie_ui_advance(uint32_t cp, int size)
{
    int32_t i = find(cp == '\t' ? ' ' : cp);
    if (i < 0) {
        return boopie_ui_line_height(size) / 2 + 2;   /* lv_font.c's placeholder */
    }
    return (uint16_t)(scale(size) * (float)boopie_ui_advance_units[i] + 0.5f);
}
