/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_hop.h"

#include <string.h>

#define GRAVITY 125.0f     /* cells a second squared */
#define HOP -38.0f         /* a hop's speed: about 6 cells up */
#define FALL_MAX 60.0f
#define SPACING 31.0f      /* between pillars */
#define SPEED0 15.0f
#define SPEED_MAX 24.0f
#define GAP0 23.0f
#define GAP_MIN 16.5f

static uint32_t rnd(boopie_hop_t *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

static float frand(boopie_hop_t *g)
{
    return (float)(rnd(g) % 10000) / 10000.0f;
}

/* A pillar at x, its gap not too far from the one before it. */
static void place(boopie_hop_t *g, boopie_hop_pipe_t *p, float x, float prev_gap)
{
    int n = g->passed + BOOPIE_HOP_PIPES;   /* about how many came before it */
    p->x = x;
    p->gap_h = GAP0 - 0.35f * (float)n;
    p->gap_h = p->gap_h < GAP_MIN ? GAP_MIN : p->gap_h;
    float lo = 6 + p->gap_h / 2, hi = BOOPIE_HOP_GROUND - 4 - p->gap_h / 2;
    float y = prev_gap + (frand(g) * 2 - 1) * 13;
    p->gap_y = y < lo ? lo : y > hi ? hi : y;
    p->passed = false;
    p->star = rnd(g) % 4 == 0;
}

void boopie_hop_start(boopie_hop_t *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 1;
    g->y = 30;
    g->hopped = 1;
    g->speed = SPEED0;
    float prev = 30;
    for (int i = 0; i < BOOPIE_HOP_PIPES; i++) {
        /* The first comes in from off the screen's right. */
        place(g, &g->pipes[i], 66 + i * SPACING, prev);
        prev = g->pipes[i].gap_y;
    }
}

void boopie_hop_flap(boopie_hop_t *g)
{
    if (!g->over && g->crashed <= 0) {
        g->vy = HOP;
        g->hopped = 0;
    }
}

static bool hits(const boopie_hop_t *g, const boopie_hop_pipe_t *p)
{
    if (BOOPIE_HOP_X + BOOPIE_HOP_HALF_W <= p->x || BOOPIE_HOP_X - BOOPIE_HOP_HALF_W >= p->x + BOOPIE_HOP_PIPE_W) {
        return false;
    }
    return g->y - BOOPIE_HOP_HALF_H < p->gap_y - p->gap_h / 2 || g->y + BOOPIE_HOP_HALF_H > p->gap_y + p->gap_h / 2;
}

boopie_hop_event_t boopie_hop_tick(boopie_hop_t *g, float dt)
{
    if (g->over) {
        return BOOPIE_HOP_NOTHING;
    }
    g->t += dt;
    g->hopped += dt;
    /* It falls, on its own or tumbling after a crash. */
    g->vy += GRAVITY * dt;
    g->vy = g->vy > FALL_MAX ? FALL_MAX : g->vy;
    g->y += g->vy * dt;
    float floor_y = BOOPIE_HOP_GROUND - BOOPIE_HOP_HALF_H - 1;
    if (g->crashed > 0) {
        g->crashed += dt;
        if (g->y > floor_y) {
            g->y = floor_y;
        }
        if (g->crashed >= BOOPIE_HOP_FALL) {
            g->over = true;
        }
        return BOOPIE_HOP_NOTHING;
    }
    if (g->y < BOOPIE_HOP_CEILING + BOOPIE_HOP_HALF_H) {
        g->y = BOOPIE_HOP_CEILING + BOOPIE_HOP_HALF_H;   /* the top holds it, no harm done */
        g->vy = g->vy < 0 ? 0 : g->vy;
    }
    boopie_hop_event_t ev = BOOPIE_HOP_NOTHING;
    if (g->y >= floor_y) {
        g->y = floor_y;
        g->crashed = 0.001f;
        return BOOPIE_HOP_CRASHED;
    }
    /* The pillars come on, faster as it goes. */
    g->speed = SPEED0 + 0.08f * g->t;
    g->speed = g->speed > SPEED_MAX ? SPEED_MAX : g->speed;
    float rightmost = 0, rightmost_gap = 30;
    for (int i = 0; i < BOOPIE_HOP_PIPES; i++) {
        if (g->pipes[i].x > rightmost) {
            rightmost = g->pipes[i].x;
            rightmost_gap = g->pipes[i].gap_y;
        }
    }
    for (int i = 0; i < BOOPIE_HOP_PIPES; i++) {
        boopie_hop_pipe_t *p = &g->pipes[i];
        p->x -= g->speed * dt;
        if (p->x + BOOPIE_HOP_PIPE_W < -2) {
            place(g, p, rightmost + SPACING, rightmost_gap);   /* round again, at the back */
            rightmost = p->x;
            rightmost_gap = p->gap_y;
            continue;
        }
        if (hits(g, p)) {
            g->crashed = 0.001f;
            g->vy = g->vy < 0 ? 0 : g->vy;
            return BOOPIE_HOP_CRASHED;
        }
        if (p->star && p->x < BOOPIE_HOP_X + BOOPIE_HOP_HALF_W && p->x + BOOPIE_HOP_PIPE_W > BOOPIE_HOP_X - BOOPIE_HOP_HALF_W) {
            p->star = false;   /* through the gap: it's on the way */
            g->stars++;
            g->score += 2;
            ev = BOOPIE_HOP_STAR;
        }
        if (!p->passed && p->x + BOOPIE_HOP_PIPE_W < BOOPIE_HOP_X - BOOPIE_HOP_HALF_W) {
            p->passed = true;
            g->passed++;
            g->score++;
            if (ev == BOOPIE_HOP_NOTHING) {
                ev = BOOPIE_HOP_PASSED;
            }
        }
    }
    return ev;
}

void boopie_hop_reward(int score, int *xp, int *stars)
{
    /* A pillar every 1.3 to 2 seconds; a star in one in four. To tune on the board. */
    *xp = score >= 30 ? 15 : score >= 15 ? 10 : score >= 6 ? 5 : 0;
    *stars = score >= 50 ? 3 : score >= 30 ? 2 : score >= 15 ? 1 : 0;
}
