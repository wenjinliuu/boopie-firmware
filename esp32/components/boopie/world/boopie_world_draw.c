/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_world_draw.h"

#include <math.h>
#include <string.h>

#define N BOOPIE_WORLD_W

static uint8_t *s_rgb;

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

static void blit(boopie_art_id_t id, int x, int y)
{
    const boopie_art_t *a = &boopie_art[id];
    int x0 = x - a->ax, y0 = y - a->ay;
    for (int j = 0; j < a->h; j++) {
        const uint8_t *row = a->px + j * a->w;
        for (int i = 0; i < a->w; i++) {
            if (row[i]) {
                put(x0 + i, y0 + j, boopie_art_palette[row[i]]);
            }
        }
    }
}

static void shadow(float cx, float cy, float rx, float ry)
{
    for (int y = (int)(cy - ry); y <= (int)(cy + ry); y++) {
        for (int x = (int)(cx - rx); x <= (int)(cx + rx); x++) {
            float dx = (x - cx) / rx, dy = (y - cy) / ry;
            if (dx * dx + dy * dy <= 1) {
                darken(x, y, 0.8f);
            }
        }
    }
}

static void pet(const boopie_world_t *w, const boopie_world_look_t *look)
{
    if (!look->pet) {
        return;
    }
    bool walking = w->state == BOOPIE_PET_WALKING;
    bool asleep = w->state == BOOPIE_PET_SLEEPING;
    /* A hop as it walks; a slow breath standing; still, asleep. */
    int hop = walking ? (int)(fabsf(sinf(w->walked * 0.45f)) * 3) : asleep ? 0 : ((int)(look->t * 2) & 1);
    int px = (int)w->x, py = (int)w->y;
    shadow(px, py, look->pet_w / 2.0f + 1, 2.2f);
    int x0 = px - look->pet_w / 2, y0 = py - look->pet_h - 2 - hop;
    /* Two little feet under it, its body's colour darkened, stepping in turn as it walks. */
    uint16_t body = look->pet[(look->pet_h - 1) * look->pet_w + look->pet_w / 2];
    if (body && !asleep) {
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
            if (c) {
                uint32_t rgb = ((uint32_t)((c >> 11) * 255 / 31) << 16) | ((uint32_t)(((c >> 5) & 63) * 255 / 63) << 8)
                               | (uint32_t)((c & 31) * 255 / 31);
                put(x0 + i, y0 + j, rgb);
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

static boopie_art_id_t art_of(const boopie_thing_t *t, const boopie_world_look_t *look)
{
    if (t->art == BOOPIE_ART_WINDOW_DAY && look->night) {
        return BOOPIE_ART_WINDOW_NIGHT;
    }
    if (t->art == BOOPIE_ART_BOWL_FULL && look->hungry) {
        return BOOPIE_ART_BOWL_EMPTY;
    }
    return (boopie_art_id_t)t->art;
}

typedef struct {
    int x, y, r;
    uint32_t c;
} light_t;

/* Night: everything dimmed and blue, and near a lamp the true colours back, warmed. */
static void night(const boopie_thing_t *t, int n, int level)
{
    light_t lights[6];
    int nl = 0;
    for (int i = 0; i < n && nl < 6; i++) {
        if (!boopie_thing_shown(&t[i], level)) {
            continue;
        }
        switch (t[i].art) {
        case BOOPIE_ART_LAMP: lights[nl++] = (light_t){ t[i].x, t[i].y - 30, 46, 0xffdca0 }; break;
        case BOOPIE_ART_TV_OLD:
        case BOOPIE_ART_TV_FLAT: lights[nl++] = (light_t){ t[i].x, t[i].y - 16, 26, 0xa8d8ff }; break;
        case BOOPIE_ART_STAR_LAMP: lights[nl++] = (light_t){ t[i].x, t[i].y - 14, 30, 0xffe090 }; break;
        case BOOPIE_ART_AQUARIUM: lights[nl++] = (light_t){ t[i].x, t[i].y - 14, 26, 0x90d8ff }; break;
        default: break;
        }
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
    const boopie_art_t *bg = &boopie_art[boopie_room_background(w->room, look->level)];
    for (int i = 0; i < N * N; i++) {
        uint32_t c = boopie_art_palette[bg->px[i]];
        rgb[i * 3] = (uint8_t)(c >> 16);
        rgb[i * 3 + 1] = (uint8_t)(c >> 8);
        rgb[i * 3 + 2] = (uint8_t)c;
    }
    int n;
    const boopie_thing_t *t = boopie_room_things(w->room, &n);
    for (int layer = BOOPIE_LAYER_FLOOR; layer <= BOOPIE_LAYER_WALL; layer++) {
        for (int i = 0; i < n; i++) {
            if (t[i].layer == layer && boopie_thing_shown(&t[i], look->level)) {
                blit(art_of(&t[i], look), t[i].x, t[i].y);
            }
        }
    }
    /* What stands, back to front, and the pet among it. */
    bool pet_drawn = false;
    int done[32] = { 0 };
    for (;;) {
        int next = -1;
        for (int i = 0; i < n && i < 32; i++) {
            if (!done[i] && t[i].layer == BOOPIE_LAYER_STAND && boopie_thing_shown(&t[i], look->level)
                && (next < 0 || t[i].y < t[next].y)) {
                next = i;
            }
        }
        /* Asleep it lies on the bed, so after it. */
        bool front = w->state == BOOPIE_PET_SLEEPING;
        if (!pet_drawn && (next < 0 || (!front && w->y < t[next].y))) {
            pet(w, look);
            pet_drawn = true;
        }
        if (next < 0) {
            break;
        }
        const boopie_art_t *a = &boopie_art[t[next].art];
        shadow(t[next].x + 2, t[next].y + 1, a->w / 2.0f, 2.0f);
        blit(art_of(&t[next], look), t[next].x, t[next].y);
        done[next] = 1;
    }
    if (look->night) {
        night(t, n, look->level);
    }
    /* The hints last, over everything and never dimmed, bobbing. */
    for (int i = 0; i < n; i++) {
        if (t[i].hint && boopie_thing_shown(&t[i], look->level)) {
            const boopie_art_t *a = &boopie_art[t[i].art];
            int bob = (int)(sinf(look->t * 3 + i) * 1.2f);
            int top = t[i].y - a->ay;
            if (t[i].art == BOOPIE_ART_DOOR_OUT) {
                top = t[i].y - 6;
            }
            blit((boopie_art_id_t)t[i].hint, t[i].x, top - 1 + bob);
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
