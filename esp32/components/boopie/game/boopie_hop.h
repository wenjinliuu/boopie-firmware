/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * 跳跳布比 (docs/boopie-interaction.md): a tap makes the pet hop, and it
 * falls between taps; pillars slide in from the right, and it has to get
 * through the gaps. Each pillar passed scores, a star in a gap scores more;
 * touching a pillar or the ground ends the round. Plain C with no IDF
 * dependencies; the coordinates are the 64 x 64 grid the pixel renderer draws.
 */

#define BOOPIE_HOP_PIPES 4
#define BOOPIE_HOP_X 20.0f        /* the pet's centre, across */
#define BOOPIE_HOP_PIPE_W 7       /* a pillar's width */
#define BOOPIE_HOP_GROUND 56      /* the ground's top row */
#define BOOPIE_HOP_CEILING 5      /* as high as the pet's head goes */
#define BOOPIE_HOP_HALF_W 4.5f    /* its hit box, round its centre: smaller than it looks */
#define BOOPIE_HOP_HALF_H 3.5f
#define BOOPIE_HOP_FALL 0.8f      /* seconds it tumbles after a crash */

typedef struct {
    float x;                      /* its left edge */
    float gap_y, gap_h;           /* the gap's centre and height */
    bool passed;
    bool star;                    /* a star in the gap, until taken */
} boopie_hop_pipe_t;

typedef enum {
    BOOPIE_HOP_NOTHING = 0,
    BOOPIE_HOP_PASSED,            /* through a gap: 1 */
    BOOPIE_HOP_STAR,              /* a star: 2 */
    BOOPIE_HOP_CRASHED,
} boopie_hop_event_t;

typedef struct boopie_hop {
    uint32_t rng;
    float t;                      /* seconds into the round */
    float y, vy;                  /* the pet's centre, and how fast it falls */
    float hopped;                 /* seconds since the last hop */
    float speed;                  /* how fast the pillars come, cells a second */
    float crashed;                /* > 0: seconds since it crashed */
    int score, passed, stars;
    bool over;
    boopie_hop_pipe_t pipes[BOOPIE_HOP_PIPES];
} boopie_hop_t;

void boopie_hop_start(boopie_hop_t *g, uint32_t seed);

/* A tap: up it goes (not once it's crashed). */
void boopie_hop_flap(boopie_hop_t *g);

/* Plays dt seconds; what happened, for its sound. */
boopie_hop_event_t boopie_hop_tick(boopie_hop_t *g, float dt);

/* The reward for a score: experience and stars. */
void boopie_hop_reward(int score, int *xp, int *stars);
