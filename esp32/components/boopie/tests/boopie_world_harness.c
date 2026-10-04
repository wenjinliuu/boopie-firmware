/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drives world/boopie_world.c for test_boopie_world.py. Arguments are steps:
 * "lv:N" (the level), "tap:X:Y", "tick:SECONDS" (in 1/30 s steps), "sleep:0|1",
 * "things" (each room's things: art, x, y, shown, its box), "wander:SECONDS"
 * (the furthest it strays). Each prints one JSON line.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_world.h"

int main(int argc, char **argv)
{
    boopie_world_t w;
    boopie_world_init(&w, 7);
    int level = 1;
    for (int i = 1; i < argc; i++) {
        char op[16] = "";
        float a = 0, b = 0;
        sscanf(argv[i], "%15[^:]:%f:%f", op, &a, &b);
        if (!strcmp(op, "lv")) {
            level = (int)a;
            printf("{}\n");
        } else if (!strcmp(op, "tap")) {
            int r = boopie_world_tap(&w, level, a, b);
            const boopie_thing_t *t = r >= 0 ? boopie_world_thing(&w, r) : NULL;
            printf("{\"tap\": %d, \"act\": %d}\n", r, t ? t->act : -1);
        } else if (!strcmp(op, "tick")) {
            int acts[16], n = 0;
            for (int k = 0; k < (int)(a * 30); k++) {
                boopie_do_t d = boopie_world_tick(&w, level, 1.0f / 30);
                if (d != BOOPIE_DO_NOTHING && n < 16) {
                    acts[n++] = d;
                }
            }
            printf("{\"room\": %d, \"x\": %.1f, \"y\": %.1f, \"state\": %d, \"cam\": %.1f, \"acts\": [", w.room, w.x,
                   w.y, w.state, w.cam);
            for (int k = 0; k < n; k++) {
                printf("%s%d", k ? ", " : "", acts[k]);
            }
            printf("]}\n");
        } else if (!strcmp(op, "sleep")) {
            boopie_world_sleep(&w, level, a != 0);
            printf("{\"state\": %d}\n", w.state);
        } else if (!strcmp(op, "wander")) {
            float far = 0, min_y = 999, max_y = -1, min_x = 999, max_x = -1;
            for (int k = 0; k < (int)(a * 30); k++) {
                boopie_world_tick(&w, level, 1.0f / 30);
                float d = hypotf(w.x - 78, w.y - 78);
                far = d > far ? d : far;
                min_x = fminf(min_x, w.x); max_x = fmaxf(max_x, w.x);
                min_y = fminf(min_y, w.y); max_y = fmaxf(max_y, w.y);
            }
            printf("{\"far\": %.1f, \"x\": [%.1f, %.1f], \"y\": [%.1f, %.1f], \"walked\": %.1f}\n", far, min_x, max_x,
                   min_y, max_y, w.walked);
        } else if (!strcmp(op, "things")) {
            printf("[");
            for (int r = 0; r < BOOPIE_ROOM_COUNT; r++) {
                int n;
                const boopie_thing_t *t = boopie_room_things((boopie_room_t)r, &n);
                for (int k = 0; k < n; k++) {
                    const boopie_art_t *art = &boopie_art[t[k].art];
                    int top = t[k].y - art->ay - (t[k].hint ? 16 : 0);
                    printf("%s{\"room\": %d, \"art\": %d, \"x\": %d, \"y\": %d, \"act\": %d, \"shown\": %d, "
                           "\"box\": [%d, %d, %d, %d], \"use\": [%d, %d]}",
                           r || k ? ", " : "", r, t[k].art, t[k].x, t[k].y, t[k].act, boopie_thing_shown(&t[k], level),
                           t[k].x - art->ax, top, t[k].x - art->ax + art->w, t[k].y, t[k].x + t[k].use_dx,
                           t[k].y + t[k].use_dy);
                }
            }
            printf("]\n");
        } else {
            return 2;
        }
    }
    return 0;
}
