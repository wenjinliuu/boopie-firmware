/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "lvgl.h"

/*
 * Boopie's text font: Noto Sans SC (思源黑体, SIL OFL 1.1), built into the
 * firmware (ui.otf, every GB2312 character with Latin and punctuation) and
 * drawn by LVGL's TinyTTF at any size, anti-aliased. Characters outside it
 * (rare ones past GB2312) show as LVGL's placeholder box.
 * boopie_ui_metrics.h measures it without LVGL. In the LVGL task.
 */

/* The font at `size` px (the em: a Chinese character is this wide), made
 * once a size; NULL if it can't be made. */
const lv_font_t *boopie_font_ui(int size);

/*
 * `base` with Chinese: a copy whose missing glyphs fall back to the font at
 * a matching size. `base` itself if it already has a fallback or there's no
 * room for another copy.
 */
const lv_font_t *boopie_font_with_cjk(const lv_font_t *base);
