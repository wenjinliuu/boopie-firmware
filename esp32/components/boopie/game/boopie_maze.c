/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_maze.h"

#include <math.h>
#include <string.h>

#define GRAVITY 70.0f     /* pixels a second squared, a board on its side */
#define FRICTION 1.6f     /* a second */
#define TOP_SPEED 34.0f
#define STEP 0.25f        /* moved at most this far between wall checks */
#define BOUNCE 0.3f
#define CLEAR_POINTS 10
#define STAR_POINTS 5

static uint32_t rnd(boopie_maze_t *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

/* A perfect maze (one way between any two cells): a random depth-first walk. */
static void carve(boopie_maze_t *g)
{
    static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };
    static const uint8_t SIDE[4] = { BOOPIE_MAZE_N, BOOPIE_MAZE_E, BOOPIE_MAZE_S, BOOPIE_MAZE_W };
    static const uint8_t BACK[4] = { BOOPIE_MAZE_S, BOOPIE_MAZE_W, BOOPIE_MAZE_N, BOOPIE_MAZE_E };
    int n = g->n;
    uint8_t stack[BOOPIE_MAZE_MAX * BOOPIE_MAZE_MAX];
    bool seen[BOOPIE_MAZE_MAX * BOOPIE_MAZE_MAX] = { false };
    memset(g->open, 0, sizeof(g->open));
    int top = 0;
    stack[top++] = 0;
    seen[0] = true;
    while (top) {
        int c = stack[top - 1], cx = c % n, cy = c / n;
        int ways[4], k = 0;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx >= 0 && nx < n && ny >= 0 && ny < n && !seen[ny * n + nx]) {
                ways[k++] = d;
            }
        }
        if (!k) {
            top--;
            continue;
        }
        int d = ways[rnd(g) % (uint32_t)k];
        int next = (cy + DY[d]) * n + cx + DX[d];
        g->open[c] |= SIDE[d];
        g->open[next] |= BACK[d];
        seen[next] = true;
        stack[top++] = (uint8_t)next;
    }
}

static void new_maze(boopie_maze_t *g)
{
    g->n = g->level + 4 < BOOPIE_MAZE_MAX ? g->level + 4 : BOOPIE_MAZE_MAX;
    carve(g);
    g->x = g->y = BOOPIE_MAZE_CELL / 2.0f + 0.5f;   /* the middle of the top left cell */
    g->vx = g->vy = 0;
    /* The star: somewhere off to the side, neither end. */
    int cells = g->n * g->n;
    g->star = 1 + (int)(rnd(g) % (uint32_t)(cells - 2));
}

void boopie_maze_start(boopie_maze_t *g, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 1;
    new_maze(g);
}

int boopie_maze_size(const boopie_maze_t *g)
{
    return g->n * BOOPIE_MAZE_CELL + 1;
}

void boopie_maze_origin(const boopie_maze_t *g, int *x, int *y)
{
    *x = *y = (64 - boopie_maze_size(g)) / 2;
}

bool boopie_maze_wall(const boopie_maze_t *g, int px, int py)
{
    int size = boopie_maze_size(g);
    if (px < 0 || py < 0 || px >= size || py >= size) {
        return true;
    }
    bool vline = px % BOOPIE_MAZE_CELL == 0, hline = py % BOOPIE_MAZE_CELL == 0;
    if (vline && hline) {
        return true;   /* the corners between cells */
    }
    if (!vline && !hline) {
        return false;  /* inside a cell */
    }
    int cx = px / BOOPIE_MAZE_CELL, cy = py / BOOPIE_MAZE_CELL;
    if (vline) {
        /* Between cell cx - 1 and cx, in row cy. */
        return cx == 0 || cx == g->n || !(g->open[cy * g->n + cx] & BOOPIE_MAZE_W);
    }
    return cy == 0 || cy == g->n || !(g->open[cy * g->n + cx] & BOOPIE_MAZE_N);
}

/* The ball, a square of half width BALL round (x, y), touches a wall pixel. */
static bool blocked(const boopie_maze_t *g, float x, float y)
{
    const float b = BOOPIE_MAZE_BALL;
    for (int py = (int)floorf(y - b); py <= (int)floorf(y + b - 0.001f); py++) {
        for (int px = (int)floorf(x - b); px <= (int)floorf(x + b - 0.001f); px++) {
            if (boopie_maze_wall(g, px, py)) {
                return true;
            }
        }
    }
    return false;
}

static int cell_of(const boopie_maze_t *g, float x, float y)
{
    int cx = (int)(x / BOOPIE_MAZE_CELL), cy = (int)(y / BOOPIE_MAZE_CELL);
    cx = cx < 0 ? 0 : cx >= g->n ? g->n - 1 : cx;
    cy = cy < 0 ? 0 : cy >= g->n ? g->n - 1 : cy;
    return cy * g->n + cx;
}

int boopie_maze_tick(boopie_maze_t *g, float dt, float tilt_x, float tilt_y)
{
    if (g->over) {
        return 0;
    }
    g->t += dt;
    if (g->t >= BOOPIE_MAZE_SECONDS) {
        g->t = BOOPIE_MAZE_SECONDS;
        g->over = true;
        return 0;
    }
    if (g->cleared > 0) {
        g->cleared -= dt;
        if (g->cleared <= 0) {
            g->cleared = 0;
            new_maze(g);
        }
        return 0;
    }
    tilt_x = tilt_x < -1 ? -1 : tilt_x > 1 ? 1 : tilt_x;
    tilt_y = tilt_y < -1 ? -1 : tilt_y > 1 ? 1 : tilt_y;
    float keep = 1 - FRICTION * dt;
    keep = keep < 0 ? 0 : keep;
    g->vx = (g->vx + tilt_x * GRAVITY * dt) * keep;
    g->vy = (g->vy + tilt_y * GRAVITY * dt) * keep;
    float speed = sqrtf(g->vx * g->vx + g->vy * g->vy);
    if (speed > TOP_SPEED) {
        g->vx *= TOP_SPEED / speed;
        g->vy *= TOP_SPEED / speed;
    }
    /* A step at a time, each axis on its own, so it slides along a wall. */
    float dx = g->vx * dt, dy = g->vy * dt;
    int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)) / STEP);
    for (int i = 0; i < steps; i++) {
        float sx = dx / steps, sy = dy / steps;
        if (sx != 0 && !blocked(g, g->x + sx, g->y)) {
            g->x += sx;
        } else if (sx != 0) {
            g->vx *= -BOUNCE;
            dx = 0;
        }
        if (sy != 0 && !blocked(g, g->x, g->y + sy)) {
            g->y += sy;
        } else if (sy != 0) {
            g->vy *= -BOUNCE;
            dy = 0;
        }
    }
    int points = 0;
    int here = cell_of(g, g->x, g->y);
    if (here == g->star) {
        g->star = -1;
        g->stars++;
        points += STAR_POINTS;
    }
    if (here == g->n * g->n - 1) {
        g->level++;
        points += CLEAR_POINTS;
        g->cleared = BOOPIE_MAZE_CLEARED;
    }
    g->score += points;
    return points;
}

void boopie_maze_reward(int score, int *xp, int *stars)
{
    /* A maze takes 5 to 15 seconds tilted well: 8 or so in a round, with
     * stars ~100. To tune on the board. */
    *xp = score >= 60 ? 15 : score >= 40 ? 10 : score >= 20 ? 5 : 0;
    *stars = score >= 90 ? 3 : score >= 60 ? 2 : score >= 40 ? 1 : 0;
}
