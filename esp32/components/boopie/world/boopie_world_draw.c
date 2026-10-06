/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_world_draw.h"

#include <math.h>
#include <string.h>

#include "boopie_garden.h"

#define N BOOPIE_WORLD_W

#ifdef BOOPIE_DATA_IN_ASSETS
/*
 * The backgrounds stay in the assets partition (world/bg_*.px): the room's is
 * read into one buffer, the size of the largest, when the room changes. If it
 * can't be read the room is drawn on black.
 */
#include "boopie_assets.h"
#include "esp_heap_caps.h"

static const uint8_t *bg_px(boopie_art_id_t id)
{
    static const char *const names[] = {
        [BOOPIE_ART_BG_DOWN] = "world/bg_down.px",       [BOOPIE_ART_BG_DOWN_FANCY] = "world/bg_down_fancy.px",
        [BOOPIE_ART_BG_UP] = "world/bg_up.px",           [BOOPIE_ART_BG_UP_STARS] = "world/bg_up_stars.px",
        [BOOPIE_ART_BG_OUTSIDE] = "world/bg_outside.px", [BOOPIE_ART_BG_WOODS] = "world/bg_woods.px",
        [BOOPIE_ART_BG_BEACH] = "world/bg_beach.px",
    };
    static uint8_t *s_buf;
    static size_t s_cap;
    static int s_have = -1;
    const boopie_art_t *a = &boopie_art[id];
    size_t n = (size_t)a->w * a->h;
    if (a->px) {
        return a->px;
    }
    if (s_have == (int)id) {
        return s_buf;
    }
    if (n > s_cap) {
        size_t cap = 0;
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            size_t m = (size_t)boopie_art[i].w * boopie_art[i].h;
            cap = m > cap ? m : cap;
        }
        heap_caps_free(s_buf);
        s_buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_cap = s_buf ? cap : 0;
        if (!s_buf) {
            return NULL;
        }
    }
    uint32_t off, size;
    s_have = -1;
    if ((size_t)id >= sizeof(names) / sizeof(names[0]) || !names[id] || !boopie_assets_find(names[id], &off, &size)
        || size != n || !boopie_assets_read(off, s_buf, n)) {
        memset(s_buf, 0, n);
        return s_buf;
    }
    s_have = (int)id;
    return s_buf;
}
#else
static const uint8_t *bg_px(boopie_art_id_t id)
{
    return boopie_art[id].px;
}
#endif

static uint8_t *s_rgb;
static int s_cam;   /* the view's left edge in the room: what's drawn moves left by it */

static void put(int x, int y, uint32_t c)
{
    if (x >= 0 && x < N && y >= 0 && y < N) {
        uint8_t *p = s_rgb + (y * N + x) * 3;
        p[0] = (uint8_t)(c >> 16);
        p[1] = (uint8_t)(c >> 8);
        p[2] = (uint8_t)c;
    }
}

static void darken(int x, int y, float k)
{
    if (x >= 0 && x < N && y >= 0 && y < N) {
        uint8_t *p = s_rgb + (y * N + x) * 3;
        p[0] = (uint8_t)(p[0] * k);
        p[1] = (uint8_t)(p[1] * (k + 0.02f));
        p[2] = (uint8_t)(p[2] * (k + 0.06f));
    }
}

/* A picture standing at (x, y) in the room; dry, washed toward straw. */
static void blit_tint(boopie_art_id_t id, int x, int y, bool dry)
{
    const boopie_art_t *a = &boopie_art[id];
    int x0 = x - s_cam - a->ax, y0 = y - a->ay;
    if (x0 >= N || x0 + a->w < 0) {
        return;
    }
    for (int j = 0; j < a->h; j++) {
        const uint8_t *row = a->px + j * a->w;
        for (int i = 0; i < a->w; i++) {
            if (row[i]) {
                uint32_t c = boopie_art_palette[row[i]];
                if (dry) {
                    c = ((((c >> 16) & 255) + 150) / 2) << 16 | ((((c >> 8) & 255) + 140) / 2) << 8 | (((c & 255) + 80) / 2);
                }
                put(x0 + i, y0 + j, c);
            }
        }
    }
}

static void blit(boopie_art_id_t id, int x, int y)
{
    blit_tint(id, x, y, false);
}

static void shadow(float cx, float cy, float rx, float ry)
{
    cx -= s_cam;
    for (int y = (int)(cy - ry); y <= (int)(cy + ry); y++) {
        for (int x = (int)(cx - rx); x <= (int)(cx + rx); x++) {
            float dx = (x - cx) / rx, dy = (y - cy) / ry;
            if (dx * dx + dy * dy <= 1) {
                darken(x, y, 0.8f);
            }
        }
    }
}

static void umbrella(int px, int top);

