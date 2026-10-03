/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_icons.h"

#include <string.h>

typedef struct {
    const char *const *rows;
    uint8_t h;
    const char *keys;         /* the characters drawn ... */
    uint32_t colours[4];      /* ... in these colours */
    uint32_t tile;
} art_t;

#define ROWS(...) (const char *const[]){ __VA_ARGS__ }, sizeof((const char *const[]){ __VA_ARGS__ }) / sizeof(char *)
#define WHITE 0xffffff

static const art_t ART[BOOPIE_ICON_COUNT] = {
    [BOOPIE_ICON_WIFI] = { ROWS("..########..", ".##......##.", "##........##", "............", "...######...",
                                "..##....##..", "............", "....####....", "............", ".....##.....",
                                ".....##....."),
                           "#", { WHITE }, 0x3d8bff },
    /* A phone with a code on it: phone setup. */
    [BOOPIE_ICON_PHONE] = { ROWS("..########..", "..#......#..", "..#.##.#.#..", "..#.##...#..",
                                 "..#....#.#..", "..#.#.##.#..", "..#......#..", "..#......#..", "..#..##..#..",
                                 "..########.."),
                            "#", { WHITE }, 0x34c27a },
    [BOOPIE_ICON_BRAIN] = { ROWS("...###.###..", "..#...#...#.", ".#.##.#.#..#", ".#....#..#.#", "#..#..#....#",
                                 "#.#...#.##.#", "#.....#....#", ".#.##.#.#.#.", "..#...#...#.", "...###.###.."),
                            "#", { WHITE }, 0xff6b9a },
    /* The companion: a paw. */
    [BOOPIE_ICON_PAW] = { ROWS("..###..###..", "..###..###..", "...#....#...", "##........##", "##..####..##",
                               "...######...", "..########..", "..########..", "...######..."),
                          "#", { WHITE }, 0xa77dff },
    /* A shield. */
    [BOOPIE_ICON_VPN] = { ROWS(".....##.....", "...######...", ".##########.", ".##########.", ".####..####.",
                               ".####..####.", ".####..####.", "..########..", "...######...", "....####....",
                               ".....##....."),
                          "#", { WHITE }, 0x2bb5a8 },
    [BOOPIE_ICON_BLUETOOTH] = { ROWS("...#...", "...##..", "...#.#.", "#..#..#", ".#.#.#.", "..###..", ".#.#.#.",
                                     "#..#..#", "...#.#.", "...##..", "...#..."),
                                "#", { WHITE }, 0x3d6bff },
    [BOOPIE_ICON_SOUND] = { ROWS("....#.......", "...##...#...", "..###....#..", "####..#..#..", "####...#..#.",
                                 "####...#..#.", "####..#..#..", "..###....#..", "...##...#...", "....#......."),
                            "#", { WHITE }, 0xff5c5c },
    /* A moon and a star: the screen and its sleep. */
    [BOOPIE_ICON_DISPLAY] = { ROWS("...####.....", ".####.......", ".###.....#..", "###.....###.", "###......#..",
                                   "###.........", "###.........", "####......##", ".####...###.", "..########..",
                                   "....####...."),
                              "#", { WHITE }, 0x5a55d6 },
    [BOOPIE_ICON_BATTERY] = { ROWS("##########..", "#........#..", "#.##.##..###", "#.##.##..###", "#........#..",
                                   "##########.."),
                              "#", { WHITE }, 0x34c27a },
    /* Drawers. */
    [BOOPIE_ICON_STORAGE] = { ROWS("############", "#..........#", "#...####...#", "#..........#", "############",
                                   "#..........#", "#...####...#", "#..........#", "############"),
                              "#", { WHITE }, 0x8b84a8 },
    /* Round again. */
    [BOOPIE_ICON_GUIDE] = { ROWS("....####.#..", "..##....##..", ".#.....###..", "#...........", "#...........",
                                 "#..........#", ".#........#.", "..##....##..", "....####...."),
                            "#", { WHITE }, 0xffa63d },
    /* A cookie dropping into a bowl. */
    [BOOPIE_ICON_CATCH] = { ROWS("......ccc.....", ".....cocco....", ".....cccoc....", "......ccc.....", "..............",
                                 "......w.w.....", "..............", "wwwwwwwwwwwwww", "bwwwwwwwwwwwwb", ".bbbbbbbbbbbb.",
                                 "..bbbbbbbbbb..", "...bbbbbbbb..."),
                            "cowb", { 0xd69650, 0x6e4022, WHITE, 0xd8d4ec }, 0xffa63d },
    [BOOPIE_ICON_MAZE] = { ROWS("#############", "#pp#.....#..#", "#pp#.....#..#", "#..#..####..#", "#.....#.....#",
                                "#.....#.....#", "####..#..####", "#........#..#", "#........#..#", "#..#######..#",
                                "#.........ee#", "#.........ee#", "#############"),
                           "#pe", { WHITE, 0xff7ab0, 0x7cf0b0 }, 0x5b8cff },
    [BOOPIE_ICON_CHAT] = { ROWS("..##########..", ".#wwwwwwwwww#.", "#wwwwwwwwwwww#", "#wwwwwwwwwwww#", "#wkkwwkkwwkkw#",
                                "#wkkwwkkwwkkw#", "#wwwwwwwwwwww#", ".#wwwwwwwwww#.", "..#ww#######..", "..#w#.........",
                                "..##.........."),
                           "#wk", { 0xe6fff2, WHITE, 0x2fae74 }, 0x3ccf8e },
    [BOOPIE_ICON_ALBUM] = { ROWS("##############", "#ssssssssssss#", "#ssssssssyyss#", "#ssssssssyyss#", "#sssgssssssss#",
                                 "#ssgggsssssss#", "#sgggggssgsss#", "#gggggggggggs#", "#gggggggggggg#", "#gggggggggggg#",
                                 "##############"),
                            "#syg", { WHITE, 0xbfe3ff, 0xffd246, 0x5bd18a }, 0xff6b9a },
    /* A sound wave. */
    [BOOPIE_ICON_NOISE] = { ROWS("........#......", "....#...#......", "....#...#.#...#", "..#.#.#.#.#...#", "#.#.#.#.#.#.#.#", "#.#.#.#.#.#.#.#", "#.#.#.#.#.#.#.#", "..#.#.#.#.#...#", "....#...#.#...#", "....#...#......", "........#......"),
                            "#", { WHITE }, 0x6c5ce7 },
    /* Static. */
    [BOOPIE_ICON_NOISE_WHITE] = { ROWS("#.#..#.##.#.", ".#.##.#..#.#", "#..#.#.##..#", ".##.#..#.#.#", "#.#.##.#..#.",
                                       "..#..#.#.##.", "#.##.#..#.#.", ".#..#.##..##"),
                                  "#", { WHITE }, 0x8b84a8 },
    /* A soft wave. */
    [BOOPIE_ICON_NOISE_PINK] = { ROWS("...##.......", "..#..#......", ".#....#.....", "#......#...#", "........#.#.",
                                      ".........#.."),
                                 "#", { WHITE }, 0xff8fb8 },
    /* A cloud and its rain. */
    [BOOPIE_ICON_NOISE_RAIN] = { ROWS("....####....", "..##....##..", ".#........#.", "#..........#", ".##########.",
                                      "............", "..#...#...#.", ".#...#...#..", "............", "...#...#....",
                                      "..#...#....."),
                                 "#", { WHITE }, 0x4a90d9 },
    [BOOPIE_ICON_NOISE_WAVES] = { ROWS("...###......", "..#...#.....", ".#..#..#...#", "#..#.#..###.", "............",
                                       "..##....##..", ".#..#..#..#.", "#....##....#"),
                                  "#", { WHITE }, 0x2bb5a8 },
};

