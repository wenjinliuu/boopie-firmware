/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "boopie_season.h"
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
    BOOPIE_ROOM_OUTSIDE,      /* 户外: the house front and the farm, wider than the screen */
    BOOPIE_ROOM_WOODS,        /* 森林: a long walk east, slimes and chests */
    BOOPIE_ROOM_BEACH,        /* 海边: west of the yard, fishing and shells */
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
    BOOPIE_DO_PLOT,           /* a farm plot (arg: which): plant, water, pick */
    BOOPIE_DO_INSIDE,         /* the house's door, from outside */
    BOOPIE_DO_MAIL,           /* the mailbox */
    BOOPIE_DO_WILD,           /* the sign to the woods */
    BOOPIE_DO_HOME_PATH,      /* the woods' sign back to the yard */
    BOOPIE_DO_CHEST,          /* a chest (arg: which): once a day */
    BOOPIE_DO_SLIME_FIGHT,    /* the pet's reached a slime: the fight's on */
    BOOPIE_DO_SLIME_WIN,      /* ... and won (boopie_world_t.last_slime: which kind) */
    BOOPIE_DO_SLIME_FLED,     /* ... or the time ran out and it got away */
    BOOPIE_DO_GATHER,         /* berries or a mushroom to pick (arg: the spot), once a day */
    BOOPIE_DO_FURNI,          /* furniture from the shop (arg: which); shown once bought and put out */
    BOOPIE_DO_BEACH,          /* the yard's sign to the beach */
    BOOPIE_DO_FISH,           /* the pier's end: the line's out */
    BOOPIE_DO_FISH_BITE,      /* a fish on it: tap now! */
    BOOPIE_DO_FISH_CAUGHT,    /* ... caught (boopie_world_t.last_fish: what) */
    BOOPIE_DO_FISH_EARLY,     /* tapped before a bite: it swam off */
    BOOPIE_DO_FISH_MISSED,    /* not tapped in time */
    BOOPIE_DO_ANTIC,          /* it's begun a little something of its own (boopie_world_t.antic) */
    BOOPIE_DO_DECOR,          /* a festival's or the weather's: arg the festival, or 16 + the weather */
    BOOPIE_DO_SHELTER,        /* rain: it's off home out of it */
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
    uint8_t arg;              /* the plot, the chest, the spot, the furniture */
} boopie_thing_t;

/*
 * Furniture from the shop (docs/boopie-world.md), each with its own place in
 * a room. Which are out (bought and not put away) is the screen's to say.
 */
typedef enum {
    BOOPIE_FURNI_CLOCK = 0,   /* 挂钟: tells the time */
    BOOPIE_FURNI_RECORD,      /* 唱片机 */
    BOOPIE_FURNI_LIGHTS,      /* 星星串灯, lit at night */
    BOOPIE_FURNI_TEDDY,       /* 小熊玩偶 */
    BOOPIE_FURNI_SWING,       /* 秋千 */
    BOOPIE_FURNI_WINDMILL,    /* 风车, turning */
    BOOPIE_FURNI_COUNT,
} boopie_furni_t;
const char *boopie_furni_name(boopie_furni_t f);
int boopie_furni_price(boopie_furni_t f);
const char *boopie_furni_where(boopie_furni_t f);   /* "客厅" ... */
void boopie_world_set_furniture(uint32_t out);      /* bit f: furniture f is out */
/* Today's festival and weather, for what's put out for them. */
void boopie_world_set_season(boopie_fest_t fest, boopie_weather_t weather);
bool boopie_world_outdoors(boopie_room_t room);

