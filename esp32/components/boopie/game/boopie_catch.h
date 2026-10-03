/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * 接零食 (docs/boopie-interaction.md): food falls, the pet slides along the
 * bottom to catch it, and storm clouds are dodged. Plain C with no IDF
 * dependencies; the coordinates are the 64 x 64 grid the pixel renderer draws.
 */

#define BOOPIE_CATCH_SECONDS 60.0f
#define BOOPIE_CATCH_ITEMS 8
#define BOOPIE_CATCH_COMBO 5      /* catches in a row that double the points */
#define BOOPIE_CATCH_PET_Y 44     /* the top of the pet's head */
#define BOOPIE_CATCH_PET_MIN 13.0f   /* where its centre may go, in the round screen */
#define BOOPIE_CATCH_PET_MAX 51.0f
#define BOOPIE_CATCH_SPEED 42.0f  /* cells a second, flat out */
#define BOOPIE_CATCH_FOODS 5      /* boopie_food_t's */

typedef enum {
    BOOPIE_CATCH_NONE = 0,
    BOOPIE_CATCH_FOOD,            /* 1 point */
    BOOPIE_CATCH_GOLD,            /* a sweet: 3 */
    BOOPIE_CATCH_CLOUD,           /* -2, and the pet's dizzy a moment */
} boopie_catch_kind_t;

typedef struct {
    boopie_catch_kind_t kind;
    uint8_t food;                 /* FOOD: which, as boopie_food_t */
    float x, y;                   /* its centre */
    float vy;
    bool caught;                  /* caught: it shows what it scored, then goes */
    float caught_t;
    int8_t points;
} boopie_catch_item_t;

typedef struct boopie_catch {
    uint32_t rng;
    float t;                      /* seconds into the round */
    float next;                   /* when the next one drops */
    float x;                      /* the pet's centre */
    float dizzy;                  /* seconds it can't move */
    int score, combo, best_combo, caught, missed;
    bool over;
    boopie_catch_item_t items[BOOPIE_CATCH_ITEMS];
} boopie_catch_t;

void boopie_catch_start(boopie_catch_t *g, uint32_t seed);

/* move: -1 (left, flat out) to 1 (right). Returns the points scored this tick
 * (0: none), and *what the last thing caught, for its sound. */
int boopie_catch_tick(boopie_catch_t *g, float dt, float move, boopie_catch_kind_t *what);

/* The reward for a score: experience and stars. */
void boopie_catch_reward(int score, int *xp, int *stars);
