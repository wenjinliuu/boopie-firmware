/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "boopie_expr.h"

/*
 * Boopie's pixel characters, drawn the way Muse's official avatar is: a 64 x 64
 * grid, dithered shading, a state-tinted glow, rim light and sparkles, blown up
 * with nearest-neighbour blocks. A C port of tools/boopie/avatar_proto.py,
 * which is the reference: tests/test_boopie_pixel.py renders both and compares.
 *
 * One expression engine drives every character; a character only draws its
 * body and face. Plain C with no IDF dependencies, so it builds on the host.
 */

#define BOOPIE_PX 64

/* Where the food overlay's bowl sits, in grid cells (inclusive): a tap
 * inside feeds the pet. */
#define BOOPIE_FOOD_X0 46
#define BOOPIE_FOOD_Y0 42
#define BOOPIE_FOOD_X1 63
#define BOOPIE_FOOD_Y1 59

/* What the food overlay offers: a different one each meal. */
typedef enum {
    BOOPIE_FOOD_RICE = 0,
    BOOPIE_FOOD_DRUMSTICK,
    BOOPIE_FOOD_ONIGIRI,
    BOOPIE_FOOD_FISH,
    BOOPIE_FOOD_COOKIE,
    BOOPIE_FOOD_COUNT,
} boopie_food_t;

typedef enum {
    BOOPIE_CHAR_BOOPIE = 0,
    BOOPIE_CHAR_GPT,
    BOOPIE_CHAR_CODEX,
    BOOPIE_CHAR_KLAUDE,
    BOOPIE_CHAR_WHALE,
    BOOPIE_CHAR_DOUBAO,
    BOOPIE_CHAR_COUNT,
} boopie_char_t;

/* Idle backgrounds, drawn behind the character, in front of it, or over the
 * whole frame (glitch). DEFAULT is Muse's own glow and sparkles alone. */
typedef enum {
    BOOPIE_SCENE_DEFAULT = 0,
    BOOPIE_SCENE_STARS,
    BOOPIE_SCENE_FIREFLIES,
    BOOPIE_SCENE_SNOW,
    BOOPIE_SCENE_PETALS,
    BOOPIE_SCENE_BUBBLES,
    BOOPIE_SCENE_MATRIX,
    BOOPIE_SCENE_NEON_GRID,
    BOOPIE_SCENE_GLITCH,
    BOOPIE_SCENE_COUNT,
} boopie_scene_t;

/* Its id ("stars") and display name ("星空"); NULL out of range. */
const char *boopie_scene_key(boopie_scene_t s);
const char *boopie_scene_name(boopie_scene_t s);

/* Its id ("boopie", "whale") and display name ("布比"); NULL out of range. */
const char *boopie_char_key(boopie_char_t c);
const char *boopie_char_name(boopie_char_t c);

/* Its own body colour, 0xRRGGBB. */
uint32_t boopie_char_default_colour(boopie_char_t c);

/* The character drawn from now on, in body colour `colour` (0xRRGGBB), or its
 * own colour for BOOPIE_COLOUR_DEFAULT. */
#define BOOPIE_COLOUR_DEFAULT 0xffffffffu
void boopie_pixel_set_character(boopie_char_t c, uint32_t colour);

/*
 * Skins: a character restyled (colours, patterns, a background), bought with
 * stars. Indexed 0 ... boopie_skin_count() - 1, in a fixed order (only ever
 * appended), so the index is what's kept of what's owned.
 */
#define BOOPIE_SKIN_MUSE BOOPIE_CHAR_COUNT   /* boopie_skin_character() of Muse's own */

int boopie_skin_count(void);
const char *boopie_skin_key(int skin);          /* "boopie_starry" */
const char *boopie_skin_name(int skin);         /* "星空" */
boopie_char_t boopie_skin_character(int skin);  /* the character it's for, or BOOPIE_SKIN_MUSE */
int boopie_skin_price(int skin);                /* stars */
bool boopie_skin_collector(int skin);           /* 典藏 */
boopie_scene_t boopie_skin_scene(int skin);     /* the background it brings */
int boopie_skin_from_key(const char *key);      /* or -1 */

/*
 * A Muse skin's colours, 0xRRGGBB, for Muse's own renderer (avatar/muse_pixel.c):
 * its fur dark to highlight, outline and soft outline; and with face set, its
 * face panel's shade, colour and light, and the eyes and mouth on it. False
 * for a skin that isn't Muse's.
 */
typedef struct {
    uint32_t fur[4], out, out2;
    bool face;
    uint32_t panel[3], features;
} boopie_muse_colours_t;
bool boopie_skin_muse_colours(int skin, boopie_muse_colours_t *out);

