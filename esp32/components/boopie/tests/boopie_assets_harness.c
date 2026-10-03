/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Reads the pack BOOPIE_ASSETS names (store/boopie_assets.c, the simulator's
 * path) and prints what it finds as JSON, for test_boopie_assets.py. */

#include <stdio.h>
#include <string.h>

#include "boopie_assets.h"

int main(int argc, char **argv)
{
    printf("{\"ready\":%d,\"version\":%u", boopie_assets_ready(), (unsigned)boopie_assets_version());
    for (int i = 1; i < argc; i++) {
        size_t n = 0;
        const char *p = boopie_assets_get(argv[i], &n);
        printf(",\"%s\":", argv[i]);
        if (p) {
            printf("\"%.*s\"", (int)n, p);
        } else {
            printf("null");
        }
    }
    printf("}\n");
    return 0;
}
