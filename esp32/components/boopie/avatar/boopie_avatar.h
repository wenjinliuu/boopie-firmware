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
#include "boopie_season.h"

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
/* A festival skin is on sale only in its festival (boopie_skin_limited);
 * the rest always. Its festival's word for the shop ("春节限定"), or "". */
bool boopie_avatar_skin_on_sale(int skin);
const char *boopie_avatar_skin_when(int skin);
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

/* Fed as the bowl's tap feeds it, for the AI's pet.feed: false (and nothing
 * done) if it isn't hungry. */
bool boopie_avatar_feed(void);

typedef struct {
    int level;
    uint32_t xp, xp_into, xp_need;   /* all, into this level, this level takes */
    uint32_t stars;
    bool hungry;
    boopie_expr_t mood;              /* IDLE, HUNGRY, SAD or SLEEPY */
} boopie_pet_status_t;

void boopie_avatar_pet_status(boopie_pet_status_t *out);

/*
 * Touch and posture (docs/boopie-interaction.md "摸摸" and "姿势感应").
 * stroke: the LVGL task, each back and forth of a finger held on the pet
 * (strokes so far this time), or a hug (held still); the first of a time
 * earns a little experience. greet / upside_down: any task, as the board is
 * picked up or turned over. posture_on: the switch, kept in NVS.
 */
void boopie_avatar_stroke(int strokes, bool hug);

/* 小花园, kept with the pet: up to date at *now (epoch seconds), or NULL
 * while the clock isn't set. The LVGL task. After a change call
 * garden_changed, with a harvest's reward (0, 0 for none). */
struct boopie_garden;
struct boopie_garden *boopie_avatar_garden(int64_t *now, int *minute);
int boopie_avatar_garden_changed(int xp, int stars);   /* the stars given */

/* 森林 (docs/boopie-world.md): the chests opened today (bit i for chest i), and
 * opening one, its reward given toward the games' daily caps (false if it
 * was opened today, or the clock isn't set); a slime beaten, its reward the
 * same way (*got_stars what was given), and how many so far. LVGL task. */
unsigned boopie_avatar_chests_open(void);
bool boopie_avatar_open_chest(int chest, int *stars, int *xp);
void boopie_avatar_slime_beaten(int stars, int xp, int *got_stars);
uint32_t boopie_avatar_slimes_beaten(void);

/* What the woods give (boopie_item_t): the spots picked today (bit i for spot
 * i), and picking one into the bag (false if picked today, or no clock);
 * how many of an item are in the bag, and giving one to the pet: fed if it's
 * hungry (*fed), else just a snack. */
unsigned boopie_avatar_gathered(void);
bool boopie_avatar_gather(int spot, int item);
int boopie_avatar_items(int item);
/* Buys one of a shop item (boopie_item_price) into the bag; false with *error (Chinese). */
bool boopie_avatar_buy_item(int item, const char **error);
/* Sets off a popper from the bag: confetti and hearts. False if there's none. */
bool boopie_avatar_pop(void);
bool boopie_avatar_snack(int item, bool *fed);
/* Something won out there (a catch): `count` of an item into the bag (item
 * -1 for none), and experience and stars toward the daily caps. */
void boopie_avatar_world_reward(int item, int count, int xp, int stars, int *got_stars);

/* The day's weather (as the AI told it today, else by the season) and
 * festival, while the clock's set (else sunny, none); the AI telling it
 * (false without a clock). A festival's gift in the mailbox, once a year:
 * waiting, and opened (the stars it gave, 0 if none waiting). LVGL task. */
boopie_weather_t boopie_avatar_weather(void);
bool boopie_avatar_set_weather(boopie_weather_t w);
boopie_fest_t boopie_avatar_festival(void);
bool boopie_avatar_mail_waiting(void);
/* 小窝's how-to (点冒气泡的东西试试看): true for its first three times, counted. */
bool boopie_avatar_world_hint(void);
/* The pet's birthday (false while it isn't set), and setting it (false for no such day). */
bool boopie_avatar_birthday(int *month, int *mday);
bool boopie_avatar_set_birthday(int month, int mday);
int boopie_avatar_open_mail(void);