/* Wear a skin (-1 for none) from the next boopie_pixel_set_character(); one
 * for another character is ignored. */
void boopie_pixel_set_skin(int skin);

/*
 * Accessories, worn over any skin: a hat (one at most) and a scarf. In the
 * order levels unlock them (boopie_pet_unlock_level); only ever appended.
 */
typedef enum {
    BOOPIE_ACC_BOW = 0,
    BOOPIE_ACC_CROWN,
    BOOPIE_ACC_SCARF,
    BOOPIE_ACC_PARTY_HAT,
    BOOPIE_ACC_COUNT,
} boopie_acc_t;

#define BOOPIE_ACC_BIT(a) (1u << (a))

const char *boopie_acc_key(boopie_acc_t a);    /* "party_hat"; NULL out of range */
const char *boopie_acc_name(boopie_acc_t a);   /* "生日帽" */
bool boopie_acc_is_hat(boopie_acc_t a);
bool boopie_acc_from_key(const char *key, boopie_acc_t *out);

/* What's worn from the next frame, BOOPIE_ACC_BIT()s. */
void boopie_pixel_set_wear(uint32_t worn);

/* The accent of an expression (glow, rim light, sparkles, the UI round it),
 * 0xRRGGBB. The core ones are Muse's official accents. */
uint32_t boopie_pixel_accent(boopie_expr_t e);

/* How long an expression's loop is, in seconds; BOOT and OFF play once. */
float boopie_pixel_loop(boopie_expr_t e);

/* An overlay's loop, in seconds. */
float boopie_overlay_loop(boopie_overlay_t o);

typedef struct {
    boopie_expr_t expr;
    double t;                /* seconds in this expression */
    float level;             /* 0..1 live voice level, or < 0 for a made-up one */
    boopie_overlay_set_t overlays;
    double overlay_t[BOOPIE_OVERLAY_COUNT];  /* seconds each has been on */
    float dt;                /* seconds since the last frame, for easing the
                              * accent; 0 jumps straight to it */
    boopie_food_t food;      /* what the food overlay offers */
    boopie_scene_t scene;    /* the background */
    double scene_t;          /* seconds the background has run */
} boopie_pixel_pose_t;

/* Render one frame into the grid. */
void boopie_pixel_render(const boopie_pixel_pose_t *pose);

/* The frame, BOOPIE_PX x BOOPIE_PX RGB, 3 bytes a pixel, row by row. */
const uint8_t *boopie_pixel_rgb(void);

/* As muse_pixel_set_size() / muse_pixel_scale(). */
void boopie_pixel_set_size(int px);
void boopie_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1);

/*
 * A frame some other renderer drew (Muse's own: palette indices and an RGB565
 * palette, bit i of bg_mask set for each index that is background) composed
 * with this file's background scene and overlays, ready for
 * boopie_pixel_scale(). pose's expression is ignored.
 */
void boopie_pixel_compose(const uint8_t *fb, const uint16_t *palette, uint32_t bg_mask,
                          const boopie_pixel_pose_t *pose);

/* Where boopie_pixel_compose() puts the accessories worn on that frame, in
 * grid cells: a hat's bottom centre x, y; a scarf's centre x, y and width.
 * NULL wears none. */
void boopie_pixel_set_slots(const float slots[5]);

/* 戳戳布比 (game/boopie_whack.h) drawn into the grid, read back with boopie_pixel_rgb():
 * the pets popping up are `head`, a boopie_char_t as it's dressed now, or
 * BOOPIE_SKIN_MUSE for Muse's own. */
struct boopie_whack;
void boopie_pixel_render_whack(const struct boopie_whack *g, int head);

/* 接零食 (game/boopie_catch.h), 重力迷宫 (game/boopie_maze.h) and 跳跳布比
 * (game/boopie_hop.h), the same way:
 * the pet catching is `head`, as boopie_pixel_render_whack(). */
struct boopie_catch;
struct boopie_maze;
struct boopie_hop;
void boopie_pixel_render_catch(const struct boopie_catch *g, int head);
void boopie_pixel_render_maze(const struct boopie_maze *g);
void boopie_pixel_render_hop(const struct boopie_hop *g, int head);

/* A character's 13 x 10 head (as in the games) in its own colours, `scale`
 * pixels a cell, RGB565 row by row into dst (13*scale x 10*scale); black
 * round it. head as boopie_pixel_render_whack(). */
#define BOOPIE_HEAD_W 13
#define BOOPIE_HEAD_H 10
void boopie_pixel_head_image(int head, uint16_t *dst, int scale);
