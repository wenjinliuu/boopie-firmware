/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "boopie_expr.h"
#include "boopie_pet.h"
#include "boopie_pixel.h"

/*
 * Which character is on screen. boopie_avatar.c stands in for Muse's
 * muse_pixel_* (muse_pixel.h), so the UI is unchanged: it passes Muse's own
 * character to the official renderer (avatar/muse_pixel.c, built with its
 * functions renamed jolly_pixel_*) and every other to boopie_pixel.c.
 *
 * Avatar 0 is Muse; 1 ... BOOPIE_CHAR_COUNT are boopie_char_t + 1; Boopie is
 * shown until another is chosen. The choice and each character's colour are
 * kept in NVS (namespace "boopie") on the device; the simulator takes them
 * from BOOPIE_AVATAR and BOOPIE_COLOUR.
 */

#define BOOPIE_AVATAR_MUSE 0
#define BOOPIE_AVATAR_BOOPIE (BOOPIE_CHAR_BOOPIE + 1)
#define BOOPIE_AVATAR_COUNT (BOOPIE_CHAR_COUNT + 1)

/* Its id ("muse", "boopie") and display name; NULL out of range. */
const char *boopie_avatar_key(int avatar);
const char *boopie_avatar_name(int avatar);

int boopie_avatar_current(void);
void boopie_avatar_select(int avatar);

/* The idle background, any character; kept in NVS like the rest. */
boopie_scene_t boopie_avatar_scene(void);
void boopie_avatar_set_scene(boopie_scene_t scene);

/* Muse's own character keeps its colours. */
bool boopie_avatar_recolourable(int avatar);

/* The current character's body colour, 0xRRGGBB, or BOOPIE_COLOUR_DEFAULT
 * for its own. */
uint32_t boopie_avatar_colour(void);
void boopie_avatar_set_colour(uint32_t rgb);

/*
 * A pet expression (hungry, sleepy, ...) shown while Muse is idle; IDLE, or
 * any core expression, clears it. A character that hasn't drawn it shows its
 * fallback (boopie_expr_resolve), so Muse's own character shows a core one.
 */
void boopie_avatar_set_pet(boopie_expr_t expr);
boopie_expr_t boopie_avatar_pet(void);

/* A pet expression for a few seconds while idle, over the pet one: dizzy
 * when shaken, eating when fed. */
void boopie_avatar_react(boopie_expr_t expr, float seconds);

/*
 * A reaction overlay on or off. LOW_BATTERY and CHARGING follow the battery
 * by themselves (muse_state_power), whatever is set here.
 */
void boopie_avatar_set_overlay(boopie_overlay_t overlay, bool on);

/*
 * The display.avatar command: any of these may be NULL to leave it as is.
 * avatar is an id ("boopie"); colour is RRGGBB or "default"; pet an
 * expression id ("hungry", "idle" to clear); reaction an overlay id, turned
 * on or off; scene a background id ("stars"); skin an owned skin's id, or
 * "none"; accessory an id ("crown"), put on or taken off. On a bad value
 * returns false with *error set, changing nothing.
 */
bool boopie_avatar_command(const char *avatar, const char *colour, const char *pet, const char *reaction,
                           const char *scene, const char *skin, const char *accessory, bool on,
                           const char **error);

/* Skins (boopie_skin_*): the one the current character wears (-1 none),
 * whether one is owned, wear an owned one (-1 takes it off), and buy one with
 * stars (then wear it). False with *error set when it can't. A skin brings its
 * background while the background chosen is the default. */
int boopie_avatar_skin(void);
int boopie_avatar_of_skin(int skin);   /* the avatar it's for */
bool boopie_avatar_owns(int skin);
bool boopie_avatar_wear(int skin, const char **error);
bool boopie_avatar_buy(int skin, const char **error);

/* Accessories (boopie_acc_t) the current character wears, BOOPIE_ACC_BIT()s;
 * each character keeps its own. Put one on (a hat replaces the hat worn) or
 * take it off; false with *error set if it isn't unlocked yet. */
uint32_t boopie_avatar_accessories(void);
bool boopie_avatar_set_accessory(boopie_acc_t acc, bool on, const char **error);

/*
 * The pet's name: the one the user gave it, or else its character's ("布比",
 * "小克" ... follows the character chosen). Setting "" goes back to the
 * character's. At most BOOPIE_PET_NAME_CHARS characters, kept in NVS.
 */
#define BOOPIE_PET_NAME_CHARS 8
#define BOOPIE_PET_NAME_MAX 40   /* bytes, with the terminator */
const char *boopie_avatar_pet_name(void);
bool boopie_avatar_has_own_name(void);
bool boopie_avatar_set_pet_name(const char *name, const char **error);

/* ---- the pet (pet/boopie_pet.c), kept here with the rest ---- */

/* The character's canvas was tapped at grid cell (gx, gy): on the food
 * bowl while it's hungry, it eats (true); anywhere else it's a poke (false,
 * and the caller shows Muse's pet reaction). */
bool boopie_avatar_tap(int gx, int gy);

typedef struct {
    int level;
    uint32_t xp, xp_into, xp_need;   /* all, into this level, this level takes */
    uint32_t stars;
    bool hungry;
    boopie_expr_t mood;              /* IDLE, HUNGRY, SAD or SLEEPY */
} boopie_pet_status_t;

void boopie_avatar_pet_status(boopie_pet_status_t *out);

/* A game round ended with `score`: its reward (xp, stars) goes to the pet, up
 * to the day's caps; *ev says what was given, *best is the best score of that
 * game so far (kept in NVS) and *record whether this round set it. */
typedef enum { BOOPIE_GAME_WHACK = 0, BOOPIE_GAME_COUNT } boopie_game_t;
void boopie_avatar_game_result(boopie_game_t game, int score, int xp, int stars, boopie_pet_event_t *ev,
                               int *best, bool *record);

/* Item `index` of a kind is unlocked; *level (if given) the level it takes. */
bool boopie_avatar_unlocked(boopie_unlock_kind_t kind, int index, int *level);
