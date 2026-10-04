/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "boopie_world.h"

struct boopie_garden;

/*
 * Drawing the world (docs/boopie-world.md): a room at 156 x 156 into an RGB
 * buffer, then three times over into the 466 px screen, with the clock and
 * the stars on top. Plain C.
 */

typedef struct {
    int level;
    bool night;               /* dim and blue, the lamps lit */
    bool hungry;              /* the bowl empty */
    float t;                  /* seconds, for the bobbing and twinkling */
    const uint16_t *pet;      /* the pet's picture, RGB565, 0 see-through */
    int pet_w, pet_h;
    const struct boopie_garden *garden;   /* the farm's plots (NULL: all bare) */
    int64_t now;              /* epoch seconds, for the garden */
    unsigned chests_open;     /* the woods' chests opened today, bit by bit */
    unsigned gathered;        /* the woods' spots picked today, bit by bit */
} boopie_world_look_t;

/* The room into rgb (BOOPIE_WORLD_W squared, 3 bytes a pixel). */
void boopie_world_draw(const boopie_world_t *w, const boopie_world_look_t *look, uint8_t *rgb);

/* Three times over into out (out_w square, RGB565, out_w <= 3 * BOOPIE_WORLD_W). */
void boopie_world_scale(const uint8_t *rgb, uint16_t *out, int out_w);

/* The clock ("13:26", in big pixel digits) and the stars, along the top. */
void boopie_world_hud(uint16_t *out, int out_w, const char *clock, unsigned stars);
