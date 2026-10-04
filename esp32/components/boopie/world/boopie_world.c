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
    { A(WINDOW_DAY), 72, 38, 1, 0, WALL, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(PICTURE), 46, 32, 4, 0, WALL, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(RUG_SMALL), 78, 106, 1, 7, FLOOR, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(RUG_BIG), 78, 110, 8, 0, FLOOR, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(DOOR_OUT), 104, 126, 1, 0, FLOOR, BOOPIE_DO_OUTSIDE, A(HINT_DOOR), 0, -4, 0 },
    { A(TV_OLD), 32, 64, 1, 14, STAND, BOOPIE_DO_GAMES, A(HINT_GAME), 2, 10, 0 },
    { A(TV_FLAT), 32, 64, 15, 0, STAND, BOOPIE_DO_GAMES, A(HINT_GAME), 2, 10, 0 },
    { A(BOOKSHELF), 104, 64, 1, 0, STAND, BOOPIE_DO_BOOKS, A(HINT_BOOK), 0, 10, 0 },
    { A(STAIRS_UP), 134, 68, 1, 0, STAND, BOOPIE_DO_UPSTAIRS, A(HINT_UP), 0, 6, 0 },
    { A(LAMP), 56, 66, 10, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(TABLE), 78, 98, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(RADIO), 20, 98, 1, 0, STAND, BOOPIE_DO_RADIO, A(HINT_NOTE), 8, 4, 0 },
    { A(BOWL_FULL), 32, 116, 1, 0, STAND, BOOPIE_DO_FEED, A(HINT_FOOD), 9, 0, 0 },
    { A(PLANT), 60, 122, 2, 11, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(AQUARIUM), 60, 122, 12, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(SOFA), 126, 98, 6, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(TROPHY), 14, 80, 20, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(WALL_CLOCK), 118, 46, 1, 0, WALL, BOOPIE_DO_FURNI, 0, 0, 32, BOOPIE_FURNI_CLOCK },
    { A(RECORD_PLAYER), 50, 104, 1, 0, STAND, BOOPIE_DO_FURNI, A(HINT_HEART), 0, 6, BOOPIE_FURNI_RECORD },
};

static const boopie_thing_t BEDROOM[] = {
    { A(WINDOW_DAY), 70, 38, 1, 0, WALL, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(POSTER), 42, 34, 9, 0, WALL, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(MIRROR), 96, 44, 1, 0, WALL, BOOPIE_DO_RENAME, A(HINT_PEN), 0, 28, 0 },
    { A(RUG_ROUND), 82, 114, 3, 0, FLOOR, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(BED), 26, 84, 1, 13, STAND, BOOPIE_DO_SLEEP, A(HINT_ZZZ), 0, -12, 0 },
    { A(BED_BIG), 30, 88, 14, 0, STAND, BOOPIE_DO_SLEEP, A(HINT_ZZZ), 0, -14, 0 },
    { A(PLANT), 50, 62, 5, 17, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(TELESCOPE), 52, 64, 18, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(WARDROBE), 120, 66, 1, 0, STAND, BOOPIE_DO_WARDROBE, A(HINT_SHIRT), -2, 8, 0 },
    { A(DESK), 128, 104, 1, 0, STAND, BOOPIE_DO_STATUS, A(HINT_INFO), -4, 10, 0 },
    { A(STAR_LAMP), 66, 122, 7, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(BEANBAG), 104, 120, 11, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(STAIRS_DOWN), 42, 124, 1, 0, STAND, BOOPIE_DO_DOWNSTAIRS, A(HINT_DOWN), 6, -4, 0 },
    { A(FAIRY_LIGHTS), 78, 27, 1, 0, WALL, BOOPIE_DO_FURNI, 0, 0, 0, BOOPIE_FURNI_LIGHTS },
    { A(TEDDY), 142, 90, 1, 0, STAND, BOOPIE_DO_FURNI, A(HINT_HEART), -10, 2, BOOPIE_FURNI_TEDDY },
};

/*
 * Outside: the house front on the left, the path east past the farm to the
 * woods' sign. The farm's six plots open as the pet grows (three, then at 5,
 * 10 and 15), and the yard fills: a lamp post at 3, flowers at 5, the well
 * at 8, the coop and its chickens and a scarecrow at 10, a bench at 12,
 * bee hives at 15, the barn and a fruit tree at 20.
 */
