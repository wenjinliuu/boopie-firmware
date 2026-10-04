/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_world.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define SPEED 30.0f          /* scene pixels a second */
#define USE_S 0.5f           /* a moment at a thing before it's used */
#define ARRIVE 1.0f

#define A(name) BOOPIE_ART_##name
#define FLOOR BOOPIE_LAYER_FLOOR
#define WALL BOOPIE_LAYER_WALL
#define STAND BOOPIE_LAYER_STAND

/*
 * The rooms, kept inside the round screen and above the buttons along its
 * bottom. What the pet's level brings (docs/boopie-world.md): downstairs a
 * plant at 2, a picture at 4, a sofa at 6, a big rug at 8, a lamp and new
 * wallpaper at 10, a fish tank at 12, a flat TV at 15, a trophy at 20;
 * upstairs a rug at 3, a plant at 5, a star lamp at 7, a poster at 9, a bean
 * bag at 11, a big bed at 14, a telescope at 18, and the starry wallpaper at 10.
 */
static const boopie_thing_t LIVING[] = {
    { A(WINDOW_DAY), 72, 38, 1, 0, WALL, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(PICTURE), 46, 32, 4, 0, WALL, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(RUG_SMALL), 78, 106, 1, 7, FLOOR, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(RUG_BIG), 78, 110, 8, 0, FLOOR, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(DOOR_OUT), 104, 126, 1, 0, FLOOR, BOOPIE_DO_OUTSIDE, A(HINT_DOOR), 0, -4 },
    { A(TV_OLD), 32, 64, 1, 14, STAND, BOOPIE_DO_GAMES, A(HINT_GAME), 2, 10 },
    { A(TV_FLAT), 32, 64, 15, 0, STAND, BOOPIE_DO_GAMES, A(HINT_GAME), 2, 10 },
    { A(BOOKSHELF), 104, 64, 1, 0, STAND, BOOPIE_DO_BOOKS, A(HINT_BOOK), 0, 10 },
    { A(STAIRS_UP), 134, 68, 1, 0, STAND, BOOPIE_DO_UPSTAIRS, A(HINT_UP), 0, 6 },
    { A(LAMP), 56, 66, 10, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(TABLE), 78, 98, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(RADIO), 20, 98, 1, 0, STAND, BOOPIE_DO_RADIO, A(HINT_NOTE), 8, 4 },
    { A(BOWL_FULL), 32, 116, 1, 0, STAND, BOOPIE_DO_FEED, A(HINT_FOOD), 9, 0 },
    { A(PLANT), 60, 122, 2, 11, STAND, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(AQUARIUM), 60, 122, 12, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(SOFA), 126, 98, 6, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(TROPHY), 14, 80, 20, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0 },
};

static const boopie_thing_t BEDROOM[] = {
    { A(WINDOW_DAY), 70, 38, 1, 0, WALL, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(POSTER), 42, 34, 9, 0, WALL, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(MIRROR), 96, 44, 1, 0, WALL, BOOPIE_DO_RENAME, A(HINT_PEN), 0, 28 },
    { A(RUG_ROUND), 82, 114, 3, 0, FLOOR, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(BED), 26, 84, 1, 13, STAND, BOOPIE_DO_SLEEP, A(HINT_ZZZ), 0, -12 },
    { A(BED_BIG), 30, 88, 14, 0, STAND, BOOPIE_DO_SLEEP, A(HINT_ZZZ), 0, -14 },
    { A(PLANT), 50, 62, 5, 17, STAND, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(TELESCOPE), 52, 64, 18, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(WARDROBE), 120, 66, 1, 0, STAND, BOOPIE_DO_WARDROBE, A(HINT_SHIRT), -2, 8 },
    { A(DESK), 128, 104, 1, 0, STAND, BOOPIE_DO_STATUS, A(HINT_INFO), -4, 10 },
    { A(STAR_LAMP), 66, 122, 7, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(BEANBAG), 104, 120, 11, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0 },
    { A(STAIRS_DOWN), 42, 124, 1, 0, STAND, BOOPIE_DO_DOWNSTAIRS, A(HINT_DOWN), 6, -4 },
};

static const struct {
    const boopie_thing_t *things;
    int count;
    int x0, y0, x1, y1;       /* the floor the pet walks */
} ROOMS[BOOPIE_ROOM_COUNT] = {
    [BOOPIE_ROOM_LIVING] = { LIVING, (int)(sizeof LIVING / sizeof LIVING[0]), 16, 70, 142, 124 },
    [BOOPIE_ROOM_BEDROOM] = { BEDROOM, (int)(sizeof BEDROOM / sizeof BEDROOM[0]), 16, 70, 142, 124 },
};

