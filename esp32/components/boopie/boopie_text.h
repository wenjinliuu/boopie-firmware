/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Text layout rules shared by Chinese and English, for any brain and any
 * screen. Text is laid out on a grid of columns: a Latin letter takes one,
 * a Chinese character or full-width punctuation mark two (East Asian Wide and
 * Fullwidth). boopie_pixel_font draws exactly these widths, so counting
 * columns is counting pixels.
 */

/* The code point at s and its length in *len (at least 1 on a non-empty
 * string); U+FFFD for a broken sequence, which then counts one byte. */
uint32_t boopie_text_decode(const char *s, size_t *len);

/* Columns the character takes: 2 for wide, else 1. */
int boopie_text_cols(uint32_t cp);

/* Columns the UTF-8 text takes, up to its first newline or NUL. */
int boopie_text_line_cols(const char *s);

/* Closing punctuation (，。！？）」…) that must not start a line. */
bool boopie_text_no_line_start(uint32_t cp);

/* Opening punctuation (（「《“…) that must not end a line. */
bool boopie_text_no_line_end(uint32_t cp);

/*
 * True if a line may break between `before` and `after` when neither is a
 * space: next to a Chinese character, unless that would leave closing
 * punctuation at a line's start or opening punctuation at its end. Latin
 * words still only break at spaces.
 */
bool boopie_text_can_break(uint32_t before, uint32_t after);
