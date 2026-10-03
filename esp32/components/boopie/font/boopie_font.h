/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "lvgl.h"

/*
 * Boopie's two text styles, split as Muse's own UI splits them
 * (docs/text-system.md): pixel on the face, smooth on the tool screens.
 *
 * Pixel: Boopie's 12 px pixel font (boopie_pixel_font.h), Chinese and English
 * alike, for the face's replies and captions beside the pixel characters:
 *
 *   boopie_font_pixel_24  each pixel a 2 x 2 block, like LVGL's unscii_16:
 *                         a Latin letter 12 px wide, a Chinese character 24.
 *   boopie_font_pixel_12  1:1, for small screens and dense text.
 *
 * Columns (boopie_text_cols) map straight to pixels, so a layout that counts
 * columns of 'M' fits Chinese too.
 *
 * Smooth: Noto Sans SC (思源黑体, SIL OFL 1.1), built into the firmware
 * (ui.otf: GB2312, Latin, punctuation) and drawn by LVGL's TinyTTF at any
 * size, for settings, the pages, text entry and the guide. Its gaps fall back
 * to the pixel font, which has the rest of GBK. In the LVGL task.
 */
extern const lv_font_t boopie_font_pixel_24;
extern const lv_font_t boopie_font_pixel_12;

/* Noto at `size` px (the em), made once a size; NULL if it can't be made. */
const lv_font_t *boopie_font_ui(int size);

/*
 * `base` with Chinese: a copy whose missing glyphs fall back to Noto at a
 * matching size. `base` itself if it already has a fallback, is the pixel
 * font, or there's no room for another copy.
 */
const lv_font_t *boopie_font_with_cjk(const lv_font_t *base);
