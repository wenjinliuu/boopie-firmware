/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_pinyin.h"

#include <string.h>

char boopie_pinyin_key(char c)
{
    static const char KEYS[] = "22233344455566677778889999";   /* a ... z */
    return c >= 'a' && c <= 'z' ? KEYS[c - 'a'] : 0;
}

/* 2: spells it whole; 1: begins it; 0: neither. */
static int spells(const char *keys, const char *py)
{
    size_t i = 0;
    for (; keys[i]; i++) {
        if (!py[i] || boopie_pinyin_key(py[i]) != keys[i]) {
            return 0;
        }
    }
    return py[i] ? 1 : 2;
}

int boopie_pinyin_match(const char *keys, int *out, int max)
{
    int n = 0;
    if (!keys || !keys[0]) {
        return 0;
    }
    for (int pass = 2; pass >= 1; pass--) {
        for (int i = 0; i < BOOPIE_PINYIN_COUNT && n < max; i++) {
            if (spells(keys, BOOPIE_PINYIN_DICT[i].py) == pass) {
                out[n++] = i;
            }
        }
    }
    return n;
}
