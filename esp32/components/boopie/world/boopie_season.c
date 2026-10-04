/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_season.h"

#include <string.h>

static const char *const KEYS[BOOPIE_WEATHER_COUNT] = { "sunny", "cloudy", "rain", "snow" };

boopie_weather_t boopie_weather_on(int32_t day, int month)
{
    uint32_t h = (uint32_t)day * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    int roll = (int)(h % 100);
    bool winter = month == 12 || month <= 2;
    int rain = winter ? 8 : month <= 5 ? 28 : month <= 8 ? 24 : 15;
    int snow = winter ? 25 : 0;
    int cloudy = 18;
    if (roll < snow) {
        return BOOPIE_WEATHER_SNOW;
    }
    if (roll < snow + rain) {
        return BOOPIE_WEATHER_RAIN;
    }
    if (roll < snow + rain + cloudy) {
        return BOOPIE_WEATHER_CLOUDY;
    }
    return BOOPIE_WEATHER_SUNNY;
}

const char *boopie_weather_key(boopie_weather_t w)
{
    return (int)w >= 0 && w < BOOPIE_WEATHER_COUNT ? KEYS[w] : "";
}

bool boopie_weather_from_key(const char *key, boopie_weather_t *out)
{
    for (int i = 0; key && i < BOOPIE_WEATHER_COUNT; i++) {
        if (!strcmp(key, KEYS[i])) {
            *out = (boopie_weather_t)i;
            return true;
        }
    }
    return false;
}

/* The lunar festivals' dates, 2026 to 2035: 春节 (the first of the first month), 中秋 and 端午. */
static const struct {
    uint8_t spring_m, spring_d, moon_m, moon_d, dragon_m, dragon_d;
} LUNAR[] = {
    { 2, 17, 9, 25, 6, 19 }, { 2, 6, 9, 15, 6, 9 },   { 1, 26, 10, 3, 5, 28 }, { 2, 13, 9, 22, 6, 16 },
    { 2, 3, 9, 12, 6, 5 },   { 1, 23, 10, 1, 6, 24 }, { 2, 11, 9, 19, 6, 12 }, { 1, 31, 9, 8, 6, 1 },
    { 2, 19, 9, 27, 6, 20 }, { 2, 8, 9, 16, 6, 10 },
};
#define LUNAR_FROM 2026

/* Days since 1 January of the year, for comparing dates in it. */
static int yday(int year, int month, int mday)
{
    static const int BEFORE[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return BEFORE[month - 1] + mday - 1 + (leap && month > 2);
}

boopie_fest_t boopie_fest_on(int year, int month, int mday)
{
    if (month < 1 || month > 12 || mday < 1 || mday > 31) {
        return BOOPIE_FEST_NONE;
    }
    if ((month == 12 && mday == 31) || (month == 1 && mday == 1)) {
        return BOOPIE_FEST_NEW_YEAR;
    }
    if (month == 12 && mday >= 20 && mday <= 26) {
        return BOOPIE_FEST_XMAS;
    }
    if (month == 10 && mday >= 28) {
        return BOOPIE_FEST_HALLOWEEN;
    }
    int k = year - LUNAR_FROM;
    if (k >= 0 && k < (int)(sizeof LUNAR / sizeof LUNAR[0])) {
        int d = yday(year, month, mday);
        int spring = yday(year, LUNAR[k].spring_m, LUNAR[k].spring_d);
        int moon = yday(year, LUNAR[k].moon_m, LUNAR[k].moon_d);
        if (d >= spring - 1 && d <= spring + 6) {
            return BOOPIE_FEST_SPRING;
        }
        if (d == spring + 14) {
            return BOOPIE_FEST_LANTERN;
        }
        if (d >= moon - 1 && d <= moon + 1) {
            return BOOPIE_FEST_MOON;
        }
        if (month == LUNAR[k].dragon_m && mday == LUNAR[k].dragon_d) {
            return BOOPIE_FEST_DRAGON;   /* before 儿童节, should they fall together */
        }
    }
    if (month == 2 && mday == 14) {
        return BOOPIE_FEST_VALENTINE;
    }
    if (month == 6 && mday == 1) {
        return BOOPIE_FEST_CHILDREN;
    }
    return BOOPIE_FEST_NONE;
}

const char *boopie_fest_name(boopie_fest_t f)
{
    static const char *const NAMES[BOOPIE_FEST_COUNT] = { "", "春节", "中秋", "万圣节", "圣诞", "元旦",
                                                          "情人节", "端午", "儿童节", "生日", "元宵" };
    return (int)f >= 0 && f < BOOPIE_FEST_COUNT ? NAMES[f] : "";
}
