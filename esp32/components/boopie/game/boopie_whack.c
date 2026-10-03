/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_whack.h"

#include <math.h>
#include <string.h>

#define CX 32.0f
#define CY 34.0f
#define RING 20.0f          /* the holes' distance from the centre */
#define HIT_R 6.0f          /* a tap this close to a hole's centre is on it */
#define RISE_S 0.15f        /* popping up, and ducking back */
#define HIT_SHOW_S 0.4f     /* a tapped one shows its points this long */

static uint32_t next_rand(boopie_whack_t *g)
{
    uint32_t x = g->rng;    /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g->rng = x;
}

static float rand01(boopie_whack_t *g)
{
    return (next_rand(g) >> 8) / 16777216.0f;
}

void boopie_whack_hole_pos(int hole, float *x, float *y)
{
    float a = (float)hole / BOOPIE_WHACK_HOLES * 6.2831853f - 1.5707963f;   /* from the top, clockwise */
    *x = CX + RING * cosf(a);
    *y = CY + RING * sinf(a);
}

void boopie_whack_start(boopie_whack_t *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0x9e3779b9u;
    g->next = 0.8f;
}

/* How hard it is, 0..1 through the round. */
static float progress(const boopie_whack_t *g)
{
    float k = g->t / BOOPIE_WHACK_SECONDS;
    return k < 0 ? 0 : k > 1 ? 1 : k;
}

static int up_count(const boopie_whack_t *g)
{
    int n = 0;
    for (int i = 0; i < BOOPIE_WHACK_HOLES; i++) {
        n += g->holes[i].kind != BOOPIE_WHACK_NONE;
    }
    return n;
}

static void pop_up(boopie_whack_t *g)
{
    float k = progress(g);
    int most = 1 + (int)(k * 2.99f);   /* one at a time at first, three by the end */
    if (up_count(g) >= most) {
        return;
    }
    int free_holes[BOOPIE_WHACK_HOLES], n = 0;
    for (int i = 0; i < BOOPIE_WHACK_HOLES; i++) {
        if (g->holes[i].kind == BOOPIE_WHACK_NONE) {
            free_holes[n++] = i;
        }
    }
    if (n == 0) {
        return;
    }
    boopie_whack_hole_t *h = &g->holes[free_holes[next_rand(g) % (uint32_t)n]];
    float r = rand01(g);
    h->kind = r < 0.1f ? BOOPIE_WHACK_GOLD : r < 0.1f + 0.1f + 0.1f * k ? BOOPIE_WHACK_CLOUD : BOOPIE_WHACK_PET;
    h->t = 0;
    h->stay = 1.2f - 0.6f * k + 0.2f * rand01(g);   /* 1.2-1.4 s at first, 0.6-0.8 s by the end */
    h->hit = false;
}

void boopie_whack_tick(boopie_whack_t *g, float dt)
{
    if (g->over) {
        return;
    }
    g->t += dt;
    for (int i = 0; i < BOOPIE_WHACK_HOLES; i++) {
        boopie_whack_hole_t *h = &g->holes[i];
        if (h->kind == BOOPIE_WHACK_NONE) {
            continue;
        }
        h->t += dt;
        if (h->hit) {
            if (h->t - h->hit_t > HIT_SHOW_S) {
                h->kind = BOOPIE_WHACK_NONE;
            }
        } else if (h->t > h->stay + 2 * RISE_S) {
            if (h->kind != BOOPIE_WHACK_CLOUD) {   /* got away: the run of hits ends */
                g->combo = 0;
                g->misses++;
            }
            h->kind = BOOPIE_WHACK_NONE;
        }
    }
    if (g->t >= BOOPIE_WHACK_SECONDS) {
        g->over = true;
        return;
    }
    if (g->t >= g->next) {
        pop_up(g);
        float k = progress(g);
        g->next = g->t + 0.9f - 0.5f * k + 0.3f * rand01(g);
    }
}

static float clamp01(float k)
{
    return k < 0 ? 0 : k > 1 ? 1 : k;
}

float boopie_whack_rise(const boopie_whack_hole_t *h)
{
    if (h->kind == BOOPIE_WHACK_NONE) {
        return 0;
    }
    if (h->hit) {   /* tapped: it ducks at once */
        return clamp01(1 - (h->t - h->hit_t) / RISE_S);
    }
    if (h->t < RISE_S) {
        return h->t / RISE_S;
    }
    return clamp01(1 - (h->t - h->stay - RISE_S) / RISE_S);
}

int boopie_whack_tap(boopie_whack_t *g, float x, float y)
{
    if (g->over) {
        return 0;
    }
    for (int i = 0; i < BOOPIE_WHACK_HOLES; i++) {
        boopie_whack_hole_t *h = &g->holes[i];
        float hx, hy;
        boopie_whack_hole_pos(i, &hx, &hy);
        if (h->kind == BOOPIE_WHACK_NONE || h->hit || boopie_whack_rise(h) < 0.3f
            || (x - hx) * (x - hx) + (y - hy + 2) * (y - hy + 2) > HIT_R * HIT_R) {
            continue;   /* the head stands a little above the hole's centre */
        }
        int points;
        if (h->kind == BOOPIE_WHACK_CLOUD) {
            points = -2;
            g->combo = 0;
        } else {
            points = h->kind == BOOPIE_WHACK_GOLD ? 3 : 1;
            g->combo++;
            g->hits++;
            if (g->combo > g->best_combo) {
                g->best_combo = g->combo;
            }
            if (g->combo >= BOOPIE_WHACK_COMBO) {
                points *= 2;
            }
        }
        g->score += points;
        if (g->score < 0) {
            g->score = 0;
        }
        h->hit = true;
        h->hit_t = h->t;
        h->points = (int8_t)points;
        return points;
    }
    return 0;
}

void boopie_whack_reward(int score, int *xp, int *stars)
{
    /* About 60 pop up in a round; a quick player hitting them all scores ~150
     * with the combos. To tune on the board. */
    *xp = score >= 90 ? 15 : score >= 60 ? 10 : score >= 30 ? 5 : 0;
    *stars = score >= 120 ? 3 : score >= 90 ? 2 : score >= 60 ? 1 : 0;
}