const boopie_thing_t *boopie_room_things(boopie_room_t room, int *count)
{
    if ((int)room < 0 || room >= BOOPIE_ROOM_COUNT) {
        *count = 0;
        return NULL;
    }
    *count = ROOMS[room].count;
    return ROOMS[room].things;
}

boopie_art_id_t boopie_room_background(boopie_room_t room, int level)
{
    if (room == BOOPIE_ROOM_BEDROOM) {
        return level >= 10 ? BOOPIE_ART_BG_UP_STARS : BOOPIE_ART_BG_UP;
    }
    return level >= 10 ? BOOPIE_ART_BG_DOWN_FANCY : BOOPIE_ART_BG_DOWN;
}

bool boopie_thing_shown(const boopie_thing_t *t, int level)
{
    return level >= t->from_level && (!t->to_level || level <= t->to_level);
}

void boopie_room_floor(boopie_room_t room, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = ROOMS[room].x0;
    *y0 = ROOMS[room].y0;
    *x1 = ROOMS[room].x1;
    *y1 = ROOMS[room].y1;
}

static uint32_t rnd(boopie_world_t *w)
{
    w->rng ^= w->rng << 13;
    w->rng ^= w->rng >> 17;
    w->rng ^= w->rng << 5;
    return w->rng;
}

static float frand(boopie_world_t *w)
{
    return (float)(rnd(w) % 10000) / 10000.0f;
}

/* Kept on the floor, and inside the round screen. */
static void on_floor(const boopie_world_t *w, float *x, float *y)
{
    int x0, y0, x1, y1;
    boopie_room_floor(w->room, &x0, &y0, &x1, &y1);
    *x = *x < x0 ? x0 : *x > x1 ? x1 : *x;
    *y = *y < y0 ? y0 : *y > y1 ? y1 : *y;
    float dx = *x - 78, dy = *y - 78, d = sqrtf(dx * dx + dy * dy);
    if (d > 66) {
        *x = 78 + dx * 66 / d;
        *y = 78 + dy * 66 / d;
    }
}

static void walk_to(boopie_world_t *w, float x, float y, int pending)
{
    on_floor(w, &x, &y);
    w->tx = x;
    w->ty = y;
    w->pending = pending;
    w->state = BOOPIE_PET_WALKING;
    w->state_t = 0;
}

void boopie_world_init(boopie_world_t *w, uint32_t seed)
{
    memset(w, 0, sizeof(*w));
    w->rng = seed ? seed : 1;
    w->facing = 1;
    w->pending = -1;
    boopie_world_enter(w, BOOPIE_ROOM_LIVING, BOOPIE_DO_NOTHING);
}

const boopie_thing_t *boopie_world_thing(const boopie_world_t *w, int i)
{
    int n;
    const boopie_thing_t *t = boopie_room_things(w->room, &n);
    return i >= 0 && i < n ? &t[i] : NULL;
}

void boopie_world_enter(boopie_world_t *w, boopie_room_t room, boopie_do_t from)
{
    w->room = room;
    w->state = BOOPIE_PET_IDLE;
    w->state_t = 0;
    w->pending = -1;
    w->idle_for = 2.0f;
    w->x = 78;
    w->y = 100;
    /* At the way in: the stairs it came by, or the door. */
    boopie_do_t way = from == BOOPIE_DO_UPSTAIRS ? BOOPIE_DO_DOWNSTAIRS : from == BOOPIE_DO_DOWNSTAIRS
                                                                              ? BOOPIE_DO_UPSTAIRS : from;
    int n;
    const boopie_thing_t *t = boopie_room_things(room, &n);
    for (int i = 0; i < n && way != BOOPIE_DO_NOTHING; i++) {
        if (t[i].act == way) {
            w->x = t[i].x + t[i].use_dx;
            w->y = t[i].y + t[i].use_dy;
            on_floor(w, &w->x, &w->y);
        }
    }
    w->tx = w->x;
    w->ty = w->y;
}

/* A thing's box on screen: its picture, and its hint over it. */
static bool hit(const boopie_thing_t *t, float x, float y)
{
    const boopie_art_t *a = &boopie_art[t->art];
    float l = t->x - a->ax - 2, top = t->y - a->ay - (t->hint ? 16 : 2);
    float r = l + a->w + 4, b = t->y + 3;
    return x >= l && x <= r && y >= top && y <= b;
}

