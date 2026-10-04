/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_posture.h"

#include <math.h>
#include <string.h>

#define STILL_G 0.06f     /* between readings: still */
#define LEAVE_S 0.3f      /* out of a pose this long: it's left it */

void boopie_posture_init(boopie_posture_t *p)
{
    memset(p, 0, sizeof(*p));
}

static float angle_deg(const float a[3], const float b[3])
{
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    float na = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    float nb = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    if (na < 0.3f || nb < 0.3f) {
        return 0;   /* falling, or nonsense */
    }
    float c = dot / (na * nb);
    c = c > 1 ? 1 : c < -1 ? -1 : c;
    return acosf(c) * 57.29578f;
}

boopie_pose_event_t boopie_posture_feed(boopie_posture_t *p, const float g[3], float dt)
{
    boopie_pose_event_t ev = BOOPIE_POSE_NONE;

    /* Face down: gravity pulling out through the screen. */
    bool down_now = g[2] > 0.8f;
    if (down_now) {
        p->face_down_s += dt;
        p->away_s = 0;
        if (!p->face_down && p->face_down_s >= BOOPIE_POSE_FACE_DOWN_S) {
            p->face_down = true;
            ev = BOOPIE_POSE_FACE_DOWN;
        }
    } else {
        p->face_down_s = 0;
        if (p->face_down) {
            p->away_s += dt;
            if (p->away_s >= LEAVE_S || dt >= LEAVE_S) {
                p->face_down = false;
                p->away_s = 0;
                ev = BOOPIE_POSE_FACE_UP;
                p->rest_s = 0;   /* turning it over isn't a lift as well */
            }
        }
    }

    /* Upside down: held up, its top toward the ground. */
    bool upside_now = g[1] < -0.65f && fabsf(g[2]) < 0.75f;
    if (upside_now) {
        p->upside_s += dt;
        if (!p->upside && p->upside_s >= BOOPIE_POSE_UPSIDE_S && ev == BOOPIE_POSE_NONE) {
            p->upside = true;
            ev = BOOPIE_POSE_UPSIDE_DOWN;
        }
    } else {
        p->upside_s = 0;
        if (p->upside && g[1] > -0.3f && ev == BOOPIE_POSE_NONE) {
            p->upside = false;
            ev = BOOPIE_POSE_RIGHT_WAY_UP;
        }
    }

    /* Picked up: lying still a while, then tipped well away from how it lay. */
    bool still = p->have_last && fabsf(g[0] - p->last[0]) < STILL_G && fabsf(g[1] - p->last[1]) < STILL_G
                 && fabsf(g[2] - p->last[2]) < STILL_G;
    if (p->face_down || down_now) {
        p->rest_s = 0;   /* face down isn't lying in wait */
    } else if (p->rest_s >= BOOPIE_POSE_REST_S && angle_deg(g, p->rest) >= BOOPIE_POSE_LIFT_DEG) {
        p->rest_s = 0;
        if (ev == BOOPIE_POSE_NONE) {
            ev = BOOPIE_POSE_LIFTED;
        }
    } else if (still) {
        if (p->rest_s == 0) {
            memcpy(p->rest, g, sizeof(p->rest));
        }
        p->rest_s += dt;
    } else if (p->rest_s < BOOPIE_POSE_REST_S) {
        p->rest_s = 0;   /* moving before it settled */
    }
    memcpy(p->last, g, sizeof(p->last));
    p->have_last = true;
    return ev;
}
