/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

/*
 * Shake detection from accelerometer samples, plain C so it's tested on the
 * host (tests/test_boopie_shake.py). A shake is sustained hard jolting, about
 * half a second of it: one knock, setting the board down or walking with it
 * isn't. After a shake it waits a few seconds before reporting another.
 */
typedef struct {
    float energy;      /* decaying sum of how far each sample is from 1 g */
    float cooldown;    /* seconds before the next shake can count */
} boopie_shake_t;

/* Feed one sample (in g) taken dt seconds after the last; true on a shake. */
bool boopie_shake_feed(boopie_shake_t *s, float ax, float ay, float az, float dt);
