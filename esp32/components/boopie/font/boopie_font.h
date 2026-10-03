/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "lvgl.h"

/*
 * Boopie's text fonts for LVGL, both drawn from the same 12 px pixel glyphs
 * (boopie_pixel_font.h), Chinese and English alike:
 *
 *   boopie_font_pixel_24  each pixel a 2 x 2 block, like LVGL's unscii_16:
 *                         a Latin letter 12 px wide, a Chinese character 24.
 *                         Reply text and captions.
 *   boopie_font_pixel_12  1:1, for small screens and dense text.
 *
 * Columns (boopie_text_cols) map straight to pixels, so a layout that counts
 * columns of 'M' fits Chinese too.
 */
extern const lv_font_t boopie_font_pixel_24;
extern const lv_font_t boopie_font_pixel_12;

/*
 * `base` with Chinese: a copy whose missing glyphs fall back to Noto Sans SC
 * (思源黑体, smooth, from the assets partition: docs/boopie-storage.md) at a
 * matching size, and its gaps to the pixel font; with no assets, straight to
 * the pixel font (24 px for bases with a line of 20 or more, else 12). `base`
 * itself if it already has a fallback or there's no room for another copy.
 */
const lv_font_t *boopie_font_with_cjk(const lv_font_t *base);
