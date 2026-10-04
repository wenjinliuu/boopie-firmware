/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Feeds pet/boopie_posture.c for test_boopie_posture.py. Each argument is
 * "gx,gy,gz,dt,n": n readings of that gravity, dt seconds apart. Prints each
 * event as "<reading number> <event>".
 */

#include <stdio.h>

#include "boopie_posture.h"

int main(int argc, char **argv)
{
    boopie_posture_t p;
    boopie_posture_init(&p);
    int k = 0;
    for (int i = 1; i < argc; i++) {
        float g[3], dt;
        int n;
        if (sscanf(argv[i], "%f,%f,%f,%f,%d", &g[0], &g[1], &g[2], &dt, &n) != 5) {
            return 2;
        }
        for (int j = 0; j < n; j++, k++) {
            boopie_pose_event_t ev = boopie_posture_feed(&p, g, dt);
            if (ev != BOOPIE_POSE_NONE) {
                printf("%d %d\n", k, (int)ev);
            }
        }
    }
    return 0;
}
