/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_text.h"

/* East Asian Wide and Fullwidth ranges that matter for Chinese text.
 * tools/boopie/gen_pixel_font.py reads this table, so the font and the
 * layout always agree: keep one range per line. */
static const struct {
    uint32_t lo, hi;
} WIDE[] = {
    /* BEGIN WIDE */
    { 0x1100, 0x115F },   /* Hangul Jamo */
    { 0x2014, 0x2016 },   /* —, ―, ‖: Chinese dashes */
    { 0x203B, 0x203B },   /* ※ */
    { 0x2103, 0x2103 },   /* ℃ */
    { 0x2116, 0x2116 },   /* № */
    { 0x2160, 0x216B },   /* Roman numerals */
    { 0x2460, 0x249B },   /* circled, parenthesised and dotted numbers */
    { 0x25A0, 0x25FF },   /* geometric shapes: ■□▲△◆◇○● */
    { 0x2605, 0x2606 },   /* ★☆ */
    { 0x2E80, 0x303E },   /* CJK radicals, symbols and punctuation */
    { 0x3041, 0x33FF },   /* kana, Bopomofo, enclosed CJK, CJK compatibility */
    { 0x3400, 0x4DBF },   /* CJK Extension A */
    { 0x4E00, 0x9FFF },   /* CJK Unified Ideographs */
    { 0xA000, 0xA4CF },   /* Yi */
    { 0xAC00, 0xD7A3 },   /* Hangul syllables */
    { 0xF900, 0xFAFF },   /* CJK compatibility ideographs */
    { 0xFE10, 0xFE19 },   /* vertical forms */
    { 0xFE30, 0xFE6F },   /* CJK compatibility and small forms */
    { 0xFF00, 0xFF60 },   /* full-width ASCII */
    { 0xFFE0, 0xFFE6 },   /* full-width signs */
    { 0x20000, 0x3FFFD }, /* CJK Extensions B and later */
    /* END WIDE */
};

/* Closing marks, sorted. */
static const uint16_t NO_START[] = {
    0x0021, 0x0029, 0x002C, 0x002E, 0x003A, 0x003B, 0x003F, 0x005D, 0x007D,
    0x2019, 0x201D, 0x2026,
    0x3001, 0x3002, 0x3009, 0x300B, 0x300D, 0x300F, 0x3011, 0x3015, 0x3017,
    0xFF01, 0xFF09, 0xFF0C, 0xFF0E, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF3D, 0xFF5D,
};

/* Opening marks, sorted. */
static const uint16_t NO_END[] = {
    0x0028, 0x005B, 0x007B,
    0x2018, 0x201C,
    0x3008, 0x300A, 0x300C, 0x300E, 0x3010, 0x3014, 0x3016,
    0xFF08, 0xFF3B, 0xFF5B,
};

uint32_t boopie_text_decode(const char *s, size_t *len)
{
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) {
        *len = u[0] ? 1 : 0;
        return u[0];
    }
    size_t n = u[0] >= 0xF0 ? 4 : u[0] >= 0xE0 ? 3 : u[0] >= 0xC0 ? 2 : 0;
    if (n == 0 || u[0] >= 0xF8) {
        *len = 1;   /* a stray continuation byte, or not UTF-8 */
        return 0xFFFD;
    }
    uint32_t cp = u[0] & (0x7F >> n);
    for (size_t i = 1; i < n; i++) {
        if ((u[i] & 0xC0) != 0x80) {
            *len = i;
            return 0xFFFD;
        }
        cp = cp << 6 | (u[i] & 0x3F);
    }
    *len = n;
    return cp;
}

int boopie_text_cols(uint32_t cp)
{
    if (cp < WIDE[0].lo) {
        return 1;
    }
    for (size_t i = 0; i < sizeof(WIDE) / sizeof(WIDE[0]); i++) {
        if (cp >= WIDE[i].lo && cp <= WIDE[i].hi) {
            return 2;
        }
    }
    return 1;
}

int boopie_text_line_cols(const char *s)
{
    int cols = 0;
    while (*s && *s != '\n') {
        size_t len;
        cols += boopie_text_cols(boopie_text_decode(s, &len));
        s += len;
    }
    return cols;
}

static bool in_sorted(const uint16_t *set, size_t n, uint32_t cp)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (set[mid] == cp) {
            return true;
        }
        if (set[mid] < cp) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return false;
}

bool boopie_text_no_line_start(uint32_t cp)
{
    return in_sorted(NO_START, sizeof(NO_START) / sizeof(NO_START[0]), cp);
}

bool boopie_text_no_line_end(uint32_t cp)
{
    return in_sorted(NO_END, sizeof(NO_END) / sizeof(NO_END[0]), cp);
}

bool boopie_text_can_break(uint32_t before, uint32_t after)
{
    if (boopie_text_cols(before) < 2 && boopie_text_cols(after) < 2) {
        return false;   /* inside a Latin word or number */
    }
    return !boopie_text_no_line_start(after) && !boopie_text_no_line_end(before);
}