/* What can be picked in the woods: each spot gives one of these, once a day. */
/* Found in the woods and on the beach; then from the shop (a price), only ever appended. */
typedef enum {
    BOOPIE_ITEM_BERRY = 0,
    BOOPIE_ITEM_MUSHROOM,
    BOOPIE_ITEM_SHELL,
    BOOPIE_ITEM_FISH,
    BOOPIE_ITEM_COOKIE,   /* 小饼干: a treat */
    BOOPIE_ITEM_CAKE,     /* 小蛋糕: a bigger one */
    BOOPIE_ITEM_POPPER,   /* 礼炮: set off for confetti */
    BOOPIE_ITEM_COUNT
} boopie_item_t;
const char *boopie_item_name(boopie_item_t item);
bool boopie_item_edible(boopie_item_t item);   /* shells are only to keep, a popper to set off */
int boopie_item_price(boopie_item_t item);     /* stars in the shop; 0: not sold */
int boopie_item_treat_xp(boopie_item_t item);  /* a treat's experience, over the day's caps; 0 for the rest */
boopie_item_t boopie_gather_item(int spot);    /* the woods' 0 to 4, the beach's 5 to 7 */
#define BOOPIE_GATHER_SPOTS 8

/*
 * Fishing off the pier: the line's out a few seconds, then a bite, and a tap
 * within a moment lands it. What bites: mostly a small fish, from level 8
 * now and then a big one, rarely a golden one, and sometimes an old boot.
 */
typedef enum { BOOPIE_FISH_SMALL = 0, BOOPIE_FISH_BIG, BOOPIE_FISH_GOLD, BOOPIE_FISH_BOOT, BOOPIE_FISH_KINDS } boopie_fish_t;
#define BOOPIE_FISH_BITE_S 1.2f   /* a bite lasts this long */

/* A room's things, and its background at a level. */
const boopie_thing_t *boopie_room_things(boopie_room_t room, int *count);
boopie_art_id_t boopie_room_background(boopie_room_t room, int level);
bool boopie_thing_shown(const boopie_thing_t *t, int level);
/* Where the pet may walk: a box on the floor. */
void boopie_room_floor(boopie_room_t room, int *x0, int *y0, int *x1, int *y1);
/* How wide it is: the screen's width, or more (then the view follows the pet). */
int boopie_room_width(boopie_room_t room);

typedef enum {
    BOOPIE_PET_IDLE = 0,
    BOOPIE_PET_WALKING,
    BOOPIE_PET_USING,
    BOOPIE_PET_SLEEPING,
    BOOPIE_PET_ANTIC,         /* at a little something of its own */
} boopie_pet_state_t;

/*
 * What it gets up to, left alone (docs/boopie-world.md "小动作"): now and then,
 * instead of wandering, something that suits the room: the TV (on the sofa
 * once there is one), a dance by the record player, the mirror, some love
 * for the teddy, the flowers or its sandcastle, the swing, a butterfly to
 * chase in the yard or the woods, a swim at the beach. A tap ends it.
 */
typedef enum {
    BOOPIE_ANTIC_NONE = 0,
    BOOPIE_ANTIC_TV,
    BOOPIE_ANTIC_DANCE,
    BOOPIE_ANTIC_MIRROR,
    BOOPIE_ANTIC_LOVE,        /* antic_art: what it loves */
    BOOPIE_ANTIC_SWING,
    BOOPIE_ANTIC_BUTTERFLY,
    BOOPIE_ANTIC_SWIM,
    BOOPIE_ANTIC_COUNT,
} boopie_antic_t;

/*
 * The woods' slimes: each hops about its own patch. Tap one and the pet goes
 * to it; there the fight's on: the slime hops round the pet, and each tap on
 * it is a hit, until it's out of hits (won) or the time is (it gets away).
 * Beaten or gone, it's back a while later, maybe another colour. Green from
 * the start, blue from level 6, pink from 12; now and then a quick gold one.
 */