static void pet(const boopie_world_t *w, const boopie_world_look_t *look)
{
    if (!look->pet) {
        return;
    }
    bool at = w->state == BOOPIE_PET_ANTIC;
    bool walking = w->state == BOOPIE_PET_WALKING || (at && w->antic == BOOPIE_ANTIC_BUTTERFLY);
    bool asleep = w->state == BOOPIE_PET_SLEEPING;
    /* In the sea (swimming, or wading in and out): only its head above the water. */
    bool wet = w->antic == BOOPIE_ANTIC_SWIM && w->room == BOOPIE_ROOM_BEACH && w->y < 62;
    /* A hop as it walks; a slow breath standing; still, asleep. */
    int hop = walking ? (int)(fabsf(sinf(w->walked * 0.45f)) * 3) : asleep ? 0 : ((int)(look->t * 2) & 1);
    int dx = 0, lift = 0;
    if (at && w->antic == BOOPIE_ANTIC_DANCE) {
        hop = (int)(fabsf(sinf(look->t * 6)) * 4);   /* bouncing to it, side to side */
        dx = (int)(sinf(look->t * 3) * 2);
    } else if (at && w->antic == BOOPIE_ANTIC_SWING) {
        lift = 7;   /* on the seat, to and fro */
        dx = (int)(sinf(look->t * 2.5f) * 3);
        hop = 0;
    } else if (at && w->antic == BOOPIE_ANTIC_TV && w->y < 100 && w->x > 100) {
        lift = 4;   /* sat on the sofa */
        hop = 0;
    } else if (wet) {
        hop = (int)(sinf(look->t * 3) * 1.2f);   /* bobbing */
    }
    int px = (int)w->x - s_cam + dx, py = (int)w->y - lift;
    if (!wet && !lift) {
        shadow(px + s_cam, py, look->pet_w / 2.0f + 1, 2.2f);
    }
    int x0 = px - look->pet_w / 2, y0 = py - look->pet_h - 2 - hop;
    int water = wet ? py - 5 : 9999;   /* rows from here down are under */
    /* Two little feet under it, its body's colour darkened, stepping in turn as it walks. */
    uint16_t body = look->pet[(look->pet_h - 1) * look->pet_w + look->pet_w / 2];
    if (body && !asleep && !wet && !lift) {
        uint32_t c = ((uint32_t)((body >> 11) * 200 / 31) << 16) | ((uint32_t)(((body >> 5) & 63) * 200 / 63) << 8)
                     | (uint32_t)((body & 31) * 200 / 31);
        int step = walking ? ((int)(w->walked * 0.3f) & 1) : 0;
        for (int f = 0; f < 2; f++) {
            int fx = px + (f ? 2 : -4), fy = py - 2 + (f == step && walking ? -1 : 0);
            for (int i = 0; i < 3; i++) {
                put(fx + i, fy, c);
                put(fx + i, fy + 1, ((c >> 1) & 0x7f7f7f));
            }
        }
    }
    for (int j = 0; j < look->pet_h; j++) {
        for (int i = 0; i < look->pet_w; i++) {
            int si = w->facing < 0 ? look->pet_w - 1 - i : i;
            uint16_t c = look->pet[j * look->pet_w + si];
            if (c && y0 + j < water) {
                uint32_t rgb = ((uint32_t)((c >> 11) * 255 / 31) << 16) | ((uint32_t)(((c >> 5) & 63) * 255 / 63) << 8)
                               | (uint32_t)((c & 31) * 255 / 31);
                put(x0 + i, y0 + j, rgb);
            }
        }
    }
    if (look->weather == BOOPIE_WEATHER_RAIN && boopie_world_outdoors(w->room) && !wet && !asleep) {
        umbrella(px, y0 - 2);
    }
    if (wet) {
        /* Rings round it on the water. */
        float r = 7 + fmodf(look->t * 4, 4);
        for (int a = 0; a < 24; a++) {
            float ang = a * 6.2832f / 24;
            if ((a + (int)(look->t * 4)) % 3) {
                put(px + (int)(cosf(ang) * r), water + (int)(sinf(ang) * r * 0.3f), 0xe8f4fc);
            }
        }
    }
    if (asleep) {
        /* z's rising, one after another. */
        for (int k = 0; k < 2; k++) {
            float u = fmodf(look->t * 0.6f + k * 0.5f, 1.0f);
            int zx = px + 5 + (int)(u * 6), zy = y0 - 2 - (int)(u * 10);
            uint32_t c = 0x5868a8;
            for (int i = 0; i < 3; i++) {
                put(zx + i, zy, c);
                put(zx + i, zy + 2, c);
            }
            put(zx + 1, zy + 1, c);
        }
    } else if (w->state == BOOPIE_PET_USING) {
        put(px, y0 - 3, 0xfff0a0);
        put(px - 1, y0 - 2, 0xfff0a0);
        put(px + 1, y0 - 2, 0xfff0a0);
    }
}

/* A slime: squashed as it lands, takes off or is hit; its hits left and the
 * time left over it in a fight; a puff as it goes. */
static void slime(const boopie_world_t *w, int i, const boopie_world_look_t *look)
{
    const boopie_slime_t *s = &w->slimes[i];
    if (s->state == BOOPIE_SLIME_POOF) {
        int rise = (int)(s->t * 10);
        blit(BOOPIE_ART_PUFF, (int)s->x, (int)s->y - rise);
        return;
    }
    if (s->state != BOOPIE_SLIME_ROAM && s->state != BOOPIE_SLIME_FIGHT) {
        return;
    }
    bool squash = s->hit < 0.18f || (s->hop >= 0 && (s->hop < 0.12f || s->hop > 0.88f))
                  || (s->hop < 0 && s->rest < 0.12f);
    int art = BOOPIE_ART_SLIME_GREEN + s->kind * 2 + (squash ? 1 : 0);
    shadow(s->x, s->y + 1, 7 - s->z * 0.25f, 2.0f);
    blit((boopie_art_id_t)art, (int)s->x, (int)(s->y - s->z));
    if (s->kind == BOOPIE_SLIME_GOLD && ((int)(look->t * 6) & 1)) {
        put((int)s->x - s_cam + 6, (int)(s->y - s->z) - 10, 0xffffff);   /* a glint */
    }
    if (s->state == BOOPIE_SLIME_FIGHT) {
        /* Its hits left, a red pip each; under them the time, shrinking. */
        int top = (int)(s->y - s->z) - 16, x0 = (int)s->x - s_cam - (s->hp_max * 3 - 1) / 2;
        for (int k = 0; k < s->hp_max; k++) {
            uint32_t c = k < s->hp ? 0xf04858 : 0x707080;
            put(x0 + k * 3, top, c);
            put(x0 + k * 3 + 1, top, c);
            put(x0 + k * 3, top + 1, c);
            put(x0 + k * 3 + 1, top + 1, c);
        }
        int bw = 16, filled = (int)(bw * w->fight_left / BOOPIE_SLIME_FIGHT_S + 0.99f);
        int bx = (int)s->x - s_cam - bw / 2;
        for (int k = 0; k < bw; k++) {
            put(bx + k, top - 3, k < filled ? (w->fight_left < 4 ? 0xf8a040 : 0xf8e070) : 0x404050);
        }
    }
}

