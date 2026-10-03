/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/*
 * Pinyin on a phone's nine keys (2 abc ... 9 wxyz), one character at a time:
 * the keys pressed match the syllables they spell, and a syllable lists its
 * characters most used first (boopie_pinyin_dict.c, generated). Plain C.
 */

typedef struct {
    const char *py;       /* "ni" */
    const char *hanzi;    /* "你呢尼..." in UTF-8 */
} boopie_pinyin_entry_t;

extern const boopie_pinyin_entry_t BOOPIE_PINYIN_DICT[];   /* most used syllable first */
extern const int BOOPIE_PINYIN_COUNT;

/* The key a letter is on, '2' ... '9'; 0 for anything else. */
char boopie_pinyin_key(char letter);

/* The syllables `keys` ("64") spell: those they spell whole first, then those
 * they begin, each most used first. Fills out[] with BOOPIE_PINYIN_DICT
 * indices, up to max; returns how many. */
int boopie_pinyin_match(const char *keys, int *out, int max);
