/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The pet's world through the year (docs/boopie-world.md "天气和节日"): each
 * day's weather, by the season (the same all day, different day to day), and
 * the festivals, by the date. Plain C.
 */

typedef enum {
    BOOPIE_WEATHER_SUNNY = 0,
    BOOPIE_WEATHER_CLOUDY,
    BOOPIE_WEATHER_RAIN,
    BOOPIE_WEATHER_SNOW,
    BOOPIE_WEATHER_COUNT,
} boopie_weather_t;

/* The weather for a day (days since 1970) in a month (1 to 12): snow only in
 * winter, rain most in spring and summer, otherwise mostly fair. */
boopie_weather_t boopie_weather_on(int32_t day, int month);
const char *boopie_weather_key(boopie_weather_t w);   /* "sunny" ... */
bool boopie_weather_from_key(const char *key, boopie_weather_t *out);

typedef enum {
    BOOPIE_FEST_NONE = 0,
    BOOPIE_FEST_SPRING,       /* 春节: its eve to the seventh day (2026 to 2035) */
    BOOPIE_FEST_MOON,         /* 中秋: the day before to the day after (2026 to 2035) */
    BOOPIE_FEST_HALLOWEEN,    /* 万圣节: 28 to 31 October */
    BOOPIE_FEST_XMAS,         /* 圣诞: 20 to 26 December */
    BOOPIE_FEST_NEW_YEAR,     /* 元旦: 31 December, 1 January */
    BOOPIE_FEST_VALENTINE,    /* 情人节: 14 February */
    BOOPIE_FEST_DRAGON,       /* 端午: the day (2026 to 2035) */
    BOOPIE_FEST_CHILDREN,     /* 儿童节: 1 June */
    BOOPIE_FEST_BIRTHDAY,     /* the pet's own: never by the date here, the screen knows it */
    BOOPIE_FEST_COUNT,
} boopie_fest_t;

boopie_fest_t boopie_fest_on(int year, int month, int mday);
const char *boopie_fest_name(boopie_fest_t f);   /* "春节" ... */
