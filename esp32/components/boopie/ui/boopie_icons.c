/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_icons.h"

#include <math.h>

#include <string.h>

typedef struct {
    const char *const *rows;
    uint8_t h;
    const char *keys;         /* the characters drawn ... */
    uint32_t colours[5];      /* ... in these colours */
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
    /* The pet hopping up to a pillar's gap. */
    [BOOPIE_ICON_HOP] = { ROWS("........ggg...", "........ggg...", "........ggg...", ".......GGGGG..", "..............",
                               "...pp.........", "..pppp........", ".pkppkp.......", ".pppppp.......", "..pppp........",
                               ".......GGGGG..", "........ggg...", "........ggg...", "........ggg..."),
                          "gGpk", { 0x5bd18a, 0x9ff0bd, 0xff8fb8, 0x1a1530 }, 0x4aa8ff },
    /* A sunflower in a pot. */
    [BOOPIE_ICON_GARDEN] = { ROWS("...yyyyy...", "..yybbbyy..", "..ybbbbby..", "..yybbbyy..", "...yyyyy...",
                                  ".....s.....", ".ll..s.....", "..llss..ll.", ".....s.ll..", ".ooooooooo.",
                                  "..ooooooo..", "..ooooooo..", "...ooooo..."),
                             "ybslo", { 0xffd23c, 0x78481e, 0x46a046, 0x6ed264, 0xdc7846 }, 0x5bb8e8 },
    [BOOPIE_ICON_BOLT] = { ROWS("....##", "...##.", "..##..", ".#####", "....##", "...##.", "..##..", ".##..."),
                           "#", { 0x90e878 }, 0x4068b8 },
    [BOOPIE_ICON_BAG] = { ROWS("..####..", ".#....#.", "########", "#yyyyyy#", "########", "#yyyyyy#", "#yyyyyy#",
                               ".######."),
                          "#y", { 0xc89628, 0xf8d048 }, 0x4068b8 },
    [BOOPIE_ICON_SHOP] = { ROWS("cccccccccc", "cwcwcwcwcc", "wwwwwwwwww", "w........w", "w.bb.....w", "w.bb..ww.w",
                                "w.bb..ww.w", "wwwwwwwwww"),
                           "cwb", { 0x60c8d8, 0xe8f0f8, 0x6098c8 }, 0x4068b8 },
    [BOOPIE_ICON_HOUSE] = { ROWS(".....rr.....", "....rrrr....", "...rrrrrr...", "..rrrrrrrr..", ".rrrrrrrrrr.",
                                 "rrrrrrrrrrrr", ".wwwwwwwwww.", ".wbbwwwwddw.", ".wbbwwwwddw.", ".wwwwwwwddw.",
                                 ".wwwwwwwddw."),
                            "rwbd", { 0xd85848, 0xf8ecd0, 0x78b0e8, 0xa87048 }, 0x2a2150 },
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
    /* An arrow down into a tray. */
    [BOOPIE_ICON_UPDATE] = { ROWS(".....##.....", ".....##.....", ".....##.....", ".....##.....", "..#..##..#..",
                                  "...#.##.#...", "....####....", ".....##.....", "#..........#", "#..........#",
                                  "############"),
                             "#", { WHITE }, 0x3ccf8e },
    /* An "i" in a ring. */
    [BOOPIE_ICON_INFO] = { ROWS("...######...", "..#......#..", ".#...##...#.", "#....##....#", "#..........#",
                                "#...###....#", "#....##....#", "#....##....#", ".#..####..#.", "..#......#..",
                                "...######..."),
                           "#", { WHITE }, 0x8a7dff },
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

const lv_image_dsc_t *boopie_icon_outlined(boopie_icon_t which, int scale)
{
    static struct {
        int which, scale;
        lv_image_dsc_t dsc;
    } s_made[8];
    static int s_count;
    if ((int)which < 0 || which >= BOOPIE_ICON_COUNT || scale < 1) {
        return NULL;
    }
    for (int i = 0; i < s_count; i++) {
        if (s_made[i].which == (int)which && s_made[i].scale == scale) {
            return &s_made[i].dsc;
        }
    }
    if (s_count == 8) {
        return NULL;
    }
    /* The art a cell bigger all round, then each empty cell touching it inked. */
    const art_t *a = &ART[which];
    int cw = (int)strlen(a->rows[0]) + 2, ch = a->h + 2;
    uint32_t cells[24 * 24] = { 0 };
    if (cw > 24 || ch > 24) {
        return NULL;
    }
    for (int j = 0; j < a->h; j++) {
        for (int i = 0; i + 2 < cw; i++) {
            const char *k = a->rows[j][i] != '.' ? strchr(a->keys, a->rows[j][i]) : NULL;
            cells[(j + 1) * cw + i + 1] = k ? 0xff000000u | a->colours[k - a->keys] : 0;
        }
    }
    uint32_t inked[24 * 24];
    memcpy(inked, cells, sizeof inked);
    for (int j = 0; j < ch; j++) {
        for (int i = 0; i < cw; i++) {
            if (cells[j * cw + i]) {
                continue;
            }
            bool touch = (i > 0 && cells[j * cw + i - 1]) || (i + 1 < cw && cells[j * cw + i + 1])
                         || (j > 0 && cells[(j - 1) * cw + i]) || (j + 1 < ch && cells[(j + 1) * cw + i]);
            if (touch) {
                inked[j * cw + i] = 0xff282c38;
            }
        }
    }
    int w = cw * scale, h = ch * scale;
    uint32_t *px = lv_malloc((size_t)w * h * sizeof(uint32_t));
    if (!px) {
        return NULL;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            px[y * w + x] = inked[(y / scale) * cw + x / scale];
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

lv_obj_t *boopie_edge_chip(lv_obj_t *parent, const char *symbol, int size)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, size, size);
    lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(0x1c1830), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_80, 0);   /* bg_opa draws directly: no layer */
    lv_obj_set_style_border_color(chip, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_opa(chip, LV_OPA_50, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chip, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_t *l = lv_label_create(chip);
    lv_obj_set_style_text_font(l, size >= 26 ? &lv_font_montserrat_16 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
    lv_label_set_text(l, symbol);
    lv_obj_center(l);
    return chip;
}

void boopie_edge_chip_at(lv_obj_t *chip, int dx, int dy, int inset)
{
    lv_display_t *d = lv_obj_get_display(chip);
    int half = (int)(d ? lv_display_get_horizontal_resolution(d) : 466) / 2;
    float len = sqrtf((float)(dx * dx + dy * dy));
    float r = (float)(half - lv_obj_get_style_width(chip, 0) / 2 - inset);
    int x = len > 0 ? (int)(dx * r / len) : 0;
    int y = len > 0 ? (int)(dy * r / len) : 0;
    lv_obj_align(chip, LV_ALIGN_CENTER, x, y);
}
