/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

/*
 * How the board is held (docs/boopie-interaction.md "姿势感应"), from which
 * way gravity pulls in the screen's frame (boopie_imu_gravity: +x the
 * screen's right, +y its bottom, +z out of its face, in g): put face down,
 * turned over again, upside down and back, and picked up after lying still.
 * Plain C with the time passed in, fed whatever rate the IMU is read at (20
 * a second with the screen on, once a second off).
 */

typedef enum {
    BOOPIE_POSE_NONE = 0,
    BOOPIE_POSE_FACE_DOWN,       /* put face down: the screen sleeps */
    BOOPIE_POSE_FACE_UP,         /* turned back over */
    BOOPIE_POSE_UPSIDE_DOWN,     /* held with its top at the bottom */
    BOOPIE_POSE_RIGHT_WAY_UP,
    BOOPIE_POSE_LIFTED,          /* picked up after lying still */
} boopie_pose_event_t;

#define BOOPIE_POSE_FACE_DOWN_S 1.0f   /* face down this long */
#define BOOPIE_POSE_UPSIDE_S 0.6f
#define BOOPIE_POSE_REST_S 10.0f       /* lying still this long before a lift counts */
#define BOOPIE_POSE_LIFT_DEG 25.0f     /* tipped this far from how it lay */

typedef struct {
    float face_down_s, upside_s, rest_s, away_s;
    bool face_down, upside;
    float rest[3];                     /* how it lay while still */
    float last[3];
    bool have_last;
} boopie_posture_t;

void boopie_posture_init(boopie_posture_t *p);

/* A reading `dt` seconds after the last; the event it makes, if any. */
boopie_pose_event_t boopie_posture_feed(boopie_posture_t *p, const float g[3], float dt);