/* ---- weather and festivals ---- */

/* Rain or snow falling through a box of the screen (all of it outdoors, a window's glass in). */
static void falling(boopie_weather_t wx, float t, int x0, int y0, int x1, int y1, int count)
{
    for (int k = 0; k < count; k++) {
        float sx = fmodf(k * 37.3f + (wx == BOOPIE_WEATHER_SNOW ? sinf(t + k) * 4 : 0), (float)(x1 - x0));
        float speed = wx == BOOPIE_WEATHER_SNOW ? 10 + (k % 5) * 2 : 90 + (k % 4) * 15;
        float sy = fmodf(k * 53.1f + t * speed, (float)(y1 - y0));
        int x = x0 + (int)sx, y = y0 + (int)sy;
        if (wx == BOOPIE_WEATHER_SNOW) {
            put(x, y, 0xffffff);
            if (k % 3 == 0) {
                put(x + 1, y, 0xe8f0ff);
                put(x, y + 1, 0xe8f0ff);
            }
        } else {
            for (int j = 0; j < 3 && y + j < y1; j++) {
                put(x - j / 2, y + j, j ? 0xb8d0f0 : 0xe0ecff);   /* a slanting streak */
            }
        }
    }
}

static void weather(const boopie_world_t *w, const boopie_thing_t *t, int n, const boopie_world_look_t *look)
{
    if (look->weather != BOOPIE_WEATHER_RAIN && look->weather != BOOPIE_WEATHER_SNOW) {
        return;
    }
    if (boopie_world_outdoors(w->room)) {
        falling(look->weather, look->t, 0, 0, N, N, look->weather == BOOPIE_WEATHER_SNOW ? 70 : 90);
        return;
    }
    for (int i = 0; i < n; i++) {   /* through the windows */
        if (t[i].art == BOOPIE_ART_WINDOW_DAY && boopie_thing_shown(&t[i], look->level)) {
            const boopie_art_t *a = &boopie_art[BOOPIE_ART_WINDOW_DAY];
            int x0 = t[i].x - a->ax + 2, y0 = t[i].y - a->ay + 2;
            falling(look->weather, look->t, x0 - s_cam, y0, x0 + a->w - 4 - s_cam, y0 + a->h - 4, 8);
        }
    }
}

/* Its umbrella, out in the rain: a red dome over its head. */
static void umbrella(int px, int top)
{
    for (int i = -6; i <= 6; i++) {
        int h = 3 - (i * i) / 12;
        for (int j = 0; j <= h; j++) {
            put(px + i, top - j, (i + 6) / 3 % 2 ? 0xf06060 : 0xfff4f0);
        }
    }
    for (int j = 1; j <= 4; j++) {
        put(px + 3, top + j, 0x604838);
    }
}

/* Fireworks over the night: bursts of sparks, one after another, never dimmed. */
static void fireworks(float t)
{
    static const uint32_t COLOURS[] = { 0xff7070, 0xffe070, 0x80d0ff, 0xb0ff90, 0xff90e0 };
    for (int k = 0; k < 3; k++) {
        float phase = fmodf(t * 0.45f + k * 0.37f, 1.0f);
        int burst = (int)(t * 0.45f + k * 0.37f);
        int cx = 24 + (burst * 47 + k * 31) % 108, cy = 20 + (burst * 13 + k * 7) % 22;
        uint32_t c = COLOURS[(burst + k) % 5];
        if (phase < 0.25f) {
            put(cx, cy + (int)((0.25f - phase) * 120), 0xfff0c0);   /* going up */
            continue;
        }
        float r = (phase - 0.25f) * 34;
        for (int a = 0; a < 16; a++) {
            float ang = a * 6.2832f / 16;
            for (int ring = 0; ring < 2; ring++) {   /* two rings, the inner one paler */
                float rr = ring ? r * 0.55f : r;
                int x = cx + (int)(cosf(ang) * rr), y = cy + (int)(sinf(ang) * rr + (phase - 0.25f) * 8);
                uint32_t col = ring ? 0xfff4e0 : c;
                if (phase < 0.85f || (a & 1)) {
                    put(x, y, col);
                    if (!ring) {
                        put(x + 1, y, col);
                        put(x, y + 1, col);
                    }
                }
            }
        }
    }
}

/* ---- its little somethings: what goes with them ---- */

static void glyph5(int x, int y, const char *const rows[5], uint32_t c)
{
    /* A dark edge round it first, so it reads over anything. */
    for (int j = 0; j < 5; j++) {
        for (int i = 0; rows[j][i]; i++) {
            if (rows[j][i] == '#') {
                put(x + i - 1, y + j, 0x303848);
                put(x + i + 1, y + j, 0x303848);
                put(x + i, y + j - 1, 0x303848);
                put(x + i, y + j + 1, 0x303848);
            }
        }
    }
    for (int j = 0; j < 5; j++) {
        for (int i = 0; rows[j][i]; i++) {
            if (rows[j][i] == '#') {
                put(x + i, y + j, c);
            }
        }
    }
}

