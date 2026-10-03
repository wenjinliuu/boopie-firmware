/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Feeds made-up accelerometer traces to the shake detector and prints, as
 * JSON, how many shakes each one counted, for test_boopie_shake.py. */

#include <math.h>
#include <stdio.h>

#include "boopie_shake.h"

#define RATE 20.0f   /* samples a second, as the IMU task reads while awake */

typedef void (*trace_t)(float t, float *a);

static void still(float t, float *a) { (void)t; a[0] = 0.02f; a[1] = -0.01f; a[2] = 1.0f; }
static void walking(float t, float *a) { a[0] = 0.1f; a[1] = 0; a[2] = 1.0f + 0.3f * sinf(t * 2 * 3.14159f * 2); }
static void knock(float t, float *a) { still(t, a); if (t > 2.0f && t < 2.05f) a[2] = 3.5f; }
static void set_down(float t, float *a) { still(t, a); if (t > 2.0f && t < 2.3f) a[2] = 0.4f; if (t >= 2.3f && t < 2.4f) a[2] = 2.0f; }
static void tilt(float t, float *a) { float k = t < 5 ? t / 5 : 1; a[0] = sinf(k * 1.5f); a[1] = 0; a[2] = cosf(k * 1.5f); }
static void shake_short(float t, float *a) { still(t, a); if (t > 2.0f && t < 3.5f) a[0] = 1.8f * sinf(t * 2 * 3.14159f * 5); }
static void shake_long(float t, float *a) { still(t, a); if (t > 1.0f && t < 11.0f) a[0] = 1.8f * sinf(t * 2 * 3.14159f * 5); }

static int count(trace_t f, float seconds)
{
    boopie_shake_t s = { 0 };
    int n = 0;
    for (int i = 0; i < (int)(seconds * RATE); i++) {
        float a[3];
        f(i / RATE, a);
        n += boopie_shake_feed(&s, a[0], a[1], a[2], 1 / RATE);
    }
    return n;
}

int main(void)
{
    printf("{\"still\":%d,\"walking\":%d,\"knock\":%d,\"set_down\":%d,\"tilt\":%d,"
           "\"shake_short\":%d,\"shake_long\":%d}\n",
           count(still, 30), count(walking, 30), count(knock, 10), count(set_down, 10), count(tilt, 10),
           count(shake_short, 10), count(shake_long, 15));
    return 0;
}
