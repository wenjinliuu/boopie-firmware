/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drives world/boopie_season.c for test_boopie_season.py: "fest:Y:M:D" prints
 * the festival that day; "weather:M" the count of each weather over 2000
 * days in month M; "same:DAY:M" the weather of a day twice. One JSON line each.
 */

#include <stdio.h>
#include <string.h>

#include "boopie_season.h"

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        char op[16] = "";
        int a = 0, b = 0, c = 0;
        sscanf(argv[i], "%15[^:]:%d:%d:%d", op, &a, &b, &c);
        if (!strcmp(op, "fest")) {
            printf("%d\n", boopie_fest_on(a, b, c));
        } else if (!strcmp(op, "weather")) {
            int n[BOOPIE_WEATHER_COUNT] = { 0 };
            for (int d = 0; d < 2000; d++) {
                n[boopie_weather_on(20000 + d, a)]++;
            }
            printf("[%d, %d, %d, %d]\n", n[0], n[1], n[2], n[3]);
        } else if (!strcmp(op, "same")) {
            printf("[%d, %d]\n", boopie_weather_on(a, b), boopie_weather_on(a, b));
        } else {
            return 2;
        }
    }
    return 0;
}