#define MADE_MAX 24

const lv_image_dsc_t *boopie_icon(boopie_icon_t which, int scale)
{
    static struct {
        int which, scale;
        lv_image_dsc_t dsc;
    } s_made[MADE_MAX];
    static int s_count;
    if ((int)which < 0 || which >= BOOPIE_ICON_COUNT || scale < 1) {
        return NULL;
    }
    for (int i = 0; i < s_count; i++) {
        if (s_made[i].which == (int)which && s_made[i].scale == scale) {
            return &s_made[i].dsc;
        }
    }
    if (s_count == MADE_MAX) {
        return NULL;
    }
    const art_t *a = &ART[which];
    int cw = (int)strlen(a->rows[0]), w = cw * scale, h = a->h * scale;
    uint32_t *px = lv_malloc((size_t)w * h * sizeof(uint32_t));
    if (!px) {
        return NULL;
    }
    for (int j = 0; j < a->h; j++) {
        for (int i = 0; i < cw; i++) {
            const char *k = a->rows[j][i] && a->rows[j][i] != '.' ? strchr(a->keys, a->rows[j][i]) : NULL;
            uint32_t c = k ? 0xff000000u | a->colours[k - a->keys] : 0;
            for (int y = 0; y < scale; y++) {
                for (int x = 0; x < scale; x++) {
                    px[(j * scale + y) * w + i * scale + x] = c;
                }
            }
        }
    }
    s_made[s_count].which = (int)which;
    s_made[s_count].scale = scale;
    s_made[s_count].dsc = (lv_image_dsc_t){
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .w = w, .h = h,
                    .stride = w * sizeof(uint32_t) },
        .data_size = (uint32_t)(w * h * sizeof(uint32_t)),
        .data = (const uint8_t *)px,
    };
    return &s_made[s_count++].dsc;
}

