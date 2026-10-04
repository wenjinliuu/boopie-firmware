/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_garden.h"

#include <string.h>

#define DAY 86400

static const struct {
    const char *name;
    uint8_t days;
    uint8_t damp_h;   /* how long a watering lasts */
    uint8_t stars;
    uint8_t xp;
} PLANTS[BOOPIE_PLANT_COUNT] = {
    [BOOPIE_PLANT_NONE] = { "", 0, 0, 0, 0 },
    [BOOPIE_PLANT_SUNFLOWER] = { "向日葵", 3, 30, 2, 15 },
    [BOOPIE_PLANT_TULIP] = { "郁金香", 4, 30, 3, 20 },
    [BOOPIE_PLANT_STRAWBERRY] = { "草莓", 5, 30, 4, 25 },
    [BOOPIE_PLANT_CACTUS] = { "仙人掌", 6, 72, 5, 30 },
};

void boopie_garden_init(boopie_garden_t *g)
{
    memset(g, 0, sizeof(*g));
    g->version = BOOPIE_GARDEN_VERSION;
}

bool boopie_garden_load(boopie_garden_t *g, const void *blob, size_t n)
{
    boopie_garden_t saved;
    if (n != sizeof saved) {
        return false;
    }
    memcpy(&saved, blob, n);
    if (saved.version != BOOPIE_GARDEN_VERSION) {
        return false;
    }
    for (int i = 0; i < BOOPIE_GARDEN_POTS; i++) {
        if (saved.pots[i].plant >= BOOPIE_PLANT_COUNT) {
            saved.pots[i].plant = BOOPIE_PLANT_NONE;
        }
    }
    *g = saved;
    return true;
}

const char *boopie_plant_name(boopie_plant_t p)
{
    return (int)p > 0 && p < BOOPIE_PLANT_COUNT ? PLANTS[p].name : "";
}

int boopie_plant_days(boopie_plant_t p)
{
    return (int)p > 0 && p < BOOPIE_PLANT_COUNT ? PLANTS[p].days : 0;
}

static bool valid(const boopie_garden_t *g, int pot)
{
    (void)g;
    return pot >= 0 && pot < BOOPIE_GARDEN_POTS;
}

static uint32_t need_s(const boopie_pot_t *p)
{
    return (uint32_t)PLANTS[p->plant].days * DAY;
}

void boopie_garden_update(boopie_garden_t *g, int64_t now)
{
    for (int i = 0; i < BOOPIE_GARDEN_POTS; i++) {
        boopie_pot_t *p = &g->pots[i];
        if (p->plant == BOOPIE_PLANT_NONE) {
            continue;
        }
        if (now < p->counted_to) {
            p->counted_to = now;   /* the clock went back: count from here */
            continue;
        }
        /* It grows while damp: from where it was counted to, till now or it dried. */
        int64_t until = now < p->damp_until ? now : p->damp_until;
        if (until > p->counted_to) {
            uint64_t grown = (uint64_t)p->grown_s + (uint64_t)(until - p->counted_to);
            p->grown_s = grown > need_s(p) ? need_s(p) : (uint32_t)grown;
        }
        p->counted_to = now;
    }
}

bool boopie_garden_plant(boopie_garden_t *g, int pot, boopie_plant_t plant, int64_t now)
{
    if (!valid(g, pot) || (int)plant <= 0 || plant >= BOOPIE_PLANT_COUNT || g->pots[pot].plant != BOOPIE_PLANT_NONE) {
        return false;
    }
    boopie_pot_t *p = &g->pots[pot];
    memset(p, 0, sizeof(*p));
    p->plant = (uint8_t)plant;
    p->counted_to = now;
    p->damp_until = now + (int64_t)PLANTS[plant].damp_h * 3600;   /* planted, and watered in */
    return true;
}

bool boopie_garden_water(boopie_garden_t *g, int pot, int64_t now)
{
    if (!valid(g, pot) || g->pots[pot].plant == BOOPIE_PLANT_NONE || boopie_garden_stage(g, pot) == BOOPIE_STAGE_BLOOM) {
        return false;
    }
    boopie_pot_t *p = &g->pots[pot];
    int64_t full = (int64_t)PLANTS[p->plant].damp_h * 3600;
    if (p->damp_until - now > full / 2) {
        return false;   /* still damp: it doesn't need it yet */
    }
    boopie_garden_update(g, now);
    p->damp_until = now + full;
    return true;
}

bool boopie_garden_harvest(boopie_garden_t *g, int pot, int *xp, int *stars)
{
    if (!valid(g, pot) || boopie_garden_stage(g, pot) != BOOPIE_STAGE_BLOOM) {
        return false;
    }
    boopie_pot_t *p = &g->pots[pot];
    *xp = PLANTS[p->plant].xp;
    *stars = PLANTS[p->plant].stars;
    if (g->harvested[p->plant] < UINT16_MAX) {
        g->harvested[p->plant]++;
    }
    memset(p, 0, sizeof(*p));
    return true;
}

boopie_stage_t boopie_garden_stage(const boopie_garden_t *g, int pot)
{
    if (!valid(g, pot) || g->pots[pot].plant == BOOPIE_PLANT_NONE) {
        return BOOPIE_STAGE_EMPTY;
    }
    const boopie_pot_t *p = &g->pots[pot];
    uint32_t need = need_s(p);
    if (p->grown_s >= need) {
        return BOOPIE_STAGE_BLOOM;
    }
    float f = (float)p->grown_s / (float)need;
    return f < 0.1f ? BOOPIE_STAGE_SEED : f < 0.35f ? BOOPIE_STAGE_SPROUT : f < 0.65f ? BOOPIE_STAGE_LEAVES
                                                                                   : BOOPIE_STAGE_BUD;
}

bool boopie_garden_dry(const boopie_garden_t *g, int pot, int64_t now)
{
    return valid(g, pot) && g->pots[pot].plant != BOOPIE_PLANT_NONE && now >= g->pots[pot].damp_until
           && boopie_garden_stage(g, pot) != BOOPIE_STAGE_BLOOM;
}

uint32_t boopie_garden_left_s(const boopie_garden_t *g, int pot)
{
    if (!valid(g, pot) || g->pots[pot].plant == BOOPIE_PLANT_NONE) {
        return 0;
    }
    uint32_t need = need_s(&g->pots[pot]);
    return g->pots[pot].grown_s >= need ? 0 : need - g->pots[pot].grown_s;
}

int64_t boopie_garden_damp_s(const boopie_garden_t *g, int pot, int64_t now)
{
    if (!valid(g, pot) || g->pots[pot].plant == BOOPIE_PLANT_NONE) {
        return 0;
    }
    return g->pots[pot].damp_until > now ? g->pots[pot].damp_until - now : 0;
}
