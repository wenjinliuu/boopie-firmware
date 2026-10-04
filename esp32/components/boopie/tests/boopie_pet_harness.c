/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Runs the pet (pet/boopie_pet.c) through made-up days and prints what
 * happened as JSON, for test_boopie_pet.py. Time starts at day 20000,
 * 00:00; a tick is a minute. */

#include <stdio.h>
#include <string.h>

#include "boopie_pet.h"

#define DAY0 20000
#define T(day, h, m) ((int64_t)(DAY0 + (day)) * 86400 + (h) * 3600 + (m) * 60)

static boopie_expr_t tick_at(boopie_pet_t *p, int64_t now, float idle)
{
    int64_t local = now;   /* the tests run in UTC */
    return boopie_pet_tick(p, true, now, (int32_t)(local / 86400), (int)(local % 86400) / 60, idle, NULL);
}

static void hunger_times(const char *name, bool feed, int delay_min)
{
    boopie_pet_t p;
    boopie_pet_init(&p);
    printf("\"%s\":[", name);
    bool first = true;
    boopie_expr_t last = BOOPIE_EXPR_IDLE;
    int64_t hungry_at = 0;
    for (int64_t now = T(0, 7, 0); now < T(1, 0, 0); now += 60) {
        boopie_expr_t e = tick_at(&p, now, 0);
        if (e != last) {
            printf("%s[\"%s\",%d]", first ? "" : ",", boopie_expr_name(e), (int)((now - T(0, 0, 0)) / 60));
            first = false;
            last = e;
            if (e == BOOPIE_EXPR_HUNGRY) {
                hungry_at = now;
            }
        }
        if (feed && p.hungry && now - hungry_at >= delay_min * 60) {
            boopie_pet_tap(&p, now, NULL);
        }
    }
    printf("],\"%s_xp\":%u", name, p.xp);
}