/* Over everything (not dimmed at night): hearts, notes or sparkles rising off it, a butterfly. */
static void antic_over(const boopie_world_t *w, const boopie_world_look_t *look)
{
    static const char *const HEART[5] = { ".#.#.", "#####", "#####", ".###.", "..#.." };
    static const char *const NOTE[5] = { "..##", "..#.", "..#.", "##..", "##.." };
    static const char *const SPARK[5] = { "..#..", "..#..", "#####", "..#..", "..#.." };
    int px = (int)w->x - s_cam, py = (int)w->y - 14;
    if (w->state == BOOPIE_PET_ANTIC) {
        const char *const *g = NULL;
        uint32_t c = 0;
        switch (w->antic) {
        case BOOPIE_ANTIC_LOVE: g = HEART; c = 0xf05878; break;
        case BOOPIE_ANTIC_DANCE: g = NOTE; c = 0xfff0a0; break;
        case BOOPIE_ANTIC_MIRROR: g = SPARK; c = 0xfff0a0; break;
        case BOOPIE_ANTIC_SWING: g = HEART; c = 0xf8a0c0; break;
        default: break;
        }
        for (int k = 0; g && k < 2; k++) {
            float u = fmodf(look->t * 0.7f + k * 0.5f, 1.0f);
            int gx = px + (k ? 4 : -8) + (int)(sinf(u * 6 + k) * 2), gy = py - 4 - (int)(u * 14);
            if (u < 0.85f) {
                glyph5(gx, gy, g, c);
            }
        }
    }
    if (w->antic == BOOPIE_ANTIC_BUTTERFLY || w->b_away > 0) {
        bool open = ((int)(look->t * 8)) & 1;
        uint32_t wing = w->room == BOOPIE_ROOM_WOODS ? 0x78b8f8 : 0xf8c848, edge = 0x384058;
        int bx = (int)w->bx - s_cam, by = (int)w->by;
        put(bx, by, edge);
        put(bx, by + 1, edge);
        put(bx, by + 2, edge);
        int span = open ? 3 : 1;
        for (int s = 1; s <= span; s++) {
            for (int j = -2; j <= 1; j++) {
                if (j == -2 && s == span) {
                    continue;   /* rounded */
                }
                put(bx - s, by + j, wing);
                put(bx + s, by + j, wing);
            }
        }
        put(bx - 1, by + 2, wing);
        put(bx + 1, by + 2, wing);
    }
}

/* The TV on while it watches: the screen flickering through a show's colours. */
static void tv_on(const boopie_world_t *w, const boopie_thing_t *t, int n, const boopie_world_look_t *look)
{
    if (w->state != BOOPIE_PET_ANTIC || w->antic != BOOPIE_ANTIC_TV) {
        return;
    }
    static const uint32_t SHOW[] = { 0x88d0f8, 0xf8c870, 0xa0e090, 0xf898b8 };
    uint32_t c = SHOW[(int)(look->t * 1.5f) & 3];
    for (int i = 0; i < n; i++) {
        if (!boopie_thing_shown(&t[i], look->level)) {
            continue;
        }
        int x0, y0, x1, y1;
        if (t[i].art == BOOPIE_ART_TV_OLD) {
            x0 = t[i].x - 7; y0 = t[i].y - 21; x1 = t[i].x + 5; y1 = t[i].y - 12;
        } else if (t[i].art == BOOPIE_ART_TV_FLAT) {
            x0 = t[i].x - 11; y0 = t[i].y - 25; x1 = t[i].x + 9; y1 = t[i].y - 14;
        } else {
            continue;
        }
        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                put(x - s_cam, y, ((x + y + (int)(look->t * 8)) % 7) ? c : 0xffffff);
            }
        }
    }
}

/* The beach: foam coming in and going out along the shore, and crabs about. */
static int shore_y(int x)
{
    return 46 + (int)(3 * sinf(x * 0.05f) + 2 * sinf(x * 0.13f + 2));   /* as tools/boopie/world_art.py draws it */
}

static void waves(float t)
{
    float reach = 2.5f + 2.5f * sinf(t * 0.9f);
    for (int sx = 0; sx < N; sx++) {
        int x = sx + s_cam;
        int y = shore_y(x) + (int)(reach + sinf(x * 0.21f + t * 1.7f) * 0.8f);
        put(sx, y, 0xf4f8fc);
        if ((x + (int)(t * 6)) % 3) {
            put(sx, y - 1, 0xc8e4f4);
        }
    }
}

static void crabs(float t)
{
    static const float AT[][3] = { { 180, 70, 22 }, { 360, 118, 16 }, { 60, 112, 14 } };   /* x, y, how far it goes */
    for (int k = 0; k < 3; k++) {
        float x = AT[k][0] + sinf(t * 0.5f + k * 2) * AT[k][2];
        bool step = ((int)(t * 6) + k) & 1;
        blit(step ? BOOPIE_ART_CRAB_A : BOOPIE_ART_CRAB_B, (int)x, (int)AT[k][1]);
    }
}

/* The line from the pet out to the float, bobbing; under, when a fish bites, with a "!". */
static void fishing(const boopie_world_t *w, const boopie_world_look_t *look)
{
    if (!w->fishing) {
        return;
    }
    int px = (int)w->x - s_cam, py = (int)w->y - 12;
    int fx = px + 15 * (w->facing < 0 ? -1 : 1), fy = (int)w->y - 4;   /* off the pier's end, in the water */
    int dip = w->bite ? (((int)(look->t * 10) & 1) ? 2 : 0) : (int)(sinf(look->t * 3) * 1.2f);
    for (int k = 0; k <= 10; k++) {   /* the line, sagging */
        float u = k / 10.0f;
        put(px + (int)((fx - px) * u), py + (int)((fy + dip - py) * u + sinf(u * 3.14159f) * 2), 0xe8e8f0);
    }
    put(fx, fy + dip, 0xf04848);
    put(fx + 1, fy + dip, 0xf04848);
    put(fx, fy + dip + 1, 0xffffff);
    put(fx + 1, fy + dip + 1, 0xffffff);
    if (w->bite) {
        blit(BOOPIE_ART_HINT_BANG, fx + s_cam, fy + dip - 2);   /* over the float */
    }
}