#define BOOPIE_SLIMES 3
#define BOOPIE_SLIME_FIGHT_S 12.0f
typedef enum { BOOPIE_SLIME_GREEN = 0, BOOPIE_SLIME_BLUE, BOOPIE_SLIME_PINK, BOOPIE_SLIME_GOLD, BOOPIE_SLIME_KINDS } boopie_slime_kind_t;
typedef enum { BOOPIE_SLIME_AWAY = 0, BOOPIE_SLIME_ROAM, BOOPIE_SLIME_FIGHT, BOOPIE_SLIME_POOF } boopie_slime_state_t;

typedef struct {
    uint8_t state;            /* boopie_slime_state_t */
    uint8_t kind;             /* boopie_slime_kind_t */
    uint8_t hp, hp_max;
    float x, y;               /* where it is, on the ground */
    float z;                  /* how high it is in its hop */
    float fx, fy, tx, ty;     /* a hop: from, to */
    float hop;                /* how far through it (0 to 1), or < 0 resting */
    float rest;               /* seconds till the next hop */
    float t;                  /* seconds in this state */
    float hit;                /* seconds since it was hit (squashed a moment) */
} boopie_slime_t;

/* Stars and experience a kind is worth. */
int boopie_slime_stars(boopie_slime_kind_t kind);
int boopie_slime_xp(boopie_slime_kind_t kind);

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
    float cam;                /* the view's left edge in a wide room */
    boopie_slime_t slimes[BOOPIE_SLIMES];
    int chase;                /* the slime it's going for, or -1 */
    int fight;                /* the slime it's fighting, or -1 */
    float fight_left;         /* seconds */
    int last_slime;           /* the kind just beaten or gone */
    boopie_do_t event;        /* for the next tick to hand on */
    int level;                /* the last level ticked with */
    bool fishing;             /* the line's out */
    bool bite;                /* and a fish is on it */
    float fish_t;             /* till the bite, or while it lasts */
    int fish_kind;            /* what's biting */
    int last_fish;            /* what was caught */
    uint8_t antic;            /* boopie_antic_t: going to it, or at it */
    uint8_t antic_phase;      /* a swim: 0 to the water's edge, 1 in, 2 swimming, 3 out */
    uint8_t antic_art;        /* LOVE: the thing's art */
    float antic_left;         /* seconds more of it */
    float bx, by, bcx, bcy;   /* the butterfly, and where it flutters round */
    float b_away;             /* seconds it's been flying off, or 0 */
} boopie_world_t;

/* Starts one now (the simulator, the tests), if the room has it: true. */
bool boopie_world_antic(boopie_world_t *w, int level, boopie_antic_t antic);
/* The one going on (or being walked to), or NONE. */
static inline boopie_antic_t boopie_world_antic_on(const boopie_world_t *w)
{
    return (boopie_antic_t)w->antic;
}

void boopie_world_init(boopie_world_t *w, uint32_t seed);

/* Into a room, at the way in from `from` (the stairs, the door), or its middle. */
void boopie_world_enter(boopie_world_t *w, boopie_room_t room, boopie_do_t from);

/*
 * A tap at (x, y), in the room (add w->cam to a point on the screen): on a thing that does something, the pet goes to use it
 * (and its index comes back); on the floor, it walks there (-1); elsewhere
 * nothing (-2). Asleep, a tap anywhere but the bed wakes it (-3). In the
 * woods: on a slime, off it goes after it (-4); fighting, a tap is a hit on
 * the slime (-5) or a miss (-6), and nothing else. Fishing, a tap reels in:
 * a catch with a bite on (-7), too soon without (-8).
 */
int boopie_world_tap(boopie_world_t *w, int level, float x, float y);

/* Moves time on dt seconds; what to do now that the pet's reached a thing, or NOTHING. */
boopie_do_t boopie_world_tick(boopie_world_t *w, int level, float dt);

/* To bed (it walks there and sleeps) or up. */
void boopie_world_sleep(boopie_world_t *w, int level, bool on);

/* The thing at index i of the current room. */
const boopie_thing_t *boopie_world_thing(const boopie_world_t *w, int i);
