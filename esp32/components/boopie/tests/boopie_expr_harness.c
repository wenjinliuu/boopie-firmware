/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Dumps the expression table for test_boopie_expr.py as JSON:
 * every expression's id, core flag and fallback, and what it resolves to for
 * a style with only the core set, every expression, and each single extended
 * expression. Also checks the Muse mode order at compile time. */

#include <stdio.h>

#include "boopie_expr.h"
#include "muse_state.h"

#define SAME(e, m) _Static_assert((int)(e) == (int)(m), #e " matches " #m)
SAME(BOOPIE_EXPR_BOOT, MUSE_MODE_BOOT);
SAME(BOOPIE_EXPR_IDLE, MUSE_MODE_IDLE);
SAME(BOOPIE_EXPR_LISTENING, MUSE_MODE_LISTENING);
SAME(BOOPIE_EXPR_THINKING, MUSE_MODE_THINKING);
SAME(BOOPIE_EXPR_SPEAKING, MUSE_MODE_SPEAKING);
SAME(BOOPIE_EXPR_ERROR, MUSE_MODE_ERROR);
SAME(BOOPIE_EXPR_OFF, MUSE_MODE_OFF);
SAME(BOOPIE_EXPR_HAPPY, MUSE_MODE_COUNT);

static const char *name_or_null(boopie_expr_t e)
{
    const char *n = boopie_expr_name(e);
    return n ? n : "(null)";
}

int main(void)
{
    printf("{\"count\":%d,\"core_count\":%d,\"exprs\":[", BOOPIE_EXPR_COUNT, BOOPIE_EXPR_CORE_COUNT);
    for (int i = 0; i < BOOPIE_EXPR_COUNT; i++) {
        boopie_expr_t e = (boopie_expr_t)i;
        boopie_expr_t back = BOOPIE_EXPR_COUNT;
        bool found = boopie_expr_from_name(boopie_expr_name(e), &back);
        printf("%s{\"id\":%d,\"name\":\"%s\",\"core\":%s,\"fallback\":\"%s\","
               "\"round_trip\":%s,\"core_only\":\"%s\",\"all\":\"%s\"}",
               i ? "," : "", i, name_or_null(e), boopie_expr_is_core(e) ? "true" : "false",
               name_or_null(boopie_expr_fallback(e)),
               found && back == e ? "true" : "false",
               name_or_null(boopie_expr_resolve(e, BOOPIE_EXPR_SET_CORE)),
               name_or_null(boopie_expr_resolve(e, BOOPIE_EXPR_SET_ALL)));
    }
    /* sleepy drawn, low_battery not: low_battery -> sleepy. */
    printf("],\"low_battery_with_sleepy\":\"%s\"",
           name_or_null(boopie_expr_resolve(BOOPIE_EXPR_LOW_BATTERY,
                                            BOOPIE_EXPR_SET_CORE | BOOPIE_EXPR_BIT(BOOPIE_EXPR_SLEEPY))));
    /* An empty set still draws core expressions as themselves. */
    printf(",\"thinking_empty_set\":\"%s\"", name_or_null(boopie_expr_resolve(BOOPIE_EXPR_THINKING, 0)));
    boopie_expr_t untouched = BOOPIE_EXPR_SHY;
    bool unknown = boopie_expr_from_name("grumpy", &untouched);
    bool null_name = boopie_expr_from_name(NULL, &untouched);
    printf(",\"unknown_found\":%s,\"null_found\":%s,\"unknown_untouched\":%s",
           unknown ? "true" : "false", null_name ? "true" : "false",
           untouched == BOOPIE_EXPR_SHY ? "true" : "false");
    printf(",\"out_of_range\":{\"valid_neg\":%s,\"valid_count\":%s,\"name\":%s,\"resolve\":\"%s\",\"fallback\":\"%s\"}}\n",
           boopie_expr_valid(-1) ? "true" : "false",
           boopie_expr_valid(BOOPIE_EXPR_COUNT) ? "true" : "false",
           boopie_expr_name(BOOPIE_EXPR_COUNT) ? "\"set\"" : "null",
           name_or_null(boopie_expr_resolve((boopie_expr_t)99, BOOPIE_EXPR_SET_ALL)),
           name_or_null(boopie_expr_fallback((boopie_expr_t)-1)));
    return 0;
}
