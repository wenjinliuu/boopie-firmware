/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Boopie's UI font (ui.otf: Noto Sans SC, built into the firmware) measured
 * without LVGL, so text can be laid out from any task: which characters it
 * has, and how wide each is at a size, rounded as LVGL's TinyTTF rounds them.
 */

/* Generated tables (boopie_ui_metrics_data.c). */
extern const uint16_t boopie_ui_units_per_em;
extern const int16_t boopie_ui_ascent, boopie_ui_descent, boopie_ui_line_gap;
extern const uint32_t boopie_ui_count;
extern const uint16_t boopie_ui_cps[];
extern const uint16_t boopie_ui_advance_units[];

bool boopie_ui_has(uint32_t cp);

/* What TinyTTF advances for cp at `size` px (the em); for a character the
 * font lacks, LVGL's placeholder box. */
int boopie_ui_advance(uint32_t cp, int size);

/* TinyTTF's line height at `size` px. */
int boopie_ui_line_height(int size);
