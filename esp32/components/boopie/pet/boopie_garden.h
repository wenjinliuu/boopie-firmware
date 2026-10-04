/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The farm (docs/boopie-world.md), once 小花园: six plots, three to start
 * and more as the pet grows (the world decides which); plant a seed, water it,
 * and it grows while its soil is damp, through sprout, leaves and bud to a
 * bloom that's harvested for stars and experience. Gentle as the pet is:
 * nothing dies, a dry plant only waits (and droops) till it's watered. Plain
 * C with the time passed in (epoch seconds); kept in NVS as it is.
 */

#define BOOPIE_GARDEN_POTS 6

typedef enum {
    BOOPIE_PLANT_NONE = 0,
    BOOPIE_PLANT_SUNFLOWER,    /* 向日葵: 3 days */
    BOOPIE_PLANT_TULIP,        /* 郁金香: 4 */
    BOOPIE_PLANT_STRAWBERRY,   /* 草莓: 5 */
    BOOPIE_PLANT_CACTUS,       /* 仙人掌: 6, a watering lasts 3 days */
    /* Rare, their seeds bought in the shop: */
    BOOPIE_PLANT_PUMPKIN,      /* 南瓜: 5, a watering lasts 36 h */
    BOOPIE_PLANT_MELON,        /* 西瓜: 6 */
    BOOPIE_PLANT_ROSE,         /* 蓝玫瑰: 7 */
    BOOPIE_PLANT_COUNT,
} boopie_plant_t;

typedef enum {
    BOOPIE_STAGE_EMPTY = -1,
    BOOPIE_STAGE_SEED = 0,
    BOOPIE_STAGE_SPROUT,
    BOOPIE_STAGE_LEAVES,
    BOOPIE_STAGE_BUD,
    BOOPIE_STAGE_BLOOM,        /* ready to harvest */
} boopie_stage_t;

typedef struct {
    uint8_t plant;             /* boopie_plant_t */
    uint8_t pad[3];
    uint32_t grown_s;          /* seconds grown so far */
    int64_t damp_until;        /* epoch seconds */
    int64_t counted_to;        /* growth counted up to here */
} boopie_pot_t;

/* Kept in NVS as it is: only ever append, and bump version on a change. */
#define BOOPIE_GARDEN_VERSION 3   /* 3: room for 16 kinds; 2: six plots (1 had three: loaded into the first three) */
#define BOOPIE_GARDEN_KINDS 16    /* kinds the harvest count keeps room for */
typedef struct boopie_garden {
    uint8_t version;
    uint8_t pad[3];
    boopie_pot_t pots[BOOPIE_GARDEN_POTS];
    uint16_t harvested[BOOPIE_GARDEN_KINDS];
} boopie_garden_t;

void boopie_garden_init(boopie_garden_t *g);
/* A saved garden; false if it isn't one (then *g is untouched). */
bool boopie_garden_load(boopie_garden_t *g, const void *blob, size_t n);

const char *boopie_plant_name(boopie_plant_t p);   /* "向日葵" */
int boopie_plant_days(boopie_plant_t p);           /* days of damp soil to bloom */
int boopie_plant_price(boopie_plant_t p);          /* a seed's stars in the shop; 0: free, always to hand */
int boopie_plant_stars(boopie_plant_t p);          /* what a harvest gives */

/* Counts growth up to now. Call before reading or changing it. */
void boopie_garden_update(boopie_garden_t *g, int64_t now);

/* Plants a seed in an empty pot, watered. */
bool boopie_garden_plant(boopie_garden_t *g, int pot, boopie_plant_t plant, int64_t now);
/* Waters a pot: false if nothing's planted, it's blooming, or it's still more than half damp. */
bool boopie_garden_water(boopie_garden_t *g, int pot, int64_t now);
/* Harvests a bloom: the reward, and the pot's emptied. */
bool boopie_garden_harvest(boopie_garden_t *g, int pot, int *xp, int *stars);

boopie_stage_t boopie_garden_stage(const boopie_garden_t *g, int pot);
bool boopie_garden_dry(const boopie_garden_t *g, int pot, int64_t now);
/* Seconds of damp soil still needed to bloom (0 when blooming). */
uint32_t boopie_garden_left_s(const boopie_garden_t *g, int pot);
/* Seconds the soil stays damp (0: dry). */
int64_t boopie_garden_damp_s(const boopie_garden_t *g, int pot, int64_t now);