static const boopie_thing_t OUTSIDE[] = {
    { A(TREE), 20, 76, 1, 19, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(FRUIT_TREE), 20, 76, 20, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(HOUSE_FRONT), 70, 64, 1, 0, STAND, BOOPIE_DO_INSIDE, A(HINT_DOOR), 0, 6, 0 },
    { A(MAILBOX), 100, 72, 1, 0, STAND, BOOPIE_DO_MAIL, A(HINT_MAIL), -7, 4, 0 },
    { A(TREE), 116, 58, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(LAMP_POST), 48, 94, 3, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(FLOWERBED), 102, 126, 5, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(BENCH), 30, 122, 12, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(ROCK), 140, 118, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(PINE), 8, 136, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(COOP), 148, 78, 10, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(CHICKEN), 140, 88, 10, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(CHICKEN), 156, 90, 10, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(FENCE_H), 180, 56, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(FENCE_H), 214, 56, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(FENCE_H), 248, 56, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(PLOT_DRY), 180, 80, 1, 0, FLOOR, BOOPIE_DO_PLOT, A(HINT_PLUS), 0, 6, 0 },
    { A(PLOT_DRY), 214, 80, 1, 0, FLOOR, BOOPIE_DO_PLOT, A(HINT_PLUS), 0, 6, 1 },
    { A(PLOT_DRY), 248, 80, 1, 0, FLOOR, BOOPIE_DO_PLOT, A(HINT_PLUS), 0, 6, 2 },
    { A(PLOT_DRY), 180, 116, 5, 0, FLOOR, BOOPIE_DO_PLOT, A(HINT_PLUS), 0, 6, 3 },
    { A(PLOT_DRY), 214, 116, 10, 0, FLOOR, BOOPIE_DO_PLOT, A(HINT_PLUS), 0, 6, 4 },
    { A(PLOT_DRY), 248, 116, 15, 0, FLOOR, BOOPIE_DO_PLOT, A(HINT_PLUS), 0, 6, 5 },
    { A(WELL), 284, 74, 8, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(SCARECROW), 282, 112, 10, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(BEEHIVE), 302, 128, 15, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(BEEHIVE), 316, 130, 15, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(BARN), 326, 70, 20, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(SIGN), 344, 114, 1, 0, STAND, BOOPIE_DO_WILD, A(HINT_TREE), -8, 2, 0 },
    { A(SWING), 122, 108, 1, 0, STAND, BOOPIE_DO_FURNI, A(HINT_HEART), 0, 2, BOOPIE_FURNI_SWING },
    { A(WINDMILL_A), 228, 58, 1, 0, STAND, BOOPIE_DO_FURNI, 0, 0, 0, BOOPIE_FURNI_WINDMILL },
};

/*
 * The woods, east of the yard: the path winds on past oaks and pines. A chest
 * from the start and another at 8 (once a day each); berry bushes at 4, a
 * fairy ring of mushrooms at 6 (lit at night), a pond at 12 with a frog from
 * 14, and at 18 a tree house where the pine was.
 */
