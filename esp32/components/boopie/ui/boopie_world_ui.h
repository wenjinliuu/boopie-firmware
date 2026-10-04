/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

/*
 * 小窝 (docs/boopie-world.md): the pet's home, below the face. The rooms at
 * full screen (world/boopie_world.c) with the clock and stars along the top,
 * what the pet says over its head, three buttons along the bottom (功能,
 * 背包, 商店) and a hint by the bottom button that it goes back. Things with
 * a bubble over them do something when tapped; the panels they open
 * (games, books, the bag, the shop, the pet's status) close on a tap
 * outside or the bottom button. Built into the muse component with the
 * pages; LVGL task unless said.
 */

void boopie_world_ui_build(lv_obj_t *tile);

/* The bottom button, from any task: closes a panel if one's open (true). */
bool boopie_world_ui_back(void);

/* Straight into a room ("living", "bedroom", "outside"), the pet at its way
 * in; false for an unknown one. For the simulator; LVGL task. */
bool boopie_world_ui_go(const char *room);

/* Its little something by name ("swim", "butterfly", "tv" ...), started now,
 * if the room has it. For the simulator; LVGL task. */
bool boopie_world_ui_antic(const char *name);

/* The AI's world.weather: today's weather in the pet's world ("sunny",
 * "cloudy", "rain", "snow"), what was done into said. False for a bad key or
 * no clock. Any task. */
bool boopie_world_ui_set_weather(const char *key, char *said, size_t cap);

/* The AI's garden.status: each farm plot, in English, into out. Any task. */
void boopie_world_ui_farm_status(char *out, size_t cap);