/* Fireflies in the woods at night: drifting, blinking. */
static void fireflies(float t)
{
    for (int k = 0; k < 14; k++) {
        float x = fmodf(k * 37.0f + t * (3 + k % 4) + sinf(t * 0.7f + k) * 10, 520.0f) - s_cam;
        float y = 62 + fmodf(k * 23.0f, 60.0f) + sinf(t * 1.3f + k * 2) * 6;
        if (sinf(t * 2.2f + k * 1.7f) < -0.2f) {
            continue;   /* blinked off */
        }
        put((int)x, (int)y, 0xf8ffb0);
        put((int)x - 1, (int)y, 0xa8c858);
        put((int)x + 1, (int)y, 0xa8c858);
        put((int)x, (int)y - 1, 0xa8c858);
        put((int)x, (int)y + 1, 0xa8c858);
    }
}

/* A plot: its soil (dark while damp), what grows in it, by kind and stage. */
static boopie_art_id_t crop_art(boopie_plant_t plant, boopie_stage_t st)
{
    bool cactus = plant == BOOPIE_PLANT_CACTUS;
    static const boopie_art_id_t RARE[][2] = {   /* bud, bloom */
        [BOOPIE_PLANT_PUMPKIN - BOOPIE_PLANT_PUMPKIN] = { BOOPIE_ART_CROP_BUD_PUMPKIN, BOOPIE_ART_CROP_PUMPKIN },
        [BOOPIE_PLANT_MELON - BOOPIE_PLANT_PUMPKIN] = { BOOPIE_ART_CROP_BUD_MELON, BOOPIE_ART_CROP_MELON },
        [BOOPIE_PLANT_ROSE - BOOPIE_PLANT_PUMPKIN] = { BOOPIE_ART_CROP_BUD_ROSE, BOOPIE_ART_CROP_ROSE },
    };
    if (plant >= BOOPIE_PLANT_PUMPKIN && (st == BOOPIE_STAGE_BUD || st == BOOPIE_STAGE_BLOOM)) {
        return RARE[plant - BOOPIE_PLANT_PUMPKIN][st == BOOPIE_STAGE_BLOOM];
    }
    switch (st) {
    case BOOPIE_STAGE_SEED: return BOOPIE_ART_CROP_SEED;
    case BOOPIE_STAGE_SPROUT: return cactus ? BOOPIE_ART_CROP_CACTUS_S : BOOPIE_ART_CROP_SPROUT;
    case BOOPIE_STAGE_LEAVES: return cactus ? BOOPIE_ART_CROP_CACTUS_M : BOOPIE_ART_CROP_LEAVES;
    case BOOPIE_STAGE_BUD:
        return cactus ? BOOPIE_ART_CROP_CACTUS_L : plant == BOOPIE_PLANT_SUNFLOWER ? BOOPIE_ART_CROP_BUD_SUN
               : plant == BOOPIE_PLANT_TULIP ? BOOPIE_ART_CROP_BUD_TULIP : BOOPIE_ART_CROP_BUD_BERRY;
    case BOOPIE_STAGE_BLOOM:
        return cactus ? BOOPIE_ART_CROP_CACTUS : plant == BOOPIE_PLANT_SUNFLOWER ? BOOPIE_ART_CROP_SUNFLOWER
               : plant == BOOPIE_PLANT_TULIP ? BOOPIE_ART_CROP_TULIP : BOOPIE_ART_CROP_STRAWBERRY;
    default: return BOOPIE_ART_COUNT;
    }
}

/* The plot's hint: plant (+), thirsty (a drop), ready (!), or none while it grows. */
static int plot_hint(const boopie_thing_t *t, const boopie_world_look_t *look, int *top)
{
    *top = t->y - 16;
    if (!look->garden) {
        return BOOPIE_ART_HINT_PLUS;
    }
    boopie_stage_t st = boopie_garden_stage(look->garden, t->arg);
    if (st == BOOPIE_STAGE_EMPTY) {
        return BOOPIE_ART_HINT_PLUS;
    }
    boopie_art_id_t crop = crop_art((boopie_plant_t)look->garden->pots[t->arg].plant, st);
    *top = t->y - 6 - boopie_art[crop].ay;
    if (st == BOOPIE_STAGE_BLOOM) {
        return BOOPIE_ART_HINT_BANG;
    }
    return boopie_garden_dry(look->garden, t->arg, look->now) ? BOOPIE_ART_HINT_WATER : 0;
}

static void plot(const boopie_thing_t *t, const boopie_world_look_t *look)
{
    const struct boopie_garden *g = look->garden;
    boopie_stage_t st = g ? boopie_garden_stage(g, t->arg) : BOOPIE_STAGE_EMPTY;
    bool damp = g && st != BOOPIE_STAGE_EMPTY && boopie_garden_damp_s(g, t->arg, look->now) > 0;
    blit(damp ? BOOPIE_ART_PLOT_DAMP : BOOPIE_ART_PLOT_DRY, t->x, t->y);
    if (st != BOOPIE_STAGE_EMPTY) {
        bool dry = boopie_garden_dry(g, t->arg, look->now);
        blit_tint(crop_art((boopie_plant_t)g->pots[t->arg].plant, st), t->x, t->y - 6, dry);
    }
}

