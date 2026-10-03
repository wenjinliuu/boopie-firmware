/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Plays 接零食 and 重力迷宫 (game/boopie_catch.c, game/boopie_maze.c) for
 * test_boopie_games.py, printing JSON:
 *   catch SEED BOT     BOT: still (never moves), chase (after the food, round the clouds)
 *   maze SEED BOT      BOT: still, solve (tilts along the way out); also checks
 *                      every maze is perfect and the ball never in a wall
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_catch.h"
#include "boopie_maze.h"

#define DT (1.0f / 30)

static int play_catch(uint32_t seed, const char *bot)
{
    boopie_catch_t g;
    boopie_catch_start(&g, seed);
    int clouds = 0, golds = 0, ticks = 0;
    float min_x = 99, max_x = -99;
    while (!g.over) {
        float move = 0;
        if (strcmp(bot, "chase") == 0) {
            /* The lowest food not yet caught; keep out from under a cloud near the head. */
            float target = g.x, best = -1;
            for (int i = 0; i < BOOPIE_CATCH_ITEMS; i++) {
                const boopie_catch_item_t *it = &g.items[i];
                if (it->kind == BOOPIE_CATCH_NONE || it->caught || it->kind == BOOPIE_CATCH_CLOUD) {
                    continue;
                }
                if (it->y > best && it->y < BOOPIE_CATCH_PET_Y - 3) {
                    best = it->y;
                    target = it->x;
                }
            }
            for (int i = 0; i < BOOPIE_CATCH_ITEMS; i++) {
                const boopie_catch_item_t *it = &g.items[i];
                if (it->kind == BOOPIE_CATCH_CLOUD && !it->caught && it->y > 26 && it->y < BOOPIE_CATCH_PET_Y
                    && fabsf(it->x - target) < 10) {
                    target = it->x + (it->x < 32 ? 12 : -12);
                }
            }
            move = (target - g.x) / 4;
        }
        boopie_catch_kind_t what = BOOPIE_CATCH_NONE;
        int p = boopie_catch_tick(&g, DT, move, &what);
        if (p && what == BOOPIE_CATCH_CLOUD) {
            clouds++;
        } else if (p && what == BOOPIE_CATCH_GOLD) {
            golds++;
        }
        min_x = fminf(min_x, g.x);
        max_x = fmaxf(max_x, g.x);
        ticks++;
    }
    int xp, stars;
    boopie_catch_reward(g.score, &xp, &stars);
    printf("{\"score\": %d, \"caught\": %d, \"missed\": %d, \"clouds\": %d, \"golds\": %d, \"best_combo\": %d, "
           "\"t\": %.2f, \"ticks\": %d, \"min_x\": %.2f, \"max_x\": %.2f, \"xp\": %d, \"stars\": %d}\n",
           g.score, g.caught, g.missed, clouds, golds, g.best_combo, g.t, ticks, min_x, max_x, xp, stars);
    return 0;
}

/* Checks the maze is perfect (n*n - 1 openings, all cells reached) and its walls
 * read back as its open sides. */
static int check_maze(const boopie_maze_t *g)
{
    int n = g->n, openings = 0;
    for (int c = 0; c < n * n; c++) {
        int cx = c % n, cy = c / n;
        if (g->open[c] & BOOPIE_MAZE_E) {
            openings++;
            if (cx == n - 1 || !(g->open[c + 1] & BOOPIE_MAZE_W)) return 1;
        }
        if (g->open[c] & BOOPIE_MAZE_S) {
            openings++;
            if (cy == n - 1 || !(g->open[c + n] & BOOPIE_MAZE_N)) return 2;
        }
        int x0 = cx * BOOPIE_MAZE_CELL, y0 = cy * BOOPIE_MAZE_CELL;
        if (boopie_maze_wall(g, x0, y0 + 3) == !!(g->open[c] & BOOPIE_MAZE_W)) return 3;
        if (boopie_maze_wall(g, x0 + 3, y0) == !!(g->open[c] & BOOPIE_MAZE_N)) return 4;
        for (int j = 1; j < BOOPIE_MAZE_CELL; j++)
            for (int i = 1; i < BOOPIE_MAZE_CELL; i++)
                if (boopie_maze_wall(g, x0 + i, y0 + j)) return 5;
    }
    if (openings != n * n - 1) return 6;
    int seen[BOOPIE_MAZE_MAX * BOOPIE_MAZE_MAX] = { 0 }, queue[BOOPIE_MAZE_MAX * BOOPIE_MAZE_MAX], h = 0, t = 0, got = 1;
    queue[t++] = 0;
    seen[0] = 1;
    while (h < t) {
        int c = queue[h++];
        int next[4] = { (g->open[c] & BOOPIE_MAZE_N) ? c - n : -1, (g->open[c] & BOOPIE_MAZE_E) ? c + 1 : -1,
                        (g->open[c] & BOOPIE_MAZE_S) ? c + n : -1, (g->open[c] & BOOPIE_MAZE_W) ? c - 1 : -1 };
        for (int k = 0; k < 4; k++)
            if (next[k] >= 0 && !seen[next[k]]) {
                seen[next[k]] = 1;
                got++;
                queue[t++] = next[k];
            }
    }
    return got == n * n ? 0 : 7;
}