int main(void)
{
    printf("{");
    hunger_times("fed", true, 10);
    printf(",");
    hunger_times("ignored", false, 0);
    printf(",");
    hunger_times("late", true, 90);

    /* Night: sleepy only when left alone. */
    boopie_pet_t p;
    boopie_pet_init(&p);
    printf(",\"night_alone\":\"%s\"", boopie_expr_name(tick_at(&p, T(0, 23, 30), 400)));
    printf(",\"night_busy\":\"%s\"", boopie_expr_name(tick_at(&p, T(0, 23, 31), 10)));
    printf(",\"early_alone\":\"%s\"", boopie_expr_name(tick_at(&p, T(1, 6, 0), 400)));
    printf(",\"day_alone\":\"%s\"", boopie_expr_name(tick_at(&p, T(1, 9, 0), 4000)));

    /* Powered off for 5 h while hungry: not sad on waking. */
    boopie_pet_init(&p);
    for (int64_t now = T(0, 8, 0); now <= T(0, 12, 0); now += 60) {
        tick_at(&p, now, 0);
    }
    int hungry_before = p.hungry;
    boopie_pet_resume(&p, T(0, 17, 0));
    printf(",\"off_hungry\":%d,\"off_after\":\"%s\"", hungry_before, boopie_expr_name(tick_at(&p, T(0, 17, 0), 0)));

    /* No clock: nothing happens. */
    boopie_pet_init(&p);
    boopie_expr_t e = boopie_pet_tick(&p, false, 0, 0, 0, 0, NULL);
    printf(",\"unknown\":[\"%s\",%u]", boopie_expr_name(e), p.xp);

    /* Caps: 30 pokes in a day, 10 replies. */
    boopie_pet_init(&p);
    tick_at(&p, T(0, 9, 0), 0);
    uint32_t xp0 = p.xp;
    for (int i = 0; i < 30; i++) {
        boopie_pet_tap(&p, T(0, 9, 1), NULL);
    }
    uint32_t poke = p.xp - xp0;
    for (int i = 0; i < 10; i++) {
        boopie_pet_talked(&p, NULL);
    }
    printf(",\"poke_xp\":%u,\"talk_xp\":%u,\"meet\":[%u,%u]", poke, p.xp - xp0 - poke, xp0, p.stars);

    /* Levels. */
    printf(",\"levels\":[");
    const uint32_t xs[] = { 0, 109, 110, 620, 2070, 7220, 7840 };
    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        printf("%s%d", i ? "," : "", boopie_pet_level(xs[i], NULL, NULL));
    }
    printf("],\"need\":[%u,%u,%u,%u]", boopie_pet_need(1), boopie_pet_need(9), boopie_pet_need(18), boopie_pet_need(40));

    /* A month as most people would: two meals on time, five replies, ten pokes. */
    boopie_pet_init(&p);
    for (int d = 0; d < 30; d++) {
        for (int64_t now = T(d, 7, 0); now < T(d, 23, 0); now += 60) {
            tick_at(&p, now, 0);
            if (p.hungry) {
                boopie_pet_tap(&p, now + 300, NULL);
            }
            if (now == T(d, 19, 0)) {
                for (int i = 0; i < 5; i++) {
                    boopie_pet_talked(&p, NULL);
                }
                for (int i = 0; i < 10; i++) {
                    boopie_pet_tap(&p, now, NULL);
                }
            }
        }
    }
    printf(",\"month\":{\"xp\":%u,\"level\":%d,\"stars\":%u}", p.xp, boopie_pet_level(p.xp, NULL, NULL), p.stars);
    /* Games: 5 rounds of 15 xp and 3 stars: whole to 45 xp and 10 stars, then half. */
    boopie_pet_init(&p);
    tick_at(&p, T(0, 9, 0), 0);
    uint32_t gx0 = p.xp, st0 = p.stars;
    boopie_pet_event_t ev;
    int tired[8];
    for (int i = 0; i < 8; i++) {
        ev = (boopie_pet_event_t){ 0 };
        boopie_pet_game(&p, 15, 3, &ev);
        tired[i] = ev.tired;
    }
    printf(",\"game\":[%u,%u", p.xp - gx0, p.stars - st0);
    printf(",[%d,%d,%d,%d,%d,%d,%d,%d]", tired[0], tired[1], tired[2], tired[3], tired[4], tired[5], tired[6], tired[7]);
    /* 小窝 has its own day: games done, it still gives. */
    gx0 = p.xp;
    st0 = p.stars;
    for (int i = 0; i < 20; i++) {
        boopie_pet_world(&p, 20, 4, NULL);
    }
    printf(",%u,%u", p.xp - gx0, p.world_stars_today);   /* (stars: levels gained add theirs) */
    (void)st0;
    tick_at(&p, T(1, 9, 0), 0);   /* the next day: room again */
    gx0 = p.xp;
    boopie_pet_game(&p, 15, 3, NULL);
    printf(",%u,%u,%u]", p.xp - gx0, p.game_stars_today, p.world_stars_today);

    /* Old saves load: version 1 (padding where the game stars went), version 2. */
    boopie_pet_t v = p, loaded;
    uint8_t raw[sizeof v];
    memcpy(raw, &v, sizeof v);
    raw[0] = 1;
    raw[50] = 0xAA;   /* padding then: anything */
    bool ok1 = boopie_pet_load(&loaded, raw, sizeof raw);
    printf(",\"load_v1\":[%d,%d,%d,%u]", ok1, loaded.version, loaded.game_stars_today, loaded.xp == p.xp);
    raw[0] = 2;
    raw[50] = 7;      /* game_stars_today then */
    raw[51] = 0x55;   /* padding then */
    bool ok2 = boopie_pet_load(&loaded, raw, sizeof raw);
    printf(",\"load_v2\":[%d,%d,%d,%d,%d]", ok2, loaded.version, loaded.game_stars_today,
           loaded.xp_today[BOOPIE_XP_WORLD], loaded.world_stars_today);
    raw[0] = 9;
    printf(",\"load_bad\":[%d,%d]", boopie_pet_load(&loaded, raw, sizeof raw), boopie_pet_load(&loaded, raw, 10));

    printf(",\"unlocks\":[%d,%d,%d,%d,%d]}\n", boopie_pet_unlock_level(BOOPIE_UNLOCK_COLOUR, 2),
           boopie_pet_unlock_level(BOOPIE_UNLOCK_SCENE, 1), boopie_pet_unlock_level(BOOPIE_UNLOCK_SCENE, 8),
           boopie_pet_unlock_level(BOOPIE_UNLOCK_ACCESSORY, 3), boopie_pet_unlock_level(BOOPIE_UNLOCK_COLOUR, 99));
    return 0;
}