/* Furniture (boopie_furni_t): what's out (bought and not put away, bit f),
 * whether one's bought, buying one for `price` stars (then it's out; false
 * with *error, in Chinese, when it can't), and putting one away or out. */
/* Rare seeds (boopie_plant_t with a price): how many are to hand, buying one
 * for `price` stars (false with *error, in Chinese), and using one to plant. */
int boopie_avatar_seeds(int plant);
bool boopie_avatar_buy_seed(int plant, int price, const char **error);
bool boopie_avatar_use_seed(int plant);

uint32_t boopie_avatar_furniture(void);
bool boopie_avatar_furni_owned(int f);
bool boopie_avatar_buy_furni(int f, int price, const char **error);
void boopie_avatar_put_out(int f, bool out);
void boopie_avatar_greet(void);
void boopie_avatar_upside_down(bool on);
/* Being stroked or hugged now: "好舒服" / "抱抱" for the face's word, or NULL. */
const char *boopie_avatar_soothed(void);
bool boopie_avatar_posture_on(void);
void boopie_avatar_set_posture_on(bool on);

/* The reaction showing now (boopie_avatar_react: a feed, a poke), or IDLE. */
boopie_expr_t boopie_avatar_reacting(void);

/* How the last game or 小窝 reward went against the day's caps: 0 whole,
 * 1 halved, 2 none (done for the day). */
int boopie_avatar_tired(void);

/* Which AI answers, the AI assistant (AI 助手: chosen in the setup guide; Muse until one is), and
 * whether the guide has been through. Kept in NVS with the rest. */
typedef enum { BOOPIE_BRAIN_XIAOZHI = 0, BOOPIE_BRAIN_MUSE, BOOPIE_BRAIN_COUNT } boopie_brain_t;
boopie_brain_t boopie_avatar_brain(void);
void boopie_avatar_set_brain(boopie_brain_t brain);
bool boopie_avatar_guided(void);
void boopie_avatar_set_guided(bool done);

/* A game round ended with `score`: its reward (xp, stars) goes to the pet, up
 * to the day's caps; *ev says what was given, *best is the best score of that
 * game so far (kept in NVS) and *record whether this round set it. */
typedef enum { BOOPIE_GAME_WHACK = 0, BOOPIE_GAME_CATCH, BOOPIE_GAME_MAZE, BOOPIE_GAME_HOP, BOOPIE_GAME_COUNT } boopie_game_t;
void boopie_avatar_game_result(boopie_game_t game, int score, int xp, int stars, boopie_pet_event_t *ev,
                               int *best, bool *record);

/* Item `index` of a kind is unlocked; *level (if given) the level it takes. */
bool boopie_avatar_unlocked(boopie_unlock_kind_t kind, int index, int *level);

/*
 * Achievements: some things are earned, not bought or reached by level.
 * 史莱姆 skin: 100 slimes beaten; 草帽: 50 harvests; 金光环: 7 days running
 * (the best run); 金牌: 10 game records broken (past the first score).
 */
typedef enum { BOOPIE_GOAL_SLIMES = 0, BOOPIE_GOAL_HARVESTS, BOOPIE_GOAL_STREAK, BOOPIE_GOAL_RECORDS,
               BOOPIE_GOAL_COUNT, BOOPIE_GOAL_NONE = -1 } boopie_goal_t;
/* How far along: true once met, *have and *need as counted. */
bool boopie_avatar_goal(boopie_goal_t g, int *have, int *need);
/* The goal a skin or accessory is earned by, or BOOPIE_GOAL_NONE. */
boopie_goal_t boopie_avatar_skin_goal(int skin);
boopie_goal_t boopie_avatar_acc_goal(boopie_acc_t a);
/* How far along, for a row on the screen: "打败史莱姆 77/100", or "已达成". */
void boopie_avatar_goal_text(boopie_goal_t g, char *out, size_t cap);
