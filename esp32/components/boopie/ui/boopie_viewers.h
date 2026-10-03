/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

/*
 * Looking back (docs/boopie-storage.md): the chat history and the album,
 * full screen over everything, from the apps page; and the box that asks
 * before clearing them when the AI's asked to. Built into the muse component
 * with the pages. _locked calls run in the LVGL task; the rest take the
 * display lock themselves.
 */

void boopie_viewer_chat_locked(void);
void boopie_viewer_album_locked(void);

/* A viewer or the box is up; closing it (the bottom button). */
bool boopie_viewer_active(void);
void boopie_viewer_close(void);

/*
 * Asks on screen before clearing what was named: "chat", "album", "notes"
 * or "all". Clears only if the user taps to. False for a name it doesn't know.
 */
bool boopie_viewer_ask_clear(const char *what);

/* Clears a kind now (the storage page's second tap): "chat", "album", "notes". */
void boopie_viewer_clear(const char *what);
