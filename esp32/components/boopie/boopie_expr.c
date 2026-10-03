/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_expr.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *name;
    boopie_expr_t fallback;   /* itself for a core expression */
} expr_info_t;

/* The table in docs/design.html, "表情规范". */
static const expr_info_t s_info[BOOPIE_EXPR_COUNT] = {
    [BOOPIE_EXPR_BOOT]        = { "boot",        BOOPIE_EXPR_BOOT },
    [BOOPIE_EXPR_IDLE]        = { "idle",        BOOPIE_EXPR_IDLE },
    [BOOPIE_EXPR_LISTENING]   = { "listening",   BOOPIE_EXPR_LISTENING },
    [BOOPIE_EXPR_THINKING]    = { "thinking",    BOOPIE_EXPR_THINKING },
    [BOOPIE_EXPR_SPEAKING]    = { "speaking",    BOOPIE_EXPR_SPEAKING },
    [BOOPIE_EXPR_ERROR]       = { "error",       BOOPIE_EXPR_ERROR },
    [BOOPIE_EXPR_OFF]         = { "off",         BOOPIE_EXPR_OFF },
    [BOOPIE_EXPR_HAPPY]       = { "happy",       BOOPIE_EXPR_HAPPY },
    [BOOPIE_EXPR_HUNGRY]      = { "hungry",      BOOPIE_EXPR_IDLE },
    [BOOPIE_EXPR_EATING]      = { "eating",      BOOPIE_EXPR_HAPPY },
    [BOOPIE_EXPR_SLEEPY]      = { "sleepy",      BOOPIE_EXPR_OFF },
    [BOOPIE_EXPR_SAD]         = { "sad",         BOOPIE_EXPR_IDLE },
    [BOOPIE_EXPR_SURPRISED]   = { "surprised",   BOOPIE_EXPR_HAPPY },
    [BOOPIE_EXPR_DIZZY]       = { "dizzy",       BOOPIE_EXPR_ERROR },
    [BOOPIE_EXPR_CELEBRATE]   = { "celebrate",   BOOPIE_EXPR_HAPPY },
    [BOOPIE_EXPR_SHY]         = { "shy",         BOOPIE_EXPR_HAPPY },
    [BOOPIE_EXPR_LOW_BATTERY] = { "low_battery", BOOPIE_EXPR_SLEEPY },
    [BOOPIE_EXPR_CHARGING]    = { "charging",    BOOPIE_EXPR_IDLE },
};

_Static_assert(BOOPIE_EXPR_COUNT <= 32, "boopie_expr_set_t holds one bit per expression");

bool boopie_expr_valid(int expr)
{
    return expr >= 0 && expr < BOOPIE_EXPR_COUNT;
}

bool boopie_expr_is_core(boopie_expr_t expr)
{
    return boopie_expr_valid(expr) && expr < BOOPIE_EXPR_CORE_COUNT;
}

const char *boopie_expr_name(boopie_expr_t expr)
{
    return boopie_expr_valid(expr) ? s_info[expr].name : NULL;
}

bool boopie_expr_from_name(const char *name, boopie_expr_t *out)
{
    if (!name || !out) {
        return false;
    }
    for (int i = 0; i < BOOPIE_EXPR_COUNT; i++) {
        if (strcmp(s_info[i].name, name) == 0) {
            *out = (boopie_expr_t)i;
            return true;
        }
    }
    return false;
}

boopie_expr_t boopie_expr_fallback(boopie_expr_t expr)
{
    return boopie_expr_valid(expr) ? s_info[expr].fallback : BOOPIE_EXPR_IDLE;
}

boopie_expr_t boopie_expr_resolve(boopie_expr_t expr, boopie_expr_set_t supported)
{
    if (!boopie_expr_valid(expr)) {
        return BOOPIE_EXPR_IDLE;
    }
    /* Each step moves toward a core expression, so this ends within
     * BOOPIE_EXPR_COUNT steps; the bound only guards a bad table edit. */
    for (int steps = 0; steps < BOOPIE_EXPR_COUNT; steps++) {
        if (boopie_expr_is_core(expr) || (supported & BOOPIE_EXPR_BIT(expr))) {
            return expr;
        }
        expr = s_info[expr].fallback;
    }
    return BOOPIE_EXPR_IDLE;
}
