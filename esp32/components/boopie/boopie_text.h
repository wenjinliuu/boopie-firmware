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
 * Fullwidth). A screen drawing a proportional font measures characters
 * itself instead (boopie_text_set_measure).
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

/*
 * How wide characters are drawn, for wrapping to a page of columns: `measure`
 * gives a character's width in pixels and a column is `col_px` pixels, so a
 * line of N columns holds N * col_px pixels of text. NULL goes back to
 * counting columns (boopie_text_cols). Set once, before text is laid out.
 */
typedef int (*boopie_text_measure_t)(uint32_t cp);
void boopie_text_set_measure(boopie_text_measure_t measure, int col_px);

/* The character's width in the measure's units: pixels, or columns. */
int boopie_text_width(uint32_t cp);

/* A column in those units: col_px, or 1. */
int boopie_text_col_width(void);