static const boopie_thing_t WOODS[] = {
    { A(MUSHROOM_RING), 236, 126, 6, 0, FLOOR, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(POND), 452, 128, 12, 0, FLOOR, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(SIGN_HOME), 14, 110, 1, 0, STAND, BOOPIE_DO_HOME_PATH, A(HINT_HOUSE), 8, 2, 0 },
    { A(OAK), 40, 70, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(PINE), 96, 64, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(OAK), 156, 66, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(OAK), 262, 70, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(PINE), 330, 66, 1, 17, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(TREE_HOUSE), 330, 70, 18, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(OAK), 400, 64, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(OAK), 494, 70, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(FERN), 60, 84, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(FERN), 228, 80, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(FERN), 300, 132, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(FERN), 462, 82, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(MUSHROOM), 88, 122, 1, 0, STAND, BOOPIE_DO_GATHER, A(HINT_MUSHROOM), 8, 0, 0 },
    { A(MUSHROOM), 190, 80, 1, 0, STAND, BOOPIE_DO_GATHER, A(HINT_MUSHROOM), 8, 2, 1 },
    { A(MUSHROOM), 372, 84, 1, 0, STAND, BOOPIE_DO_GATHER, A(HINT_MUSHROOM), 8, 2, 2 },
    { A(BUSH), 66, 134, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(BUSH), 206, 136, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(BUSH), 404, 136, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(BERRY_BUSH), 124, 126, 4, 0, STAND, BOOPIE_DO_GATHER, A(HINT_BERRY), 0, -6, 3 },
    { A(BERRY_BUSH), 352, 126, 4, 0, STAND, BOOPIE_DO_GATHER, A(HINT_BERRY), 0, -6, 4 },
    { A(LOG), 166, 126, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(STUMP), 280, 126, 1, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(FROG), 444, 120, 14, 0, STAND, BOOPIE_DO_NOTHING, 0, 0, 0, 0 },
    { A(CHEST_SHUT), 248, 76, 1, 0, STAND, BOOPIE_DO_CHEST, A(HINT_KEY), 0, 6, 0 },
    { A(CHEST_SHUT), 500, 112, 8, 0, STAND, BOOPIE_DO_CHEST, A(HINT_KEY), -10, 2, 1 },
};

/* Where each slime keeps to: its patch's middle, and how far it strays. */
static const float SLIME_HOME[BOOPIE_SLIMES] = { 130, 300, 430 };
#define SLIME_RANGE 36.0f
#define SLIME_BACK_S 40.0f    /* gone this long, beaten or fled */

static const struct {
    const boopie_thing_t *things;
    int count;
    int x0, y0, x1, y1;       /* the floor the pet walks */
    int width;
} ROOMS[BOOPIE_ROOM_COUNT] = {
    [BOOPIE_ROOM_LIVING] = { LIVING, (int)(sizeof LIVING / sizeof LIVING[0]), 16, 70, 142, 124, BOOPIE_WORLD_W },
    [BOOPIE_ROOM_BEDROOM] = { BEDROOM, (int)(sizeof BEDROOM / sizeof BEDROOM[0]), 16, 70, 142, 124, BOOPIE_WORLD_W },
    [BOOPIE_ROOM_OUTSIDE] = { OUTSIDE, (int)(sizeof OUTSIDE / sizeof OUTSIDE[0]), 10, 70, 350, 126, 360 },
    [BOOPIE_ROOM_WOODS] = { WOODS, (int)(sizeof WOODS / sizeof WOODS[0]), 10, 70, 510, 126, 520 },
};

int boopie_room_width(boopie_room_t room)
{
    return (int)room >= 0 && room < BOOPIE_ROOM_COUNT ? ROOMS[room].width : BOOPIE_WORLD_W;
}

/* Where the view would be, the pet in its middle. */
static float cam_for(const boopie_world_t *w)
{
    float c = w->x - BOOPIE_WORLD_W / 2.0f, max = (float)(boopie_room_width(w->room) - BOOPIE_WORLD_W);
    return c < 0 ? 0 : c > max ? max : c;
}

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
    if (room == BOOPIE_ROOM_OUTSIDE) {
        return BOOPIE_ART_BG_OUTSIDE;
    }
    if (room == BOOPIE_ROOM_WOODS) {
        return BOOPIE_ART_BG_WOODS;
    }
    if (room == BOOPIE_ROOM_BEDROOM) {
        return level >= 10 ? BOOPIE_ART_BG_UP_STARS : BOOPIE_ART_BG_UP;
    }
    return level >= 10 ? BOOPIE_ART_BG_DOWN_FANCY : BOOPIE_ART_BG_DOWN;
}

/* ---- furniture and the woods' pickings ---- */

static const struct {
    const char *name, *where;
    int price;
} FURNI[BOOPIE_FURNI_COUNT] = {
    [BOOPIE_FURNI_CLOCK] = { "挂钟", "客厅", 15 },     [BOOPIE_FURNI_RECORD] = { "唱片机", "客厅", 25 },
    [BOOPIE_FURNI_LIGHTS] = { "星星串灯", "卧室", 20 }, [BOOPIE_FURNI_TEDDY] = { "小熊玩偶", "卧室", 15 },
    [BOOPIE_FURNI_SWING] = { "秋千", "院子", 40 },     [BOOPIE_FURNI_WINDMILL] = { "风车", "农场", 60 },
};
static uint32_t s_furniture;   /* out now */

