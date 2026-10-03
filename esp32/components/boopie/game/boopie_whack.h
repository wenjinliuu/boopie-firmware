/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * 戳戳布比 (docs/boopie-interaction.md): the pet pops out of eight holes round
 * the screen and is tapped for points. Plain C with no IDF dependencies; the
 * coordinates are the 64 x 64 grid the pixel renderer draws.
 */

#define BOOPIE_WHACK_HOLES 8
#define BOOPIE_WHACK_SECONDS 60.0f
#define BOOPIE_WHACK_COMBO 5      /* hits in a row that double the points */

typedef enum {
    BOOPIE_WHACK_NONE = 0,
    BOOPIE_WHACK_PET,             /* 1 point */
    BOOPIE_WHACK_GOLD,            /* 3 */
    BOOPIE_WHACK_CLOUD,           /* -2 if tapped */
} boopie_whack_kind_t;

typedef struct {
    boopie_whack_kind_t kind;
    float t;                      /* seconds since it began to rise */
    float stay;                   /* how long it stays up */
    bool hit;                     /* tapped: it ducks, showing what it scored */
    float hit_t;
    int8_t points;                /* what the tap scored */
} boopie_whack_hole_t;

typedef struct boopie_whack {
    uint32_t rng;
    float t;                      /* seconds into the round */
    float next;                   /* when the next one pops up */
    int score, combo, best_combo, hits, misses;
    bool over;
    boopie_whack_hole_t holes[BOOPIE_WHACK_HOLES];
} boopie_whack_t;

/* A hole's centre, in grid cells. */
void boopie_whack_hole_pos(int hole, float *x, float *y);

void boopie_whack_start(boopie_whack_t *g, uint32_t seed);
void boopie_whack_tick(boopie_whack_t *g, float dt);

/* A tap at grid (x, y): the points it scored (0: nothing there). */
int boopie_whack_tap(boopie_whack_t *g, float x, float y);

/* How far a hole's occupant is out of it, 0..1. */
float boopie_whack_rise(const boopie_whack_hole_t *h);

/* The reward for a score: experience and stars. */
void boopie_whack_reward(int score, int *xp, int *stars);