int boopie_world_tap(boopie_world_t *w, int level, float x, float y)
{
    int n;
    const boopie_thing_t *t = boopie_room_things(w->room, &n);
    int found = -1;
    for (int i = 0; i < n; i++) {
        /* The last drawn wins: floor marks under walls under what stands, front last. */
        if (t[i].act != BOOPIE_DO_NOTHING && boopie_thing_shown(&t[i], level) && hit(&t[i], x, y)) {
            if (found < 0 || t[i].layer > t[found].layer || (t[i].layer == t[found].layer && t[i].y >= t[found].y)) {
                found = i;
            }
        }
    }
    if (w->state == BOOPIE_PET_SLEEPING) {
        if (found >= 0 && t[found].act == BOOPIE_DO_SLEEP) {
            w->state = BOOPIE_PET_USING;   /* the bed again: up it gets */
            w->state_t = 0;
            w->pending = found;
            return found;
        }
        w->state = BOOPIE_PET_IDLE;
        w->state_t = 0;
        w->idle_for = 1.5f;
        return -3;
    }
    if (found >= 0) {
        walk_to(w, t[found].x + t[found].use_dx, t[found].y + t[found].use_dy, found);
        return found;
    }
    int x0, y0, x1, y1;
    boopie_room_floor(w->room, &x0, &y0, &x1, &y1);
    if (y >= y0 - 6 && y <= y1 + 6 && x >= x0 - 6 && x <= x1 + 6) {
        walk_to(w, x, y, -1);
        return -1;
    }
    return -2;
}

void boopie_world_sleep(boopie_world_t *w, int level, bool on)
{
    if (!on) {
        if (w->state == BOOPIE_PET_SLEEPING) {
            w->state = BOOPIE_PET_IDLE;
            w->state_t = 0;
            w->idle_for = 2.0f;
        }
        return;
    }
    int n;
    const boopie_thing_t *t = boopie_room_things(w->room, &n);
    for (int i = 0; i < n; i++) {
        if (t[i].act == BOOPIE_DO_SLEEP && boopie_thing_shown(&t[i], level)) {
            walk_to(w, t[i].x + t[i].use_dx, t[i].y + t[i].use_dy, i);
            return;
        }
    }
    /* No bed here: a nap where it is. */
    w->state = BOOPIE_PET_SLEEPING;
    w->state_t = 0;
}

boopie_do_t boopie_world_tick(boopie_world_t *w, int level, float dt)
{
    w->state_t += dt;
    switch (w->state) {
    case BOOPIE_PET_WALKING: {
        float dx = w->tx - w->x, dy = w->ty - w->y, d = sqrtf(dx * dx + dy * dy);
        float step = SPEED * dt;
        if (fabsf(dx) > 0.5f) {
            w->facing = dx < 0 ? -1 : 1;
        }
        if (d <= step || d < ARRIVE) {
            w->x = w->tx;
            w->y = w->ty;
            w->walked += d;
            w->state = w->pending >= 0 ? BOOPIE_PET_USING : BOOPIE_PET_IDLE;
            w->state_t = 0;
            w->idle_for = 2.0f + 5.0f * frand(w);
        } else {
            w->x += dx / d * step;
            w->y += dy / d * step;
            w->walked += step;
        }
        return BOOPIE_DO_NOTHING;
    }
    case BOOPIE_PET_USING: {
        if (w->state_t < USE_S) {
            return BOOPIE_DO_NOTHING;
        }
        const boopie_thing_t *t = boopie_world_thing(w, w->pending);
        w->pending = -1;
        boopie_do_t act = t ? (boopie_do_t)t->act : BOOPIE_DO_NOTHING;
        w->state = act == BOOPIE_DO_SLEEP ? BOOPIE_PET_SLEEPING : BOOPIE_PET_IDLE;
        w->state_t = 0;
        w->idle_for = 3.0f + 4.0f * frand(w);
        if (act == BOOPIE_DO_UPSTAIRS || act == BOOPIE_DO_DOWNSTAIRS) {
            boopie_world_enter(w, act == BOOPIE_DO_UPSTAIRS ? BOOPIE_ROOM_BEDROOM : BOOPIE_ROOM_LIVING, act);
        }
        (void)level;
        return act;
    }
    case BOOPIE_PET_SLEEPING:
        return BOOPIE_DO_NOTHING;
    default:
        /* Left alone, it wanders: somewhere on the floor, now and then. */
        if (w->state_t >= w->idle_for) {
            int x0, y0, x1, y1;
            boopie_room_floor(w->room, &x0, &y0, &x1, &y1);
            walk_to(w, x0 + frand(w) * (x1 - x0), y0 + frand(w) * (y1 - y0), -1);
        }
        return BOOPIE_DO_NOTHING;
    }
}
