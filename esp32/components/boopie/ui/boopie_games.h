/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

/*
 * The games, full screen over everything (docs/boopie-interaction.md): a
 * start card, play, a pause, and the results with the reward. Built into the
 * muse component with the pages.
 *
 * _locked calls run in the LVGL task (or with the display lock held); the
 * rest take the lock themselves, from any task.
 */

/* Opens a game by id: "whack" (戳戳布比), "catch" (接零食), "maze" (重力迷宫),
 * "hop" (跳跳布比);
 * false if there's no such game, or another is open. */
bool boopie_games_open_locked(const char *game);
bool boopie_games_open(const char *game);

/* A game is on screen: the face isn't drawn meanwhile. */
bool boopie_games_active(void);

/* A button while a game is open: the top one starts and pauses, the bottom
 * one leaves. */
void boopie_games_key(bool top);
