/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drives pet/boopie_garden.c for test_boopie_garden.py. Each argument is a
 * step at the hour set by the last "at:H": "plant:POT:KIND", "water:POT",
 * "harvest:POT", "save" (round trips the blob), "v1" (saves the first three pots
 * as version 1 did and loads that), "v2" (all six, as version 2) or "at:H". After each, a JSON
 * line: what the step returned, and each pot's stage, hours grown, dry, and
 * hours left.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_garden.h"

#define T0 1760000000LL

int main(int argc, char **argv)
{
    boopie_garden_t g;
    boopie_garden_init(&g);
    int64_t now = T0;
    for (int i = 1; i < argc; i++) {
        char op[16] = "";
        int a = 0, b = 0;
        sscanf(argv[i], "%15[^:]:%d:%d", op, &a, &b);
        int ret = 0, xp = 0, stars = 0;
        if (strcmp(op, "at") == 0) {
            now = T0 + (int64_t)a * 3600;
        } else if (strcmp(op, "plant") == 0) {
            boopie_garden_update(&g, now);
            ret = boopie_garden_plant(&g, a, (boopie_plant_t)b, now);
        } else if (strcmp(op, "water") == 0) {
            boopie_garden_update(&g, now);
            ret = boopie_garden_water(&g, a, now);
        } else if (strcmp(op, "harvest") == 0) {
            boopie_garden_update(&g, now);
            ret = boopie_garden_harvest(&g, a, &xp, &stars);
        } else if (strcmp(op, "v1") == 0) {
            struct {
                uint8_t version;
                uint8_t pad[3];
                boopie_pot_t pots[3];
                uint16_t harvested[5];
            } old = { .version = 1 };
            memcpy(old.pots, g.pots, sizeof old.pots);
            memcpy(old.harvested, g.harvested, sizeof old.harvested);
            boopie_garden_t back;
            ret = boopie_garden_load(&back, &old, sizeof old);
            ret += back.version == BOOPIE_GARDEN_VERSION && memcmp(back.pots, g.pots, sizeof old.pots) == 0
                   && back.pots[3].plant == 0 && back.pots[5].plant == 0;
            g = back;
        } else if (strcmp(op, "v2") == 0) {
            struct {
                uint8_t version;
                uint8_t pad[3];
                boopie_pot_t pots[6];
                uint16_t harvested[5];
            } old = { .version = 2 };
            memcpy(old.pots, g.pots, sizeof old.pots);
            memcpy(old.harvested, g.harvested, sizeof old.harvested);
            boopie_garden_t back;
            ret = boopie_garden_load(&back, &old, sizeof old);
            ret += back.version == BOOPIE_GARDEN_VERSION && memcmp(back.pots, g.pots, sizeof old.pots) == 0
                   && memcmp(back.harvested, g.harvested, sizeof old.harvested) == 0 && back.harvested[7] == 0;
            g = back;
        } else if (strcmp(op, "save") == 0) {
            boopie_garden_t back;
            char blob[sizeof g];
            memcpy(blob, &g, sizeof g);
            ret = boopie_garden_load(&back, blob, sizeof blob) && memcmp(&back, &g, sizeof g) == 0;
            ret += !boopie_garden_load(&back, blob, sizeof blob - 1);
        } else {
            return 2;
        }
        boopie_garden_update(&g, now);
        printf("{\"ret\": %d, \"xp\": %d, \"stars\": %d, \"pots\": [", ret, xp, stars);
        for (int p = 0; p < BOOPIE_GARDEN_POTS; p++) {
            printf("%s[%d, %.2f, %d, %.2f]", p ? ", " : "", (int)boopie_garden_stage(&g, p),
                   g.pots[p].grown_s / 3600.0, boopie_garden_dry(&g, p, now), boopie_garden_left_s(&g, p) / 3600.0);
        }
        printf("], \"harvested\": [%d, %d, %d, %d, %d, %d, %d]}\n", g.harvested[1], g.harvested[2], g.harvested[3],
               g.harvested[4], g.harvested[5], g.harvested[6], g.harvested[7]);
    }
    return 0;
}
