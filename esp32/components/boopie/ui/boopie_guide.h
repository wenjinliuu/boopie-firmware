/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/*
 * The setup guide (docs/boopie-interaction.md): hello, getting online, the
 * brain, the pet and its name, then how to use it. Shown on the first boot,
 * and again from settings. Full screen over everything; in the LVGL task.
 */
void boopie_guide_start(void);
bool boopie_guide_active(void);

/* Each frame or so: brings the guide back when it's sent someone off to
 * settings and they're back on the face (`on_face`). */
void boopie_guide_tick(bool on_face);
