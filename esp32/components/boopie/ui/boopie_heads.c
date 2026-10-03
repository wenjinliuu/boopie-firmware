/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_heads.h"

#include "boopie_avatar.h"
#include "boopie_pixel.h"

#define MADE_MAX 24

const lv_image_dsc_t *boopie_head(int avatar, int scale)
{
    static struct {
        int avatar, scale;
        lv_image_dsc_t dsc;
    } s_made[MADE_MAX];
    static int s_count;
    if (avatar < 0 || avatar >= BOOPIE_AVATAR_COUNT || scale < 1) {
        return NULL;
    }
    for (int i = 0; i < s_count; i++) {
        if (s_made[i].avatar == avatar && s_made[i].scale == scale) {
            return &s_made[i].dsc;
        }
    }
    if (s_count == MADE_MAX) {
        return NULL;
    }
    int w = BOOPIE_HEAD_W * scale, h = BOOPIE_HEAD_H * scale;
    uint16_t *px = lv_malloc((size_t)w * h * sizeof(uint16_t));
    if (!px) {
        return NULL;
    }
    boopie_pixel_head_image(avatar == BOOPIE_AVATAR_MUSE ? BOOPIE_SKIN_MUSE : avatar - 1, px, scale);
    s_made[s_count].avatar = avatar;
    s_made[s_count].scale = scale;
    s_made[s_count].dsc = (lv_image_dsc_t){
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = w, .h = h,
                    .stride = w * sizeof(uint16_t) },
        .data_size = (uint32_t)(w * h * sizeof(uint16_t)),
        .data = (const uint8_t *)px,
    };
    return &s_made[s_count++].dsc;
}
