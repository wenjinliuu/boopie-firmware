/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_catch.h"

#include <math.h>
#include <string.h>

#define SPAWN_Y 5.0f
#define CATCH_HALF 7.5f      /* how far off the pet's centre still catches */
#define SHOWN 0.6f           /* how long a catch shows its points */
#define DIZZY 1.0f

static uint32_t rnd(boopie_catch_t *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

static float frand(boopie_catch_t *g)
{
    return (float)(rnd(g) % 10000) / 10000.0f;
}

void boopie_catch_start(boopie_catch_t *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 1;
    g->x = 32;
    g->next = 0.6f;
}

static void spawn(boopie_catch_t *g)
{
    for (int i = 0; i < BOOPIE_CATCH_ITEMS; i++) {
        boopie_catch_item_t *it = &g->items[i];
        if (it->kind != BOOPIE_CATCH_NONE) {
            continue;
        }
        float r = frand(g);
        /* More clouds as the round goes on. */
        float clouds = 0.12f + 0.12f * g->t / BOOPIE_CATCH_SECONDS;
        it->kind = r < clouds ? BOOPIE_CATCH_CLOUD : r < clouds + 0.1f ? BOOPIE_CATCH_GOLD : BOOPIE_CATCH_FOOD;
        it->food = (uint8_t)(rnd(g) % BOOPIE_CATCH_FOODS);
        /* Anywhere the pet can reach; of two tries, the one further from it, so it
         * has to move. */
        float x1 = 32 + (frand(g) * 2 - 1) * 19, x2 = 32 + (frand(g) * 2 - 1) * 19;
        it->x = fabsf(x1 - g->x) > fabsf(x2 - g->x) ? x1 : x2;
        it->y = SPAWN_Y;
        /* Faster as the round goes on; a sweet falls fastest. */
        it->vy = 11 + 14 * g->t / BOOPIE_CATCH_SECONDS + frand(g) * 4 + (it->kind == BOOPIE_CATCH_GOLD ? 6 : 0);
        it->caught = false;
        it->points = 0;
        return;
    }
}

int boopie_catch_tick(boopie_catch_t *g, float dt, float move, boopie_catch_kind_t *what)
{
    int scored = 0;
    if (g->over) {
        return 0;
    }
    g->t += dt;
    if (g->t >= BOOPIE_CATCH_SECONDS) {
        g->t = BOOPIE_CATCH_SECONDS;
        g->over = true;
        return 0;
    }
    if (g->dizzy > 0) {
        g->dizzy -= dt;
    } else {
        move = move < -1 ? -1 : move > 1 ? 1 : move;
        g->x += move * BOOPIE_CATCH_SPEED * dt;
        g->x = g->x < BOOPIE_CATCH_PET_MIN ? BOOPIE_CATCH_PET_MIN : g->x > BOOPIE_CATCH_PET_MAX ? BOOPIE_CATCH_PET_MAX : g->x;
    }
    if (g->t >= g->next) {
        spawn(g);
        /* One every 1.1 s at the start, every 0.5 s at the end. */
        g->next = g->t + 1.1f - 0.6f * g->t / BOOPIE_CATCH_SECONDS + frand(g) * 0.3f;
    }
    for (int i = 0; i < BOOPIE_CATCH_ITEMS; i++) {
        boopie_catch_item_t *it = &g->items[i];
        if (it->kind == BOOPIE_CATCH_NONE) {
            continue;
        }
        if (it->caught) {
            if (g->t - it->caught_t > SHOWN) {
                it->kind = BOOPIE_CATCH_NONE;
            }
            continue;
        }
        float was = it->y;
        it->y += it->vy * dt;
        /* Its bottom (about 3 below its centre) crossing the top of the head. */
        float line = BOOPIE_CATCH_PET_Y - 3;
        if (was < line && it->y >= line && fabsf(it->x - g->x) <= CATCH_HALF) {
            it->caught = true;
            it->caught_t = g->t;
            if (it->kind == BOOPIE_CATCH_CLOUD) {
                it->points = -2;
                g->combo = 0;
                g->dizzy = DIZZY;
            } else {
                g->combo++;
                g->caught++;
                if (g->combo > g->best_combo) {
                    g->best_combo = g->combo;
                }
                int p = it->kind == BOOPIE_CATCH_GOLD ? 3 : 1;
                it->points = (int8_t)(g->combo >= BOOPIE_CATCH_COMBO ? p * 2 : p);
            }
            g->score += it->points;
            if (g->score < 0) {
                g->score = 0;
            }
            scored += it->points;
            if (what) {
                *what = it->kind;
            }
            continue;
        }
        if (it->y > BOOPIE_CATCH_PET_Y + 7) {   /* down at the ground: missed */
            if (it->kind != BOOPIE_CATCH_CLOUD) {
                g->missed++;
                g->combo = 0;
            }
            it->kind = BOOPIE_CATCH_NONE;
        }
    }
    return scored;
}

void boopie_catch_reward(int score, int *xp, int *stars)
{
    /* About 75 fall in a round; catching every food with the combos scores
     * ~150. To tune on the board. */
    *xp = score >= 80 ? 15 : score >= 50 ? 10 : score >= 25 ? 5 : 0;
    *stars = score >= 110 ? 3 : score >= 80 ? 2 : score >= 50 ? 1 : 0;
}
