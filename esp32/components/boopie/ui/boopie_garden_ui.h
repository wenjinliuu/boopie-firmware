/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * 小花园's screen (docs/boopie-interaction.md), full screen over everything,
 * from the apps page: three pots on the pixel grid under the day's sky; tap
 * one to plant, water or harvest. Built into the muse component with the
 * pages. _locked calls run in the LVGL task; the rest take the display lock.
 */

void boopie_garden_ui_open_locked(void);

bool boopie_garden_ui_active(void);
void boopie_garden_ui_close(void);

/* The AI's garden.status: each pot, in English, into out. */
void boopie_garden_ui_status(char *out, size_t cap);
