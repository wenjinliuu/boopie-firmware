/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_font.h"

#define CJK_COPIES 8
#define SIZES 8

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
                                                      LV_FONT_KERNING_NONE, 256);
            if (!f) {
                return NULL;
            }
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
    if (!base || base->fallback) {
        return base;
    }
    for (int i = 0; i < CJK_COPIES; i++) {
        if (s_base[i] == base) {
            return &s_copy[i];
        }
        if (!s_base[i]) {
            /* Montserrat's line is about 1.1 of its size; Chinese reads right a touch larger. */
            const lv_font_t *cjk = boopie_font_ui((base->line_height * 100 + 55) / 110 + 1);
            if (!cjk) {
                return base;
            }
            s_copy[i] = *base;
            s_copy[i].fallback = cjk;
            s_base[i] = base;
            return &s_copy[i];
        }
    }
    return base;
}
