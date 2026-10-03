/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/*
 * The pages around the home screen (docs/boopie-interaction.md): apps a swipe
 * right away, cards a pull down, the pet a swipe up; settings stay Muse's, a
 * swipe left. muse_ui.c makes the tiles and calls these; built into the muse
 * component, as boopie_avatar.c is.
 */
void boopie_pages_build(lv_obj_t *apps, lv_obj_t *cards, lv_obj_t *pet);

/* Refreshes the page on screen (NULL: none of these). */
void boopie_pages_tick(lv_obj_t *shown);

/* The time for the home screen, "14:32", or NULL until the clock is known. */
const char *boopie_pages_clock(void);

/*
 * The power menu, over everything: power off, restart, mute, factory reset
 * (asked twice). From any task: these take the display lock.
 */
void boopie_pages_power_menu(void);
bool boopie_pages_menu_open(void);
void boopie_pages_menu_close(void);
