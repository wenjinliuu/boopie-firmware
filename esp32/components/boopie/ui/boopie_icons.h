/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "lvgl.h"

/*
 * Pixel icons on coloured tiles, for the apps page (big, in colour) and the
 * settings rows (small, white glyphs), so the tool screens share the face's
 * pixel look. Built into the muse component with the pages; LVGL task.
 */

typedef enum {
    /* Settings. */
    BOOPIE_ICON_WIFI = 0,
    BOOPIE_ICON_PHONE,
    BOOPIE_ICON_BRAIN,
    BOOPIE_ICON_PAW,
    BOOPIE_ICON_VPN,
    BOOPIE_ICON_BLUETOOTH,
    BOOPIE_ICON_SOUND,
    BOOPIE_ICON_DISPLAY,
    BOOPIE_ICON_BATTERY,
    BOOPIE_ICON_STORAGE,
    BOOPIE_ICON_GUIDE,
    /* Apps. */
    BOOPIE_ICON_CATCH,
    BOOPIE_ICON_MAZE,
    BOOPIE_ICON_CHAT,
    BOOPIE_ICON_ALBUM,
    BOOPIE_ICON_NOISE,
    BOOPIE_ICON_HOP,
    /* 白噪音's sounds. */
    BOOPIE_ICON_NOISE_WHITE,
    BOOPIE_ICON_NOISE_PINK,
    BOOPIE_ICON_NOISE_RAIN,
    BOOPIE_ICON_NOISE_WAVES,
    BOOPIE_ICON_COUNT,
} boopie_icon_t;

/* The art alone, `scale` pixels a cell, transparent round it (made once). */
const lv_image_dsc_t *boopie_icon(boopie_icon_t which, int scale);

/* An RGB565 picture with its black made see-through (the pet's head on a
 * tile), made once per picture. */
const lv_image_dsc_t *boopie_icon_keyed(const lv_image_dsc_t *rgb565);

/* Its tile colour. */
uint32_t boopie_icon_tile_colour(boopie_icon_t which);

/*
 * A rounded tile `size` px square in the icon's colour with the art centred
 * on it, `scale` px a cell; clicks pass through to the parent. `art` overrides
 * the art (the pet's head for 戳戳布比), with `tile` its colour.
 */
lv_obj_t *boopie_icon_tile(lv_obj_t *parent, boopie_icon_t which, int size, int scale);
lv_obj_t *boopie_icon_tile_custom(lv_obj_t *parent, const lv_image_dsc_t *art, uint32_t tile, int size);