static boopie_art_id_t art_of(const boopie_thing_t *t, const boopie_world_look_t *look)
{
    if (t->art == BOOPIE_ART_WINDOW_DAY && look->night) {
        return BOOPIE_ART_WINDOW_NIGHT;
    }
    if (t->art == BOOPIE_ART_BOWL_FULL && look->hungry) {
        return BOOPIE_ART_BOWL_EMPTY;
    }
    if (t->act == BOOPIE_DO_CHEST && (look->chests_open >> t->arg & 1)) {
        return BOOPIE_ART_CHEST_OPEN;
    }
    if (t->art == BOOPIE_ART_BERRY_BUSH && (look->gathered >> t->arg & 1)) {
        return BOOPIE_ART_BUSH;   /* picked today */
    }
    if (t->art == BOOPIE_ART_CAMPFIRE_A && ((int)(look->t * 4) & 1)) {
        return BOOPIE_ART_CAMPFIRE_B;   /* flickering */
    }
    if (t->art == BOOPIE_ART_WINDMILL_A && ((int)(look->t * 3) & 1)) {
        return BOOPIE_ART_WINDMILL_B;   /* turning */
    }
    return (boopie_art_id_t)t->art;
}

/* A mushroom picked today is gone till tomorrow. */
static bool gone(const boopie_thing_t *t, const boopie_world_look_t *look)
{
    return t->art == BOOPIE_ART_MUSHROOM && t->act == BOOPIE_DO_GATHER && (look->gathered >> t->arg & 1);
}

typedef struct {
    int x, y, r;
    uint32_t c;
} light_t;

/* Night: everything dimmed and blue, and near a lamp the true colours back, warmed. */
static void night(const boopie_thing_t *t, int n, int level)
{
    light_t lights[12];
    int nl = 0;
    for (int i = 0; i < n && nl < 12; i++) {
        if (!boopie_thing_shown(&t[i], level)) {
            continue;
        }
        switch (t[i].art) {
        case BOOPIE_ART_LAMP: lights[nl++] = (light_t){ t[i].x, t[i].y - 30, 46, 0xffdca0 }; break;
        case BOOPIE_ART_TV_OLD:
        case BOOPIE_ART_TV_FLAT: lights[nl++] = (light_t){ t[i].x, t[i].y - 16, 26, 0xa8d8ff }; break;
        case BOOPIE_ART_STAR_LAMP: lights[nl++] = (light_t){ t[i].x, t[i].y - 14, 30, 0xffe090 }; break;
        case BOOPIE_ART_AQUARIUM: lights[nl++] = (light_t){ t[i].x, t[i].y - 14, 26, 0x90d8ff }; break;
        case BOOPIE_ART_LAMP_POST: lights[nl++] = (light_t){ t[i].x, t[i].y - 27, 40, 0xffe0a0 }; break;
        case BOOPIE_ART_HOUSE_FRONT: lights[nl++] = (light_t){ t[i].x, t[i].y - 14, 40, 0xffd090 }; break;
        case BOOPIE_ART_MUSHROOM_RING: lights[nl++] = (light_t){ t[i].x, t[i].y - 4, 30, 0xc8b0ff }; break;
        case BOOPIE_ART_FAIRY_LIGHTS: lights[nl++] = (light_t){ t[i].x, t[i].y - 2, 44, 0xffe0b0 }; break;
        case BOOPIE_ART_LANTERN: lights[nl++] = (light_t){ t[i].x, t[i].y - 21, 22, 0xff9070 }; break;
        case BOOPIE_ART_JACK_O_LANTERN: lights[nl++] = (light_t){ t[i].x, t[i].y - 6, 26, 0xffb050 }; break;
        case BOOPIE_ART_XMAS_TREE: lights[nl++] = (light_t){ t[i].x, t[i].y - 20, 30, 0xfff0b0 }; break;
        case BOOPIE_ART_LIGHTHOUSE: lights[nl++] = (light_t){ t[i].x, t[i].y - 50, 60, 0xfff0b0 }; break;
        case BOOPIE_ART_TREE_HOUSE: lights[nl++] = (light_t){ t[i].x + 6, t[i].y - 45, 22, 0xffe0a0 }; break;
        default: break;
        }
    }
    for (int k = 0; k < nl; k++) {
        lights[k].x -= s_cam;
    }
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            uint8_t *p = s_rgb + (y * N + x) * 3;
            float r = p[0], g = p[1], b = p[2];
            float lit = 0;
            uint32_t warm = 0xffffff;
            for (int k = 0; k < nl; k++) {
                float dx = (float)(x - lights[k].x), dy = (y - lights[k].y) * 1.15f;
                float d = sqrtf(dx * dx + dy * dy);
                if (d < lights[k].r) {
                    float q = (1 - d / lights[k].r);
                    q = q * q * 5 + (((x + y) & 1) ? 0.5f : 0) - 0.25f;   /* steps, dithered */
                    float l = (float)(int)q / 5.0f;
                    l = l > 1 ? 1 : l;
                    if (l > lit) {
                        lit = l;
                        warm = lights[k].c;
                    }
                }
            }
            float nr = r * 0.34f, ng = g * 0.38f, nb = b * 0.6f + 14;
            if (lit > 0) {
                nr += (r * (warm >> 16 & 255) / 255 - nr) * lit;
                ng += (g * (warm >> 8 & 255) / 255 - ng) * lit;
                nb += (b * (warm & 255) / 255 - nb) * lit;
            }
            p[0] = (uint8_t)(nr > 255 ? 255 : nr);
            p[1] = (uint8_t)(ng > 255 ? 255 : ng);
            p[2] = (uint8_t)(nb > 255 ? 255 : nb);
        }
    }
}

