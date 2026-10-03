/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Phone setup on screen (docs/boopie-interaction.md): a QR code to join the
 * board's hotspot, then one for the page, then what the page saved. Full
 * screen over everything; the hotspot is up while it's open and goes when it
 * closes, or after ten minutes with no phone asking for anything.
 *
 * In the LVGL task (or with the display lock held).
 */

/* `done` hears what was saved (BOOPIE_SETUP_SAVED_* bits) once it closes. */
void boopie_setup_open(void (*done)(uint32_t saved));
bool boopie_setup_active(void);
void boopie_setup_close(void);

/* Each frame or so: follows the phone along. */
void boopie_setup_tick(void);
