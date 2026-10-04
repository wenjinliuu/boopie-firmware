/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drives world/boopie_world.c for test_boopie_world.py. Arguments are steps:
 * "lv:N" (the level), "tap:X:Y", "tick:SECONDS" (in 1/30 s steps), "sleep:0|1",
 * "things" (each room's things: art, x, y, shown, its box), "wander:SECONDS"
 * (the furthest it strays), "woods" (straight into the woods), "chase" (tap the
 * first slime about, tick till the fight's on), "hit:N" (tap the slime fought
 * N times, a quarter second apart, ticking on to the end), "kinds:N" (the
 * kinds of N slimes met at the level). Each prints one JSON line.
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
        } else if (!strcmp(op, "woods")) {
            w.level = level;
            boopie_world_enter(&w, BOOPIE_ROOM_WOODS, BOOPIE_DO_WILD);
            printf("{\"room\": %d, \"x\": %.1f, \"y\": %.1f}\n", w.room, w.x, w.y);
        } else if (!strcmp(op, "chase")) {
            int s = 0;
            while (s < BOOPIE_SLIMES && w.slimes[s].state != BOOPIE_SLIME_ROAM) {
                s++;
            }
            int r = s < BOOPIE_SLIMES ? boopie_world_tap(&w, level, w.slimes[s].x, w.slimes[s].y - 5) : -9;
            int got = 0;
            float secs = 0;
            for (; secs < 20 && !got; secs += 1.0f / 30) {
                got = boopie_world_tick(&w, level, 1.0f / 30) == BOOPIE_DO_SLIME_FIGHT;
            }
            printf("{\"tap\": %d, \"fight\": %d, \"slime\": %d, \"secs\": %.1f, \"hp\": %d, \"kind\": %d}\n", r, got,
                   w.fight, secs, w.fight >= 0 ? w.slimes[w.fight].hp : -1, w.fight >= 0 ? w.slimes[w.fight].kind : -1);
        } else if (!strcmp(op, "hit")) {
            int f = w.fight, taps[16], nt = 0, acts[8], na = 0;
            float px = w.x, py = w.y, lo = 999, hi = -999, ylo = 999, yhi = -999;
            for (int k = 0; k < (int)a && w.fight >= 0; k++) {
                for (int j = 0; j < 8; j++) {   /* a quarter second, the slime hopping */
                    boopie_do_t d = boopie_world_tick(&w, level, 1.0f / 30);
                    if (d != BOOPIE_DO_NOTHING && na < 8) {
                        acts[na++] = d;
                    }
                    if (w.slimes[f].state == BOOPIE_SLIME_FIGHT) {
                        lo = fminf(lo, w.slimes[f].x - w.cam); hi = fmaxf(hi, w.slimes[f].x - w.cam);
                        ylo = fminf(ylo, w.slimes[f].y); yhi = fmaxf(yhi, w.slimes[f].y);
                    }
                }
                const boopie_slime_t *s = &w.slimes[f];
                int r = boopie_world_tap(&w, level, s->x, s->y - s->z - 5);
                if (nt < 16) {
                    taps[nt++] = r;
                }
            }
            for (int k = 0; k < 30; k++) {
                boopie_do_t d = boopie_world_tick(&w, level, 1.0f / 30);
                if (d != BOOPIE_DO_NOTHING && na < 8) {
                    acts[na++] = d;
                }
            }
            printf("{\"taps\": [");
            for (int k = 0; k < nt; k++) {
                printf("%s%d", k ? ", " : "", taps[k]);
            }
            printf("], \"acts\": [");
            for (int k = 0; k < na; k++) {
                printf("%s%d", k ? ", " : "", acts[k]);
            }
            printf("], \"fight\": %d, \"state\": %d, \"last\": %d, \"moved\": %.1f, \"view\": [%.1f, %.1f], "
                   "\"y\": [%.1f, %.1f]}\n",
                   w.fight, f >= 0 ? w.slimes[f].state : -1, w.last_slime, hypotf(w.x - px, w.y - py), lo, hi, ylo, yhi);
        } else if (!strcmp(op, "watch")) {
            /* The slime fought, untouched for SECONDS: where it hops, on screen. */
            int f = w.fight, fled = 0;
            float lo = 999, hi = -999, yhi = -999;
            for (int k = 0; k < (int)(a * 30); k++) {
                fled |= boopie_world_tick(&w, level, 1.0f / 30) == BOOPIE_DO_SLIME_FLED;
                if (f >= 0 && w.slimes[f].state == BOOPIE_SLIME_FIGHT) {
                    lo = fminf(lo, w.slimes[f].x - w.cam); hi = fmaxf(hi, w.slimes[f].x - w.cam);
                    yhi = fmaxf(yhi, w.slimes[f].y);
                }
            }
            printf("{\"view\": [%.1f, %.1f], \"y\": %.1f, \"fled\": %d}\n", lo, hi, yhi, fled);
        } else if (!strcmp(op, "slimes")) {
            printf("[");
            for (int k = 0; k < BOOPIE_SLIMES; k++) {
                printf("%s{\"state\": %d, \"kind\": %d, \"x\": %.1f, \"y\": %.1f}", k ? ", " : "", w.slimes[k].state,
                       w.slimes[k].kind, w.slimes[k].x, w.slimes[k].y);
            }
            printf("]\n");
        } else if (!strcmp(op, "kinds")) {
            int count[BOOPIE_SLIME_KINDS] = { 0 };
            w.level = level;
            for (int k = 0; k < (int)a; k += BOOPIE_SLIMES) {
                boopie_world_enter(&w, BOOPIE_ROOM_WOODS, BOOPIE_DO_WILD);
                for (int s = 0; s < BOOPIE_SLIMES; s++) {
                    count[w.slimes[s].kind]++;
                }
            }
            printf("[%d, %d, %d, %d]\n", count[0], count[1], count[2], count[3]);
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
