/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_font.h"

#include "boopie_pixel_font.h"

/* The one thing that differs between the two sizes. */
typedef struct {
    uint8_t scale;
} pixel_font_dsc_t;

static bool get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *g, uint32_t letter,
                          uint32_t letter_next)
{
    (void)letter_next;
    bool tab = letter == '\t';
    int32_t index = boopie_pixel_find(tab ? ' ' : letter);
    if (index < 0) {
        return false;
    }
    int scale = ((const pixel_font_dsc_t *)font->dsc)->scale;
    int w = boopie_pixel_width(index) * scale;
    bool blank = boopie_pixel_blank(index);

    g->adv_w = tab ? 2 * w : w;
    g->box_w = blank ? 0 : w;
    g->box_h = blank ? 0 : BOOPIE_PIXEL_CELL * scale;
    g->ofs_x = 0;
    g->ofs_y = -(BOOPIE_PIXEL_CELL - BOOPIE_PIXEL_BASELINE) * scale;
    g->stride = 0;
    g->format = LV_FONT_GLYPH_FORMAT_A1;
    g->is_placeholder = false;
    g->gid.index = (uint32_t)index + 1;   /* 0 means none */
    return true;
}

/* The glyph as an A8 bitmap in draw_buf, each pixel a scale x scale block.
 * LVGL takes draw_buf itself back, as the built-in fonts return it. */
static const void *get_glyph_bitmap(lv_font_glyph_dsc_t *g, lv_draw_buf_t *draw_buf)
{
    if (!g->gid.index || !draw_buf || !g->box_w) {
        return NULL;
    }
    int32_t index = (int32_t)g->gid.index - 1;
    int scale = ((const pixel_font_dsc_t *)g->resolved_font->dsc)->scale;
    uint32_t stride = draw_buf->header.stride;
    uint8_t *out = draw_buf->data;
    for (int y = 0; y < g->box_h; y++) {
        uint8_t *row = out + (uint32_t)y * stride;
        for (int x = 0; x < g->box_w; x++) {
            row[x] = boopie_pixel_dot(index, x / scale, y / scale) ? 0xFF : 0x00;
        }
    }
    return draw_buf;
}

static const pixel_font_dsc_t s_dsc_2x = { .scale = 2 };
static const pixel_font_dsc_t s_dsc_1x = { .scale = 1 };

const lv_font_t boopie_font_pixel_24 = {
    .get_glyph_dsc = get_glyph_dsc,
    .get_glyph_bitmap = get_glyph_bitmap,
    .line_height = BOOPIE_PIXEL_CELL * 2,
    .base_line = (BOOPIE_PIXEL_CELL - BOOPIE_PIXEL_BASELINE) * 2,
    .underline_position = -2,
    .underline_thickness = 2,
    .dsc = &s_dsc_2x,
};

const lv_font_t boopie_font_pixel_12 = {
    .get_glyph_dsc = get_glyph_dsc,
    .get_glyph_bitmap = get_glyph_bitmap,
    .line_height = BOOPIE_PIXEL_CELL,
    .base_line = BOOPIE_PIXEL_CELL - BOOPIE_PIXEL_BASELINE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &s_dsc_1x,
};

#define CJK_COPIES 8
#define SIZES 8
/* Rendered glyphs kept per size: a screen of Chinese rarely shows more than
 * this many different ones, and 256 at 28 px held some 200 KB of PSRAM each. */
#define GLYPH_CACHE 96

/* ui.otf, linked in by the build (EMBED_FILES; the simulator's incbin). */
extern const uint8_t ui_otf_start[] __asm__("_binary_ui_otf_start");
extern const uint8_t ui_otf_end[] __asm__("_binary_ui_otf_end");

const lv_font_t *boopie_font_ui(int size)
{
    static struct {
        int size;
        lv_font_t *font;
    } s_made[SIZES];
    for (int i = 0; i < SIZES; i++) {
        if (s_made[i].font && s_made[i].size == size) {
            return s_made[i].font;
        }
    }
    for (int i = 0; i < SIZES; i++) {
        if (!s_made[i].font) {
            lv_font_t *f = lv_tiny_ttf_create_data_ex(ui_otf_start, (size_t)(ui_otf_end - ui_otf_start), size,
                                                      LV_FONT_KERNING_NONE, GLYPH_CACHE);
            if (!f) {
                return NULL;
            }
            /* Past GB2312, the pixel font has the rest of GBK. */
            f->fallback = size >= 20 ? &boopie_font_pixel_24 : &boopie_font_pixel_12;
            s_made[i].size = size;
            s_made[i].font = f;
            return f;
        }
    }
    return NULL;
}

const lv_font_t *boopie_font_with_cjk(const lv_font_t *base)
{
    static const lv_font_t *s_base[CJK_COPIES];
    static lv_font_t s_copy[CJK_COPIES];
    if (!base || base->fallback || base->dsc == &s_dsc_2x || base->dsc == &s_dsc_1x) {
        return base;
    }
    for (int i = 0; i < CJK_COPIES; i++) {
        if (s_base[i] == base) {
            return &s_copy[i];
        }
        if (!s_base[i]) {
            /* Montserrat's line is about 1.1 of its size; Chinese reads right a touch larger. */
            const lv_font_t *cjk = boopie_font_ui((base->line_height * 100 + 55) / 110 + 1);
            s_copy[i] = *base;
            s_copy[i].fallback = cjk ? cjk : base->line_height >= 20 ? &boopie_font_pixel_24 : &boopie_font_pixel_12;
            s_base[i] = base;
            return &s_copy[i];
        }
    }
    return base;
}