const char *boopie_furni_name(boopie_furni_t f)
{
    return (int)f >= 0 && f < BOOPIE_FURNI_COUNT ? FURNI[f].name : "";
}

int boopie_furni_price(boopie_furni_t f)
{
    return (int)f >= 0 && f < BOOPIE_FURNI_COUNT ? FURNI[f].price : 0;
}

const char *boopie_furni_where(boopie_furni_t f)
{
    return (int)f >= 0 && f < BOOPIE_FURNI_COUNT ? FURNI[f].where : "";
}

void boopie_world_set_furniture(uint32_t out)
{
    s_furniture = out;
}

const char *boopie_item_name(boopie_item_t item)
{
    return item == BOOPIE_ITEM_BERRY ? "蓝莓" : item == BOOPIE_ITEM_MUSHROOM ? "蘑菇" : "";
}

boopie_item_t boopie_gather_item(int spot)
{
    return spot >= 3 ? BOOPIE_ITEM_BERRY : BOOPIE_ITEM_MUSHROOM;   /* the woods' table: mushrooms, then bushes */
}

bool boopie_thing_shown(const boopie_thing_t *t, int level)
{
    if (t->act == BOOPIE_DO_FURNI && !(s_furniture >> t->arg & 1)) {
        return false;
    }
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
    if (boopie_room_width(w->room) > BOOPIE_WORLD_W) {
        return;   /* the view follows it: only the floor's box */
    }
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
    w->level = 1;
    boopie_world_enter(w, BOOPIE_ROOM_LIVING, BOOPIE_DO_NOTHING);
}

const boopie_thing_t *boopie_world_thing(const boopie_world_t *w, int i)
{
    int n;
    const boopie_thing_t *t = boopie_room_things(w->room, &n);
    return i >= 0 && i < n ? &t[i] : NULL;
}

/* ---- the slimes ---- */

int boopie_slime_stars(boopie_slime_kind_t kind)
{
    static const int STARS[BOOPIE_SLIME_KINDS] = { 1, 2, 3, 5 };
    return (int)kind >= 0 && kind < BOOPIE_SLIME_KINDS ? STARS[kind] : 0;
}

int boopie_slime_xp(boopie_slime_kind_t kind)
{
    static const int XP[BOOPIE_SLIME_KINDS] = { 8, 10, 12, 15 };
    return (int)kind >= 0 && kind < BOOPIE_SLIME_KINDS ? XP[kind] : 0;
}

/* Hits it takes, and how quick its hops are (seconds a hop, and between) in a fight. */
static const uint8_t SLIME_HP[BOOPIE_SLIME_KINDS] = { 3, 4, 5, 3 };
static const float SLIME_HOP_S[BOOPIE_SLIME_KINDS] = { 0.55f, 0.48f, 0.42f, 0.32f };
static const float SLIME_WAIT_S[BOOPIE_SLIME_KINDS] = { 0.55f, 0.45f, 0.35f, 0.22f };

static void spawn(boopie_world_t *w, int i)
{
    boopie_slime_t *s = &w->slimes[i];
    memset(s, 0, sizeof *s);
    int roll = (int)(rnd(w) % 100);
    s->kind = roll < 6                        ? BOOPIE_SLIME_GOLD
              : w->level >= 12 && roll < 36   ? BOOPIE_SLIME_PINK
              : w->level >= 6 && roll < 66    ? BOOPIE_SLIME_BLUE
                                              : BOOPIE_SLIME_GREEN;
    s->hp = s->hp_max = SLIME_HP[s->kind];
    s->state = BOOPIE_SLIME_ROAM;
    s->x = SLIME_HOME[i] + (frand(w) - 0.5f) * SLIME_RANGE;
    s->y = 84 + frand(w) * 36;
    s->hop = -1;
    s->rest = 0.5f + frand(w) * 2;
}

static void hop_to(boopie_slime_t *s, float x, float y)
{
    s->fx = s->x;
    s->fy = s->y;
    s->tx = x;
    s->ty = y;
    s->hop = 0;
}