const lv_image_dsc_t *boopie_icon_keyed(const lv_image_dsc_t *src)
{
    static struct {
        const lv_image_dsc_t *src;
        lv_image_dsc_t dsc;
    } s_keyed[8];
    static int s_count;
    if (!src || src->header.cf != LV_COLOR_FORMAT_RGB565) {
        return src;
    }
    for (int i = 0; i < s_count; i++) {
        if (s_keyed[i].src == src) {
            return &s_keyed[i].dsc;
        }
    }
    if (s_count == 8) {
        return src;
    }
    int w = src->header.w, h = src->header.h;
    uint32_t *px = lv_malloc((size_t)w * h * sizeof(uint32_t));
    if (!px) {
        return src;
    }
    const uint16_t *in = (const uint16_t *)src->data;
    for (int i = 0; i < w * h; i++) {
        uint16_t c = in[i];
        uint32_t r = (c >> 11) * 255 / 31, g = (c >> 5 & 63) * 255 / 63, b = (c & 31) * 255 / 31;
        px[i] = c ? 0xff000000u | r << 16 | g << 8 | b : 0;
    }
    s_keyed[s_count].src = src;
    s_keyed[s_count].dsc = (lv_image_dsc_t){
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .w = w, .h = h,
                    .stride = w * sizeof(uint32_t) },
        .data_size = (uint32_t)(w * h * sizeof(uint32_t)),
        .data = (const uint8_t *)px,
    };
    return &s_keyed[s_count++].dsc;
}

uint32_t boopie_icon_tile_colour(boopie_icon_t which)
{
    return (int)which >= 0 && which < BOOPIE_ICON_COUNT ? ART[which].tile : 0x2a2150;
}

lv_obj_t *boopie_icon_tile_custom(lv_obj_t *parent, const lv_image_dsc_t *art, uint32_t tile, int size)
{
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, size, size);
    lv_obj_set_style_radius(t, size * 3 / 10, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(t, lv_color_hex(tile), 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_EVENT_BUBBLE);
    if (art) {
        lv_obj_t *img = lv_image_create(t);
        lv_image_set_src(img, art);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(img);
    }
    return t;
}

lv_obj_t *boopie_icon_tile(lv_obj_t *parent, boopie_icon_t which, int size, int scale)
{
    return boopie_icon_tile_custom(parent, boopie_icon(which, scale), boopie_icon_tile_colour(which), size);
}