/* The next cell on the way from `from` to the way out. */
static int next_cell(const boopie_maze_t *g, int from)
{
    int n = g->n, goal = n * n - 1;
    int prev[BOOPIE_MAZE_MAX * BOOPIE_MAZE_MAX], queue[BOOPIE_MAZE_MAX * BOOPIE_MAZE_MAX], h = 0, t = 0;
    for (int i = 0; i < n * n; i++) prev[i] = -2;
    queue[t++] = goal;
    prev[goal] = -1;
    while (h < t) {   /* backwards from the goal: prev[] points one step nearer it */
        int c = queue[h++];
        int next[4] = { (g->open[c] & BOOPIE_MAZE_N) ? c - n : -1, (g->open[c] & BOOPIE_MAZE_E) ? c + 1 : -1,
                        (g->open[c] & BOOPIE_MAZE_S) ? c + n : -1, (g->open[c] & BOOPIE_MAZE_W) ? c - 1 : -1 };
        for (int k = 0; k < 4; k++)
            if (next[k] >= 0 && prev[next[k]] == -2) {
                prev[next[k]] = c;
                queue[t++] = next[k];
            }
    }
    return prev[from] >= 0 ? prev[from] : goal;
}

static int play_maze(uint32_t seed, const char *bot)
{
    boopie_maze_t g;
    boopie_maze_start(&g, seed);
    int bad = 0, in_wall = 0, mazes = 1, sizes[16] = { 0 };
    sizes[0] = g.n;
    bad = check_maze(&g);
    float max_speed = 0;
    while (!g.over) {
        float tx = 0, ty = 0;
        if (strcmp(bot, "solve") == 0 && g.cleared <= 0) {
            int n = g.n;
            int c = (int)(g.y / BOOPIE_MAZE_CELL) * n + (int)(g.x / BOOPIE_MAZE_CELL);
            int to = next_cell(&g, c);
            float gx = (to % n) * BOOPIE_MAZE_CELL + BOOPIE_MAZE_CELL / 2.0f + 0.5f;
            float gy = (to / n) * BOOPIE_MAZE_CELL + BOOPIE_MAZE_CELL / 2.0f + 0.5f;
            /* Lean toward the next cell's middle, braking as it gets there. */
            tx = (gx - g.x) * 0.25f - g.vx * 0.06f;
            ty = (gy - g.y) * 0.25f - g.vy * 0.06f;
        }
        boopie_maze_tick(&g, DT, tx, ty);
        if (g.cleared <= 0 && mazes <= g.level) {   /* a new maze, after a clear's pause */
            if (mazes < 16) sizes[mazes] = g.n;
            mazes++;
            if (!bad) bad = check_maze(&g);
        }
        float b = BOOPIE_MAZE_BALL;
        for (int py = (int)floorf(g.y - b); py <= (int)floorf(g.y + b - 0.001f); py++)
            for (int px = (int)floorf(g.x - b); px <= (int)floorf(g.x + b - 0.001f); px++)
                if (boopie_maze_wall(&g, px, py)) in_wall++;
        max_speed = fmaxf(max_speed, sqrtf(g.vx * g.vx + g.vy * g.vy));
    }
    int xp, stars;
    boopie_maze_reward(g.score, &xp, &stars);
    printf("{\"score\": %d, \"level\": %d, \"stars\": %d, \"bad\": %d, \"in_wall\": %d, \"max_speed\": %.1f, "
           "\"sizes\": [%d, %d, %d, %d, %d], \"x\": %.2f, \"y\": %.2f, \"xp\": %d, \"reward_stars\": %d}\n",
           g.score, g.level, g.stars, bad, in_wall, max_speed, sizes[0], sizes[1], sizes[2], sizes[3], sizes[4],
           g.x, g.y, xp, stars);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s catch|maze SEED BOT\n", argv[0]);
        return 2;
    }
    uint32_t seed = (uint32_t)strtoul(argv[2], NULL, 10);
    return strcmp(argv[1], "catch") == 0 ? play_catch(seed, argv[3]) : play_maze(seed, argv[3]);
}