void boopie_world_draw(const boopie_world_t *w, const boopie_world_look_t *look, uint8_t *rgb)
{
    s_rgb = rgb;
    s_cam = (int)(w->cam + 0.5f);
    boopie_art_id_t bg_id = boopie_room_background(w->room, look->level);
    const boopie_art_t *bg = &boopie_art[bg_id];
    const uint8_t *bg_pixels = bg_px(bg_id);
    if (!bg_pixels) {
        memset(rgb, 0, (size_t)N * N * 3);
        return;
    }
    int cam = s_cam + N > bg->w ? bg->w - N : s_cam;
    /* Outdoors, the weather on the ground: white with snow, grey and wet in rain, dull when cloudy. */
    bool out = boopie_world_outdoors(w->room);
    int wx = out ? look->weather : BOOPIE_WEATHER_SUNNY;
    for (int y = 0; y < N; y++) {
        const uint8_t *src = bg_pixels + y * bg->w + cam;
        uint8_t *dst = rgb + y * N * 3;
        for (int x = 0; x < N; x++) {
            uint32_t c = boopie_art_palette[src[x]];
            int r = (int)(c >> 16), g = (int)(c >> 8 & 255), b = (int)(c & 255);
            if (wx == BOOPIE_WEATHER_SNOW) {
                int k = ((x + y) & 1) ? 55 : 45;   /* dithered, patchy */
                r += (240 - r) * k / 100;
                g += (246 - g) * k / 100;
                b += (252 - b) * k / 100;
            } else if (wx == BOOPIE_WEATHER_RAIN) {
                r = r * 78 / 100;
                g = g * 82 / 100;
                b = b * 88 / 100 + 10;
            } else if (wx == BOOPIE_WEATHER_CLOUDY) {
                int grey = (r + g + b) / 3;
                r = (r * 3 + grey) / 4 * 92 / 100;
                g = (g * 3 + grey) / 4 * 92 / 100;
                b = (b * 3 + grey) / 4 * 95 / 100;
            }
            dst[x * 3] = (uint8_t)r;
            dst[x * 3 + 1] = (uint8_t)g;
            dst[x * 3 + 2] = (uint8_t)(b > 255 ? 255 : b);
        }
    }
    if (w->room == BOOPIE_ROOM_BEACH) {
        waves(look->t);   /* under the pier */
    }
    int n;
    const boopie_thing_t *t = boopie_room_things(w->room, &n);
    for (int layer = BOOPIE_LAYER_FLOOR; layer <= BOOPIE_LAYER_WALL; layer++) {
        for (int i = 0; i < n; i++) {
            if (t[i].layer == layer && boopie_thing_shown(&t[i], look->level)) {
                if (t[i].act == BOOPIE_DO_PLOT) {
                    plot(&t[i], look);
                } else {
                    blit(art_of(&t[i], look), t[i].x, t[i].y);
                }
            }
        }
    }
    if (w->room == BOOPIE_ROOM_BEACH) {
        crabs(look->t);
    }
    /* What stands, back to front, and the pet and the slimes among it. */
    bool pet_drawn = false;
    bool slime_drawn[BOOPIE_SLIMES] = { false };
    int done[48] = { 0 };
    for (;;) {
        int next = -1;
        for (int i = 0; i < n && i < 48; i++) {
            if (!done[i] && t[i].layer == BOOPIE_LAYER_STAND && boopie_thing_shown(&t[i], look->level)
                && (next < 0 || t[i].y < t[next].y)) {
                next = i;
            }
        }
        for (int k = 0; k < BOOPIE_SLIMES; k++) {
            if (!slime_drawn[k] && (next < 0 || w->slimes[k].y < t[next].y)
                && (pet_drawn || w->y >= w->slimes[k].y)) {
                slime(w, k, look);
                slime_drawn[k] = true;
            }
        }
        /* Asleep it lies on the bed, so after it. */
        bool front = w->state == BOOPIE_PET_SLEEPING;
        if (!pet_drawn && (next < 0 || (!front && w->y < t[next].y))) {
            pet(w, look);
            pet_drawn = true;
        }
        if (next < 0) {
            for (int k = 0; k < BOOPIE_SLIMES; k++) {
                if (!slime_drawn[k]) {
                    slime(w, k, look);   /* in front of the pet */
                }
            }
            break;
        }
        const boopie_art_t *a = &boopie_art[t[next].art];
        int x = t[next].x;
        if (t[next].art == BOOPIE_ART_CHICKEN) {
            x += (int)(sinf(look->t * 0.8f + next) * 4);   /* pecking about */
        }
        if (!gone(&t[next], look)) {
            shadow(x + 2, t[next].y + 1, a->w / 2.0f, 2.0f);
            blit(art_of(&t[next], look), x, t[next].y);
        }
        done[next] = 1;
    }
    fishing(w, look);
    tv_on(w, t, n, look);
    weather(w, t, n, look);
    if (look->night) {
        night(t, n, look->level);
        if (w->room == BOOPIE_ROOM_WOODS && look->weather < BOOPIE_WEATHER_RAIN) {
            fireflies(look->t);
        }
        if (boopie_world_outdoors(w->room) && (look->fest == BOOPIE_FEST_NEW_YEAR || look->fest == BOOPIE_FEST_SPRING)) {
            fireworks(look->t);
        }
    }
    antic_over(w, look);
    /* The hints last, over everything and never dimmed, bobbing. */
    for (int i = 0; i < n; i++) {
        if (t[i].hint && boopie_thing_shown(&t[i], look->level)) {
            const boopie_art_t *a = &boopie_art[t[i].art];
            int bob = (int)(sinf(look->t * 3 + i) * 1.2f);
            int top = t[i].y - a->ay;
            int hint = t[i].hint;
            if ((t[i].act == BOOPIE_DO_MAIL && !look->mail_waiting)
                || (t[i].act == BOOPIE_DO_CHEST && (look->chests_open >> t[i].arg & 1))
                || (t[i].act == BOOPIE_DO_GATHER && (look->gathered >> t[i].arg & 1))) {
                continue;   /* opened or picked today: nothing more till tomorrow */
            }
            if (t[i].art == BOOPIE_ART_DOOR_OUT) {
                top = t[i].y - 6;
            } else if (t[i].act == BOOPIE_DO_PLOT) {
                hint = plot_hint(&t[i], look, &top);
            } else if (t[i].act == BOOPIE_DO_FISH) {
                if (w->fishing || w->y < t[i].y - 8) {
                    continue;   /* out on it already */
                }
                top = t[i].y - 18;   /* halfway out along it */
            } else if (t[i].art == BOOPIE_ART_HOUSE_FRONT) {
                top = t[i].y - 18;   /* over its door, not its roof */
            }
            /* Not over the pet when it's stopped right under one: it'd hide its face. */
            bool on_pet = w->state != BOOPIE_PET_WALKING && fabsf(t[i].x - w->x) < 10 && top > w->y - 26
                          && top < w->y + 4;
            if (hint && !on_pet) {
                blit((boopie_art_id_t)hint, t[i].x, top - 1 + bob);
            }
        }
    }
}

