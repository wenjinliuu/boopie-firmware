/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * 白噪音's screen (docs/boopie-interaction.md), full screen over everything,
 * from the apps page: four sounds, how long, and stop. Closing it leaves the
 * sound playing. Built into the muse component with the pages. _locked calls
 * run in the LVGL task; the rest take the display lock themselves.
 */

void boopie_noise_ui_open_locked(void);

bool boopie_noise_ui_active(void);
void boopie_noise_ui_close(void);

/*
 * The AI's noise.play / noise.stop: plays `kind` ("white", "pink", "rain",
 * "waves"; NULL or "" for rain) for `minutes` (0: until stopped; < 0: 30).
 * Says what it did into `out`; false for a kind it doesn't know.
 */
bool boopie_noise_ui_play(const char *kind, int minutes, char *out, size_t cap);
void boopie_noise_ui_stop(char *out, size_t cap);
