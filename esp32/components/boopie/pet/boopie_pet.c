/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_pet.h"

#include <string.h>

static const uint16_t XP_EACH[BOOPIE_XP_SOURCE_COUNT] = { 10, 20, 2, 5, 15 };
static const uint16_t XP_CAP[BOOPIE_XP_SOURCE_COUNT] = { 10, 40, 20, 30, 45 };
#define LATE_FEED_XP 10
#define MEET_STARS 2

/* Colours by index as the settings page lists them (0 own, 1 cherry pink). */
static const uint8_t COLOUR_LEVEL[] = { 1, 1, 2, 4, 7, 9, 12, 15, 17 };
/* Scenes as boopie_scene_t: default stars fireflies snow petals bubbles matrix neon_grid glitch. */
static const uint8_t SCENE_LEVEL[] = { 1, 3, 6, 11, 8, 13, 16, 18, 20 };
/* Accessories: bow, crown, scarf, party hat. */
static const uint8_t ACCESSORY_LEVEL[] = { 5, 10, 14, 19 };

void boopie_pet_init(boopie_pet_t *p)
{
    memset(p, 0, sizeof(*p));
    p->version = BOOPIE_PET_VERSION;
    p->day = -1;
}

uint32_t boopie_pet_need(int level)
{
    uint32_t n = 80 + 30 * (uint32_t)(level < 1 ? 1 : level);
    return n > 620 ? 620 : n;
}

int boopie_pet_level(uint32_t xp, uint32_t *into, uint32_t *need)
{
    int level = 1;
    while (xp >= boopie_pet_need(level)) {
        xp -= boopie_pet_need(level);
        level++;
    }
    if (into) {
        *into = xp;
    }
    if (need) {
        *need = boopie_pet_need(level);
    }
    return level;
}

int boopie_pet_unlock_level(boopie_unlock_kind_t kind, int index)
{
    if (index < 0) {
        return 0;
    }
    switch (kind) {
    case BOOPIE_UNLOCK_COLOUR:
        return index < (int)sizeof(COLOUR_LEVEL) ? COLOUR_LEVEL[index] : 0;
    case BOOPIE_UNLOCK_SCENE:
        return index < (int)sizeof(SCENE_LEVEL) ? SCENE_LEVEL[index] : 0;
    case BOOPIE_UNLOCK_ACCESSORY:
        return index < (int)sizeof(ACCESSORY_LEVEL) ? ACCESSORY_LEVEL[index] : 0;
    default:
        return 0;
    }
}

static void add_xp(boopie_pet_t *p, int xp, boopie_pet_event_t *ev)
{
    if (xp <= 0) {
        return;
    }
    int before = boopie_pet_level(p->xp, NULL, NULL);
    p->xp += (uint32_t)xp;
    int gained = boopie_pet_level(p->xp, NULL, NULL) - before;
    p->stars += (uint32_t)(gained * BOOPIE_PET_LEVEL_STARS);
    if (ev) {
        ev->xp += xp;
        ev->levels += gained;
        ev->stars += gained * BOOPIE_PET_LEVEL_STARS;
    }
}

void boopie_pet_earn(boopie_pet_t *p, boopie_xp_source_t src, int xp, boopie_pet_event_t *ev)
{
    if ((int)src < 0 || src >= BOOPIE_XP_SOURCE_COUNT) {
        return;
    }
    if (xp < 0) {
        xp = XP_EACH[src];
    }
    int room = XP_CAP[src] - p->xp_today[src];
    if (xp > room) {
        xp = room;
    }
    if (xp <= 0) {
        return;
    }
    p->xp_today[src] += (uint16_t)xp;
    add_xp(p, xp, ev);
}

void boopie_pet_resume(boopie_pet_t *p, int64_t now)
{
    if (p->last_tick > 0 && now > p->last_tick) {
        int64_t gap = now - p->last_tick;
        if (p->last_fed > 0) {
            p->last_fed += gap;
        }
        if (p->hungry) {
            p->hungry_since += gap;
        }
    }
    p->last_tick = now;
}

static bool in_day(int minute)
{
    return minute >= BOOPIE_PET_DAY_START_MIN && minute < BOOPIE_PET_DAY_END_MIN;
}

static bool at_night(int minute)
{
    return minute >= BOOPIE_PET_NIGHT_START_MIN || minute < BOOPIE_PET_NIGHT_END_MIN;
}

boopie_expr_t boopie_pet_tick(boopie_pet_t *p, bool known, int64_t now, int32_t day, int minute,
                              float idle_s, boopie_pet_event_t *ev)
{
    if (!known) {
        return BOOPIE_EXPR_IDLE;   /* no clock yet: the pet waits */
    }
    if (day != p->day) {           /* a new day */
        p->day = day;
        p->hungers_today = 0;
        p->met_today = 0;
        memset(p->xp_today, 0, sizeof(p->xp_today));
        /* It wakes up fed: the first hunger comes 3.5 h after the day starts. */
        int64_t day_start = now - (int64_t)(minute - BOOPIE_PET_DAY_START_MIN) * 60;
        if (p->last_fed < day_start) {
            p->last_fed = day_start;
        }
    }
    if (!p->met_today) {
        p->met_today = 1;
        boopie_pet_earn(p, BOOPIE_XP_MEET, -1, ev);
        p->stars += MEET_STARS;
        if (ev) {
            ev->stars += MEET_STARS;
        }
    }
    p->last_tick = now;

    if (!in_day(minute)) {
        p->hungry = 0;             /* no hunger at night, and none carried into it */
    } else if (!p->hungry && p->hungers_today < BOOPIE_PET_HUNGERS_A_DAY &&
               now - p->last_fed >= BOOPIE_PET_HUNGRY_AFTER_S) {
        p->hungry = 1;
        p->hungry_since = now;
        p->hungers_today++;
    }

    if (at_night(minute) && idle_s >= BOOPIE_PET_SLEEPY_IDLE_S) {
        return BOOPIE_EXPR_SLEEPY;
    }
    if (p->hungry) {
        return now - p->hungry_since >= BOOPIE_PET_SAD_AFTER_S ? BOOPIE_EXPR_SAD : BOOPIE_EXPR_HUNGRY;
    }
    return BOOPIE_EXPR_IDLE;
}

bool boopie_pet_tap(boopie_pet_t *p, int64_t now, boopie_pet_event_t *ev)
{
    if (p->hungry) {
        bool on_time = now - p->hungry_since <= BOOPIE_PET_ON_TIME_S;
        p->hungry = 0;
        p->last_fed = now;
        boopie_pet_earn(p, BOOPIE_XP_FEED, on_time ? XP_EACH[BOOPIE_XP_FEED] : LATE_FEED_XP, ev);
        if (ev) {
            ev->fed = true;
        }
        return true;
    }
    boopie_pet_earn(p, BOOPIE_XP_POKE, -1, ev);
    return false;
}

void boopie_pet_talked(boopie_pet_t *p, boopie_pet_event_t *ev)
{
    boopie_pet_earn(p, BOOPIE_XP_TALK, -1, ev);
}