void boopie_world_scale(const uint8_t *rgb, uint16_t *out, int out_w)
{
    /* Each scene pixel 3 x 3, the scene centred (466 = 3 x 156 - 2). */
    int off = (3 * N - out_w) / 2;
    for (int y = 0; y < out_w; y++) {
        const uint8_t *row = rgb + ((y + off) / 3) * N * 3;
        uint16_t *o = out + y * out_w;
        for (int x = 0; x < out_w; x++) {
            const uint8_t *p = row + ((x + off) / 3) * 3;
            o[x] = (uint16_t)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3);
        }
    }
}

/* ---- the HUD's pixel digits ---- */

static const char *const DIGITS[11][5] = {
    { "###", "#.#", "#.#", "#.#", "###" }, { ".#.", "##.", ".#.", ".#.", "###" }, { "###", "..#", "###", "#..", "###" },
    { "###", "..#", ".##", "..#", "###" }, { "#.#", "#.#", "###", "..#", "..#" }, { "###", "#..", "###", "..#", "###" },
    { "###", "#..", "###", "#.#", "###" }, { "###", "..#", ".#.", ".#.", ".#." }, { "###", "#.#", "###", "#.#", "###" },
    { "###", "#.#", "###", "..#", "###" }, { "...", ".#.", "...", ".#.", "..." },
};

static void block(uint16_t *out, int out_w, int x, int y, int w, int h, uint16_t c)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (i >= 0 && i < out_w && j >= 0 && j < out_w) {
                out[j * out_w + i] = c;
            }
        }
    }
}

static int glyph(char ch)
{
    return ch == ':' ? 10 : ch >= '0' && ch <= '9' ? ch - '0' : -1;
}

/* Text in pixel digits, centred on cx (or left at x if left), k px a dot, outlined. */
static int digits(uint16_t *out, int out_w, const char *s, int cx, int y, int k, bool left)
{
    int n = (int)strlen(s);
    int w = n * 4 * k - k;
    int x0 = left ? cx : cx - w / 2;
    const uint16_t ink = 0xffff, edge = 0x3a48;   /* white, outlined in dark green-grey */
    for (int pass = 0; pass < 2; pass++) {
        int x = x0;
        for (int c = 0; c < n; c++) {
            int g = glyph(s[c]);
            for (int j = 0; g >= 0 && j < 5; j++) {
                for (int i = 0; i < 3; i++) {
                    if (DIGITS[g][j][i] == '#') {
                        if (pass == 0) {
                            block(out, out_w, x + i * k - 3, y + j * k - 3, k + 6, k + 6, edge);
                        } else {
                            block(out, out_w, x + i * k, y + j * k, k, k, ink);
                        }
                    }
                }
            }
            x += 4 * k;
        }
    }
    return x0 + w;
}

void boopie_world_hud(uint16_t *out, int out_w, const char *clock, unsigned stars)
{
    int cx = out_w / 2;
    if (clock && *clock) {
        digits(out, out_w, clock, cx, 22, 5, false);
    }
    /* A star and the count under the clock. */
    char buf[12];
    int n = 0;
    unsigned v = stars;
    char tmp[12];
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 10);
    for (int i = 0; i < n; i++) {
        buf[i] = tmp[n - 1 - i];
    }
    buf[n] = '\0';
    int w = n * 12 - 3 + 22;
    int sx = cx - w / 2, sy = 56;
    static const char *const STAR[9] = { "....#....", "....#....", "...###...", "#########", ".#######.",
                                         "..#####..", "..##.##..", ".##...##.", "#.......#" };
    for (int pass = 0; pass < 2; pass++) {
        for (int j = 0; j < 9; j++) {
            for (int i = 0; i < 9; i++) {
                if (STAR[j][i] == '#') {
                    if (pass == 0) {
                        block(out, out_w, sx + i * 2 - 2, sy + j * 2 - 2, 6, 6, 0x3a48);
                    } else {
                        block(out, out_w, sx + i * 2, sy + j * 2, 2, 2, 0xfe28);
                    }
                }
            }
        }
    }
    digits(out, out_w, buf, sx + 24, sy + 1, 3, true);
}