/* Its next hop: about its patch, or round the pet in a fight, kept in view. */
static void next_hop(boopie_world_t *w, int i)
{
    boopie_slime_t *s = &w->slimes[i];
    float x, y;
    if (s->state == BOOPIE_SLIME_FIGHT) {
        float side = frand(w) < 0.5f ? -1 : 1;
        x = w->x + side * (14 + frand(w) * 26);
        y = w->y + (frand(w) - 0.5f) * 36;
        float lo = w->cam + 14, hi = w->cam + BOOPIE_WORLD_W - 14;
        if (x < lo || x > hi) {
            x = w->x - side * (14 + frand(w) * 26);
        }
        x = x < lo ? lo : x > hi ? hi : x;
    } else {
        x = s->x + (frand(w) - 0.5f) * 24;
        y = s->y + (frand(w) - 0.5f) * 14;
        float home = SLIME_HOME[i];
        x = x < home - SLIME_RANGE ? home - SLIME_RANGE : x > home + SLIME_RANGE ? home + SLIME_RANGE : x;
    }
    /* Above the buttons along the screen's bottom, in a fight, to be tapped. */
    float bottom = s->state == BOOPIE_SLIME_FIGHT ? 114 : 124;
    y = y < 78 ? 78 : y > bottom ? bottom : y;
    hop_to(s, x, y);
}

static void slime_tick(boopie_world_t *w, int i, float dt)
{
    boopie_slime_t *s = &w->slimes[i];
    s->t += dt;
    s->hit += dt;
    switch (s->state) {
    case BOOPIE_SLIME_AWAY:
        if (s->t >= SLIME_BACK_S) {
            spawn(w, i);
        }
        return;
    case BOOPIE_SLIME_POOF:
        s->z = 0;
        if (s->t >= 0.7f) {
            s->state = BOOPIE_SLIME_AWAY;
            s->t = 0;
        }
        return;
    default:
        break;
    }
    bool fight = s->state == BOOPIE_SLIME_FIGHT;
    if (s->hop < 0) {
        s->z = 0;
        s->rest -= dt;
        if (s->rest <= 0) {
            next_hop(w, i);
        }
        return;
    }
    float len = fight ? SLIME_HOP_S[s->kind] : 0.6f;
    s->hop += dt / len;
    if (s->hop >= 1) {
        s->x = s->tx;
        s->y = s->ty;
        s->z = 0;
        s->hop = -1;
        s->rest = fight ? SLIME_WAIT_S[s->kind] : 1.2f + frand(w) * 2.5f;
        return;
    }
    s->x = s->fx + (s->tx - s->fx) * s->hop;
    s->y = s->fy + (s->ty - s->fy) * s->hop;
    s->z = sinf(s->hop * 3.14159f) * (fight ? 9 : 5);
}

/* A tap's on slime i: its body, where it is in its hop, and a little round it. */
static bool on_slime(const boopie_slime_t *s, float x, float y)
{
    if (s->state != BOOPIE_SLIME_ROAM && s->state != BOOPIE_SLIME_FIGHT) {
        return false;
    }
    float top = s->y - s->z - 18, bottom = s->y + 4;
    return x >= s->x - 12 && x <= s->x + 12 && y >= top && y <= bottom;
}

