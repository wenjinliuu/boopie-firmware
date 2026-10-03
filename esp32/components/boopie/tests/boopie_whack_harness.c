/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Plays 戳戳布比 (game/boopie_whack.c) as a few kinds of player and prints
 * what happened as JSON, for test_boopie_whack.py. */

#include <stdio.h>

#include "boopie_whack.h"

#define DT (1.0f / 30)

/* A round where the player taps everything that's up (`clouds`: those too)
 * after `react` seconds, or nothing at all. */
static void play(const char *name, uint32_t seed, float react, bool clouds, bool taps)
{
    boopie_whack_t g;
    boopie_whack_start(&g, seed);
    int frames = 0, max_up = 0, clouds_hit = 0;
    while (!g.over && frames < 100000) {
        boopie_whack_tick(&g, DT);
        frames++;
        int up = 0;
        for (int i = 0; i < BOOPIE_WHACK_HOLES; i++) {
            boopie_whack_hole_t *h = &g.holes[i];
            up += h->kind != BOOPIE_WHACK_NONE;
            if (taps && h->kind != BOOPIE_WHACK_NONE && !h->hit && h->t >= react
                && (clouds || h->kind != BOOPIE_WHACK_CLOUD)) {
                float x, y;
                boopie_whack_hole_pos(i, &x, &y);
                clouds_hit += h->kind == BOOPIE_WHACK_CLOUD;
                boopie_whack_tap(&g, x, y - 2);
            }
        }
        max_up = up > max_up ? up : max_up;
    }
    int xp, stars;
    boopie_whack_reward(g.score, &xp, &stars);
    printf("\"%s\":{\"score\":%d,\"hits\":%d,\"misses\":%d,\"best_combo\":%d,\"seconds\":%.2f,"
           "\"max_up\":%d,\"clouds_hit\":%d,\"xp\":%d,\"stars\":%d}",
           name, g.score, g.hits, g.misses, g.best_combo, frames * DT, max_up, clouds_hit, xp, stars);
}

int main(void)
{
    printf("{");
    play("perfect", 1, 0.3f, false, true);
    printf(",");
    play("perfect_again", 1, 0.3f, false, true);
    printf(",");
    play("slow", 1, 1.0f, false, true);
    printf(",");
    play("careless", 1, 0.3f, true, true);
    printf(",");
    play("idle", 1, 0, false, false);

    /* A tap on an empty hole, and one far from any. */
    boopie_whack_t g;
    boopie_whack_start(&g, 7);
    printf(",\"empty_tap\":[%d,%d]", boopie_whack_tap(&g, 32, 14), boopie_whack_tap(&g, 0, 0));

    printf(",\"rewards\":[");
    const int scores[] = { 0, 30, 60, 90, 119, 120 };
    for (unsigned i = 0; i < sizeof scores / sizeof scores[0]; i++) {
        int xp, stars;
        boopie_whack_reward(scores[i], &xp, &stars);
        printf("%s[%d,%d]", i ? "," : "", xp, stars);
    }
    printf("]}\n");
    return 0;
}
