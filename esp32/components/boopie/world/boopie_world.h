/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "boopie_world_art.h"

/*
 * The pet's world (docs/boopie-world.md): rooms of things, some there from
 * the start and some brought by the pet's level, the pet walking about them,
 * and taps that send it to a thing to use it. Plain C, the time passed in;
 * coordinates are the 156 x 156 scene (the screen at 3x).
 */

#define BOOPIE_WORLD_W 156

typedef enum {
    BOOPIE_ROOM_LIVING = 0,   /* 一楼客厅 */
    BOOPIE_ROOM_BEDROOM,      /* 二楼卧室 */
    BOOPIE_ROOM_COUNT,
} boopie_room_t;

/* What using a thing does: the screen's to carry out. */
typedef enum {
    BOOPIE_DO_NOTHING = 0,
    BOOPIE_DO_GAMES,          /* the TV */
    BOOPIE_DO_BOOKS,          /* the bookshelf: the album, the chat history */
    BOOPIE_DO_RADIO,          /* 白噪音 */
    BOOPIE_DO_FEED,           /* the bowl */
    BOOPIE_DO_UPSTAIRS,
    BOOPIE_DO_DOWNSTAIRS,
    BOOPIE_DO_OUTSIDE,        /* the door */
    BOOPIE_DO_SLEEP,          /* the bed: to sleep, or wake */
    BOOPIE_DO_WARDROBE,       /* 换装 */
    BOOPIE_DO_RENAME,         /* the mirror */
    BOOPIE_DO_STATUS,         /* the desk's computer */
    BOOPIE_DO_COUNT,
} boopie_do_t;

typedef enum { BOOPIE_LAYER_FLOOR = 0, BOOPIE_LAYER_WALL, BOOPIE_LAYER_STAND } boopie_layer_t;

typedef struct {
    uint8_t art;              /* boopie_art_id_t */
    int16_t x, y;             /* where it stands */
    uint8_t from_level;       /* shown from this level ... */
    uint8_t to_level;         /* ... to this one (0: on and on) */
    uint8_t layer;            /* boopie_layer_t */
    uint8_t act;              /* boopie_do_t */
    uint8_t hint;             /* its hint bubble's art, or 0 for none */
    int8_t use_dx, use_dy;    /* where the pet stands to use it, from (x, y) */
} boopie_thing_t;

/* A room's things, and its background at a level. */
const boopie_thing_t *boopie_room_things(boopie_room_t room, int *count);
boopie_art_id_t boopie_room_background(boopie_room_t room, int level);
bool boopie_thing_shown(const boopie_thing_t *t, int level);
/* Where the pet may walk: a box on the floor. */
void boopie_room_floor(boopie_room_t room, int *x0, int *y0, int *x1, int *y1);

typedef enum { BOOPIE_PET_IDLE = 0, BOOPIE_PET_WALKING, BOOPIE_PET_USING, BOOPIE_PET_SLEEPING } boopie_pet_state_t;

typedef struct {
    uint32_t rng;
    boopie_room_t room;
    float x, y;               /* the pet's feet */
    float tx, ty;             /* walking to */
    int facing;               /* -1 left, 1 right */
    boopie_pet_state_t state;
    float state_t;            /* seconds in this state */
    float idle_for;           /* how long it rests before wandering on */
    int pending;              /* the thing it's walking to use, or -1 */
    float walked;             /* distance walked, for the hop */
} boopie_world_t;

void boopie_world_init(boopie_world_t *w, uint32_t seed);

/* Into a room, at the way in from `from` (the stairs, the door), or its middle. */
void boopie_world_enter(boopie_world_t *w, boopie_room_t room, boopie_do_t from);

/*
 * A tap at (x, y): on a thing that does something, the pet goes to use it
 * (and its index comes back); on the floor, it walks there (-1); elsewhere
 * nothing (-2). Asleep, a tap anywhere but the bed wakes it (-3).
 */
int boopie_world_tap(boopie_world_t *w, int level, float x, float y);

/* Moves time on dt seconds; what to do now that the pet's reached a thing, or NOTHING. */
boopie_do_t boopie_world_tick(boopie_world_t *w, int level, float dt);

/* To bed (it walks there and sleeps) or up. */
void boopie_world_sleep(boopie_world_t *w, int level, bool on);

/* The thing at index i of the current room. */
const boopie_thing_t *boopie_world_thing(const boopie_world_t *w, int i);
