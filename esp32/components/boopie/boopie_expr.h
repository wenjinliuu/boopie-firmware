/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Boopie's expression list, shared by every avatar character (Muse, Boopie,
 * GPT, Codex, ...). The system only ever names an expression; the
 * active style draws it. A style that hasn't drawn an extended expression yet
 * falls back along a fixed chain until it reaches one it has, and every chain
 * ends at a core expression, which every character must draw.
 *
 * THINKING has a second phase every character draws: after a few seconds
 * of thinking it pulls out a tiny laptop and types ("working"). That's the
 * renderer's business, from how long the mode has lasted, not an expression.
 *
 * Values are stable from here on: only ever append. The first seven core expressions are in
 * the same order as muse_mode_t (MUSE_MODE_BOOT ... MUSE_MODE_OFF), so a Muse
 * mode converts with a cast; HAPPY is Muse's pet reaction.
 */
typedef enum {
    /* Core: every character draws these. */
    BOOPIE_EXPR_BOOT = 0,
    BOOPIE_EXPR_IDLE,
    BOOPIE_EXPR_LISTENING,
    BOOPIE_EXPR_THINKING,
    BOOPIE_EXPR_SPEAKING,
    BOOPIE_EXPR_ERROR,
    BOOPIE_EXPR_OFF,
    BOOPIE_EXPR_HAPPY,

    /* Pet: for the pet and games, each with a fallback. */
    BOOPIE_EXPR_HUNGRY,
    BOOPIE_EXPR_EATING,
    BOOPIE_EXPR_SLEEPY,
    BOOPIE_EXPR_SAD,
    BOOPIE_EXPR_DIZZY,

    BOOPIE_EXPR_COUNT,
} boopie_expr_t;

#define BOOPIE_EXPR_CORE_COUNT 8

/* A set of expressions, bit (1 << expr) each: what a character can draw. */
typedef uint32_t boopie_expr_set_t;

#define BOOPIE_EXPR_BIT(e) ((boopie_expr_set_t)1u << (e))
#define BOOPIE_EXPR_SET_CORE ((boopie_expr_set_t)((1u << BOOPIE_EXPR_CORE_COUNT) - 1u))
#define BOOPIE_EXPR_SET_ALL ((boopie_expr_set_t)((1u << BOOPIE_EXPR_COUNT) - 1u))

/* True for a value in range. */
bool boopie_expr_valid(int expr);

/* True for the eight every character draws. */
bool boopie_expr_is_core(boopie_expr_t expr);

/* Its id, as in the design doc and commands ("idle", "hungry"), or NULL. */
const char *boopie_expr_name(boopie_expr_t expr);

/* The expression with that id; false (and *out untouched) if there's none. */
bool boopie_expr_from_name(const char *name, boopie_expr_t *out);

/* The next one down its fallback chain; a core expression returns itself. */
boopie_expr_t boopie_expr_fallback(boopie_expr_t expr);

/*
 * What a character that can draw `supported` shows for `expr`: `expr` itself if
 * it's in the set, else the first one down its chain that is. A core
 * expression is always returned as itself, set or not, since every character
 * must draw it. Out-of-range values show IDLE.
 */
boopie_expr_t boopie_expr_resolve(boopie_expr_t expr, boopie_expr_set_t supported);

/*
 * Overlays: effects drawn on top of whatever the expression is, any number at
 * once. Reactions are short (the caller clears them); device ones last as long
 * as the state does. Every character draws every overlay, from shared code.
 */
typedef enum {
    /* Reactions. */
    BOOPIE_OVERLAY_SURPRISE = 0,  /* a "!" over the head, eyes wide */
    BOOPIE_OVERLAY_BLUSH,         /* big blush (shy) */
    BOOPIE_OVERLAY_CONFETTI,      /* confetti falling (celebrate) */
    BOOPIE_OVERLAY_HEARTS,        /* hearts floating up */

    /* Device. */
    BOOPIE_OVERLAY_LOW_BATTERY,   /* an empty battery, blinking red */
    BOOPIE_OVERLAY_CHARGING,      /* a bolt, sparks rising */

    BOOPIE_OVERLAY_COUNT,
} boopie_overlay_t;

/* A set of overlays, bit (1 << overlay) each. */
typedef uint8_t boopie_overlay_set_t;

#define BOOPIE_OVERLAY_BIT(o) ((boopie_overlay_set_t)(1u << (o)))
#define BOOPIE_OVERLAY_SET_ALL ((boopie_overlay_set_t)((1u << BOOPIE_OVERLAY_COUNT) - 1u))

/* Its id ("hearts", "low_battery"), or NULL out of range. */
const char *boopie_overlay_name(boopie_overlay_t overlay);

/* The overlay with that id; false (and *out untouched) if there's none. */
bool boopie_overlay_from_name(const char *name, boopie_overlay_t *out);
