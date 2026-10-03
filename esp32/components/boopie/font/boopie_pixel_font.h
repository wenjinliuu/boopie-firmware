/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Boopie's bilingual 12 px pixel font, as plain data (no LVGL): Chinese
 * characters and full-width marks are 12 px wide, Latin 6 px, every glyph
 * 12 px tall with its baseline 10 px down. Glyph widths follow
 * boopie_text_cols(). boopie_font.h draws it with LVGL at 1x and 2x.
 */

#define BOOPIE_PIXEL_CELL 12
#define BOOPIE_PIXEL_HALF 6
#define BOOPIE_PIXEL_BASELINE 10

/* Generated tables (boopie_pixel_glyphs.c). */
extern const uint32_t boopie_pixel_glyph_count;
extern const uint16_t boopie_pixel_cps[];
extern const uint8_t boopie_pixel_wide[];
extern const uint8_t boopie_pixel_bits[];

/* The glyph index of cp, or -1 if the font has none. */
int32_t boopie_pixel_find(uint32_t cp);

/* Width in pixels at 1x: 12 or 6. */
int boopie_pixel_width(int32_t index);

/* True if pixel (x, y) of the glyph's 12 x 12 cell is ink. */
bool boopie_pixel_dot(int32_t index, int x, int y);

/* True if the glyph has no ink (a space). */
bool boopie_pixel_blank(int32_t index);
