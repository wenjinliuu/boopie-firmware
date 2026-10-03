/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * 重力迷宫 (docs/boopie-interaction.md): tilt the board and a ball rolls
 * through a maze to its way out; each maze cleared brings a bigger one, and
 * a star in each is worth a detour. Plain C with no IDF dependencies; the
 * coordinates are the 64 x 64 grid the pixel renderer draws, the maze a
 * square of cells BOOPIE_MAZE_CELL apart (a wall line and the corridor).
 */

#define BOOPIE_MAZE_SECONDS 90.0f
#define BOOPIE_MAZE_MAX 7         /* cells a side, at most: 43 x 43 on the grid */
#define BOOPIE_MAZE_CELL 6
#define BOOPIE_MAZE_BALL 1.4f     /* the ball's half width */
#define BOOPIE_MAZE_CLEARED 0.7f  /* the pause between mazes */

/* A cell's open sides. */
enum { BOOPIE_MAZE_N = 1, BOOPIE_MAZE_E = 2, BOOPIE_MAZE_S = 4, BOOPIE_MAZE_W = 8 };

typedef struct boopie_maze {
    uint32_t rng;
    float t;                      /* seconds into the round */
    int n;                        /* cells a side, this maze */
    int level;                    /* mazes cleared */
    uint8_t open[BOOPIE_MAZE_MAX * BOOPIE_MAZE_MAX];
    float x, y, vx, vy;           /* the ball, in maze pixels from its top left */
    int star;                     /* the star's cell, or -1 once taken */
    float cleared;                /* > 0: just cleared, the next comes when it runs out */
    int score, stars;
    bool over;
} boopie_maze_t;

void boopie_maze_start(boopie_maze_t *g, uint32_t seed);

/* tilt_x, tilt_y: which way and how far the board leans, in g (+x toward the
 * screen's right, +y toward its bottom). Returns the points scored this tick. */
int boopie_maze_tick(boopie_maze_t *g, float dt, float tilt_x, float tilt_y);

/* The maze's size in pixels (n * CELL + 1), and where its top left sits on the grid. */
int boopie_maze_size(const boopie_maze_t *g);
void boopie_maze_origin(const boopie_maze_t *g, int *x, int *y);

/* A wall at maze pixel (px, py); outside the maze is wall. */
bool boopie_maze_wall(const boopie_maze_t *g, int px, int py);

/* The reward for a score: experience and stars. */
void boopie_maze_reward(int score, int *xp, int *stars);
