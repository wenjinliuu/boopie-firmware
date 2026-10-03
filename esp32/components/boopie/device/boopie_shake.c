/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_shake.h"

#include <math.h>

#define DEADBAND_G 0.4f     /* ignore gentle handling */
#define DECAY_PER_S 0.25f    /* energy left after a second of calm */
#define TRIGGER 0.18f        /* leaky g-seconds of jolting past the deadband; tune on the board */
#define COOLDOWN_S 4.0f

bool boopie_shake_feed(boopie_shake_t *s, float ax, float ay, float az, float dt)
{
    if (dt <= 0 || dt > 1) {
        dt = 0.05f;
    }
    float d = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f) - DEADBAND_G;
    s->energy = s->energy * powf(DECAY_PER_S, dt) + (d > 0 ? d * dt : 0);
    if (s->cooldown > 0) {
        s->cooldown -= dt;
        return false;
    }
    if (s->energy >= TRIGGER) {
        s->energy = 0;
        s->cooldown = COOLDOWN_S;
        return true;
    }
    return false;
}
