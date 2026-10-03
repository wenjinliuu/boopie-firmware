/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "lvgl.h"

/* A character's head (avatar index, as boopie_avatar.h counts them) as an
 * LVGL image, `scale` screen pixels a grid cell; made once and kept. NULL if
 * there's no memory for it. In the LVGL task. */
const lv_image_dsc_t *boopie_head(int avatar, int scale);
