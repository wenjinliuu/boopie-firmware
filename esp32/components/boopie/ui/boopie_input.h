/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

/*
 * Text entry for the small round screen, full screen over everything: nine
 * keys spelling pinyin (one character at a time: the keys, then a spelling,
 * then a character), or letters and digits in English mode. In the LVGL task
 * (or with the display lock held).
 */

/* Done (text, true) or cancelled (NULL, false). */
typedef void (*boopie_input_done_t)(const char *text, bool done);

/* Opens it with a title, a starting text, a hint for when it's empty, and at
 * most max_chars characters. */
void boopie_input_open(const char *title, const char *text, const char *hint, int max_chars,
                       boopie_input_done_t done);
bool boopie_input_active(void);
void boopie_input_cancel(void);