static int fight_tap(boopie_world_t *w, float x, float y)
{
    boopie_slime_t *s = &w->slimes[w->fight];
    if (!on_slime(s, x, y) || s->hit < 0.2f) {
        return -6;
    }
    s->hit = 0;
    if (s->hp > 0) {
        s->hp--;
    }
    w->facing = s->x < w->x ? -1 : 1;
    if (s->hp == 0) {
        s->state = BOOPIE_SLIME_POOF;
        s->t = 0;
        w->last_slime = s->kind;
        w->event = BOOPIE_DO_SLIME_WIN;
        w->fight = -1;
        w->state = BOOPIE_PET_IDLE;
        w->state_t = 0;
        w->idle_for = 2.0f;
    } else {
        next_hop(w, (int)(s - w->slimes));   /* off it springs */
    }
    return -5;
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
    boopie_do_t way = from == BOOPIE_DO_UPSTAIRS     ? BOOPIE_DO_DOWNSTAIRS
                      : from == BOOPIE_DO_DOWNSTAIRS ? BOOPIE_DO_UPSTAIRS
                      : from == BOOPIE_DO_OUTSIDE    ? BOOPIE_DO_INSIDE
                      : from == BOOPIE_DO_INSIDE     ? BOOPIE_DO_OUTSIDE
                      : from == BOOPIE_DO_WILD       ? BOOPIE_DO_HOME_PATH
                      : from == BOOPIE_DO_HOME_PATH  ? BOOPIE_DO_WILD
                                                     : from;
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
    w->cam = cam_for(w);
    w->chase = w->fight = -1;
    w->event = BOOPIE_DO_NOTHING;
    for (int i = 0; i < BOOPIE_SLIMES; i++) {
        if (room == BOOPIE_ROOM_WOODS) {
            spawn(w, i);
        } else {
            w->slimes[i].state = BOOPIE_SLIME_AWAY;
        }
    }
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
    if (w->fight >= 0) {
        return fight_tap(w, x, y);
    }
    if (w->state != BOOPIE_PET_SLEEPING) {
        for (int i = 0; i < BOOPIE_SLIMES; i++) {
            if (on_slime(&w->slimes[i], x, y)) {
                walk_to(w, w->slimes[i].x, w->slimes[i].y, -1);
                w->chase = i;
                return -4;
            }
        }
    }
    w->chase = -1;
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
    w->level = level;
    w->state_t += dt;
    for (int i = 0; i < BOOPIE_SLIMES; i++) {
        slime_tick(w, i, dt);
    }
    if (w->event != BOOPIE_DO_NOTHING) {
        boopie_do_t e = w->event;
        w->event = BOOPIE_DO_NOTHING;
        return e;
    }
    if (w->fight >= 0) {
        /* Standing its ground, turned to the slime, till it's won or the time's out. */
        boopie_slime_t *s = &w->slimes[w->fight];
        w->facing = s->x < w->x ? -1 : 1;
        w->cam += (cam_for(w) - w->cam) * (dt * 4 > 1 ? 1 : dt * 4);
        w->fight_left -= dt;
        if (w->fight_left <= 0) {
            s->state = BOOPIE_SLIME_POOF;
            s->t = 0;
            w->last_slime = s->kind;
            w->fight = -1;
            w->state = BOOPIE_PET_IDLE;
            w->state_t = 0;
            w->idle_for = 2.0f;
            return BOOPIE_DO_SLIME_FLED;
        }
        return BOOPIE_DO_NOTHING;
    }
    if (w->chase >= 0) {
        boopie_slime_t *s = &w->slimes[w->chase];
        if (s->state != BOOPIE_SLIME_ROAM) {
            w->chase = -1;
        } else {
            float dx = s->x - w->x, dy = s->y - w->y;
            if (dx * dx + dy * dy < 16 * 16 || w->state != BOOPIE_PET_WALKING) {
                /* Caught up: the fight's on. */
                w->fight = w->chase;
                w->chase = -1;
                w->fight_left = BOOPIE_SLIME_FIGHT_S;
                w->state = BOOPIE_PET_IDLE;
                w->state_t = 0;
                s->state = BOOPIE_SLIME_FIGHT;
                s->t = 0;
                s->hit = 1;
                if (s->hop < 0) {
                    s->rest = 0.3f;
                }
                return BOOPIE_DO_SLIME_FIGHT;
            }
            w->tx = s->x;   /* after it as it hops */
            w->ty = s->y;
            on_floor(w, &w->tx, &w->ty);
        }
    }
    /* The view eases after the pet in a wide room. */
    float want = cam_for(w);
    w->cam += (want - w->cam) * (dt * 4 > 1 ? 1 : dt * 4);
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
        } else if (act == BOOPIE_DO_OUTSIDE) {
            boopie_world_enter(w, BOOPIE_ROOM_OUTSIDE, act);
        } else if (act == BOOPIE_DO_INSIDE) {
            boopie_world_enter(w, BOOPIE_ROOM_LIVING, act);
        } else if (act == BOOPIE_DO_WILD) {
            boopie_world_enter(w, BOOPIE_ROOM_WOODS, act);
        } else if (act == BOOPIE_DO_HOME_PATH) {
            boopie_world_enter(w, BOOPIE_ROOM_OUTSIDE, act);
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
