/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Boopie's expression list, shared by every avatar style (pixel, Boopie HD,
 * custom HD packs, ASCII). The system only ever names an expression; the
 * active style draws it. A style that hasn't drawn an extended expression yet
 * falls back along a fixed chain until it reaches one it has, and every chain
 * ends at a core expression, which every style must draw.
 *
 * Values are stable: only ever append. The first seven core expressions are in
 * the same order as muse_mode_t (MUSE_MODE_BOOT ... MUSE_MODE_OFF), so a Muse
 * mode converts with a cast; HAPPY is Muse's pet reaction.
 */
typedef enum {
    /* Core: every style draws these. */
    BOOPIE_EXPR_BOOT = 0,
    BOOPIE_EXPR_IDLE,
    BOOPIE_EXPR_LISTENING,
    BOOPIE_EXPR_THINKING,
    BOOPIE_EXPR_SPEAKING,
    BOOPIE_EXPR_ERROR,
    BOOPIE_EXPR_OFF,
    BOOPIE_EXPR_HAPPY,

    /* Extended: for the pet and games, each with a fallback. */
    BOOPIE_EXPR_HUNGRY,
    BOOPIE_EXPR_EATING,
    BOOPIE_EXPR_SLEEPY,
    BOOPIE_EXPR_SAD,
    BOOPIE_EXPR_SURPRISED,
    BOOPIE_EXPR_DIZZY,
    BOOPIE_EXPR_CELEBRATE,
    BOOPIE_EXPR_SHY,
    BOOPIE_EXPR_LOW_BATTERY,
    BOOPIE_EXPR_CHARGING,

    BOOPIE_EXPR_COUNT,
} boopie_expr_t;

#define BOOPIE_EXPR_CORE_COUNT 8

/* A set of expressions, bit (1 << expr) each: what a style can draw. */
typedef uint32_t boopie_expr_set_t;

#define BOOPIE_EXPR_BIT(e) ((boopie_expr_set_t)1u << (e))
#define BOOPIE_EXPR_SET_CORE ((boopie_expr_set_t)((1u << BOOPIE_EXPR_CORE_COUNT) - 1u))
#define BOOPIE_EXPR_SET_ALL ((boopie_expr_set_t)((1u << BOOPIE_EXPR_COUNT) - 1u))

/* True for a value in range. */
bool boopie_expr_valid(int expr);

/* True for the eight every style draws. */
bool boopie_expr_is_core(boopie_expr_t expr);

/* Its id, as in the design doc and commands ("idle", "low_battery"), or NULL. */
const char *boopie_expr_name(boopie_expr_t expr);

/* The expression with that id; false (and *out untouched) if there's none. */
bool boopie_expr_from_name(const char *name, boopie_expr_t *out);

/* The next one down its fallback chain; a core expression returns itself. */
boopie_expr_t boopie_expr_fallback(boopie_expr_t expr);

/*
 * What a style that can draw `supported` shows for `expr`: `expr` itself if
 * it's in the set, else the first one down its chain that is. A core
 * expression is always returned as itself, set or not, since every style must
 * draw it. Out-of-range values show IDLE.
 */
boopie_expr_t boopie_expr_resolve(boopie_expr_t expr, boopie_expr_set_t supported);
