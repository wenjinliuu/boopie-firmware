/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "boopie_expr.h"

/*
 * The pet: hunger, feeding, experience, levels and stars, as designed in
 * docs/boopie-character.md "宠物养成". Plain C with the time passed in, so
 * tests/test_boopie_pet.py runs it on the host; boopie_avatar.c feeds it the
 * clock and the taps, and keeps it in NVS.
 *
 * Gentle on purpose: it never dies, nothing is lost by being away, and time
 * powered off doesn't count (boopie_pet_resume).
 */

/* Hunger: only in the day, 3.5 h after a meal, twice a day at most. */
#define BOOPIE_PET_DAY_START_MIN (8 * 60)
#define BOOPIE_PET_DAY_END_MIN (21 * 60)
#define BOOPIE_PET_HUNGRY_AFTER_S (3 * 3600 + 1800)
#define BOOPIE_PET_HUNGERS_A_DAY 2
#define BOOPIE_PET_ON_TIME_S 3600        /* fed within this of getting hungry */
#define BOOPIE_PET_SAD_AFTER_S (3 * 3600)/* hungry this long: sad */
/* Sleepy: at night, left alone this long. */
#define BOOPIE_PET_NIGHT_START_MIN (23 * 60)
#define BOOPIE_PET_NIGHT_END_MIN (7 * 60)
#define BOOPIE_PET_SLEEPY_IDLE_S (5 * 60)

/* Levels never stop; a level takes 80 + 30 x level, at most 620. */
#define BOOPIE_PET_LEVEL_STARS 20

typedef enum {
    BOOPIE_XP_MEET = 0,     /* the first time each day: 10, and 2 stars */
    BOOPIE_XP_FEED,         /* 20 on time, 10 late */
    BOOPIE_XP_POKE,         /* 2 */
    BOOPIE_XP_TALK,         /* 5 a reply */
    BOOPIE_XP_GAME,         /* 5 to 15 a round */
    BOOPIE_XP_SOURCE_COUNT,
} boopie_xp_source_t;

/* Kept in NVS as it is: only ever append, and bump version on a change. */
#define BOOPIE_PET_VERSION 1
typedef struct {
    uint8_t version;
    uint8_t hungers_today;
    uint8_t hungry;
    uint8_t met_today;
    int32_t day;                    /* the local day the counters are for (days since 1970) */
    int64_t last_fed;               /* epoch seconds */
    int64_t hungry_since;
    int64_t last_tick;              /* last seen awake, for time powered off */
    uint32_t xp;                    /* all ever earned */
    uint32_t stars;
    uint16_t xp_today[BOOPIE_XP_SOURCE_COUNT];
} boopie_pet_t;

/* What happened in a call, for the screen to show. */
typedef struct {
    int xp;                         /* earned */
    int stars;
    int levels;                     /* gained */
    bool fed;
} boopie_pet_event_t;

/* A new pet. */
void boopie_pet_init(boopie_pet_t *p);

/* Powered off since p->last_tick: shift the clocks by the gap, so it isn't
 * hungrier for it. */
void boopie_pet_resume(boopie_pet_t *p, int64_t now);

/*
 * Each few seconds, with the local time: day (days since 1970) and minute of
 * the day; known false while the clock isn't set (the pet then waits).
 * idle_s: how long since anyone touched or talked to it. Returns what to
 * show while idle: IDLE, HUNGRY, SAD or SLEEPY.
 */
boopie_expr_t boopie_pet_tick(boopie_pet_t *p, bool known, int64_t now, int32_t day, int minute,
                              float idle_s, boopie_pet_event_t *ev);

/* Tapped: fed if hungry (true), else a poke. */
bool boopie_pet_tap(boopie_pet_t *p, int64_t now, boopie_pet_event_t *ev);

/* A reply was spoken to it. */
void boopie_pet_talked(boopie_pet_t *p, boopie_pet_event_t *ev);

/* Experience from a source (capped per day; games pass their own amount). */
void boopie_pet_earn(boopie_pet_t *p, boopie_xp_source_t src, int xp, boopie_pet_event_t *ev);

/* Level from experience, and the experience into and needed for the next. */
int boopie_pet_level(uint32_t xp, uint32_t *into, uint32_t *need);

/* Experience a level takes to the next. */
uint32_t boopie_pet_need(int level);

/* What each level unlocks, 2 ... 20: a colour, a background or an accessory. */
typedef enum { BOOPIE_UNLOCK_NONE, BOOPIE_UNLOCK_COLOUR, BOOPIE_UNLOCK_SCENE, BOOPIE_UNLOCK_ACCESSORY } boopie_unlock_kind_t;

/* The level that unlocks item `index` of a kind (colour 0 = own, 1 = cherry
 * pink: from the start; scene as boopie_scene_t; accessory: bow, crown,
 * scarf, party hat), or 0 if it isn't unlocked by level. */
int boopie_pet_unlock_level(boopie_unlock_kind_t kind, int index);
