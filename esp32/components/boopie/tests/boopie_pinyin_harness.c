/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Each argument is a key sequence; prints the syllables it matches and the
 * first characters of the first, as JSON, for test_boopie_pinyin.py. */

#include <stdio.h>
#include <string.h>

#include "boopie_pinyin.h"

int main(int argc, char **argv)
{
    printf("{\"count\":%d", BOOPIE_PINYIN_COUNT);
    for (int a = 1; a < argc; a++) {
        int idx[16];
        int n = boopie_pinyin_match(argv[a], idx, 16);
        printf(",\"%s\":{\"py\":[", argv[a]);
        for (int i = 0; i < n; i++) {
            printf("%s\"%s\"", i ? "," : "", BOOPIE_PINYIN_DICT[idx[i]].py);
        }
        printf("],\"first\":\"");
        if (n) {
            fwrite(BOOPIE_PINYIN_DICT[idx[0]].hanzi, 1, 15, stdout);   /* five characters */
        }
        printf("\"}");
    }
    printf("}\n");
    return 0;
}
