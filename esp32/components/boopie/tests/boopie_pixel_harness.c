/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Renders frames for test_boopie_pixel.py. Each stdin line is
 *   <character> <expression> <t> [<overlay> <overlay_t>] [scene:<scene>] [skin:<skin>]
 * (the scene runs on the same clock as the expression)
 * and gets one frame on stdout, BOOPIE_PX x BOOPIE_PX x 3 bytes of RGB. A
 * line "time" instead prints the mean milliseconds a frame takes. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "boopie_pixel.h"

static int char_from_key(const char *key)
{
    for (int c = 0; c < BOOPIE_CHAR_COUNT; c++) {
        if (strcmp(boopie_char_key((boopie_char_t)c), key) == 0) {
            return c;
        }
    }
    return -1;
}

int main(void)
{
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        char ckey[32], ekey[32], okey[32] = "";
        double t = 0, ot = 0;
        if (strncmp(line, "time", 4) == 0) {
            boopie_pixel_pose_t p = { .expr = BOOPIE_EXPR_SPEAKING, .level = -1 };
            int n = 0;
            clock_t start = clock();
            for (int c = 0; c < BOOPIE_CHAR_COUNT; c++) {
                boopie_pixel_set_character((boopie_char_t)c, BOOPIE_COLOUR_DEFAULT);
                for (int i = 0; i < 50; i++, n++) {
                    p.t = i * 0.04;
                    boopie_pixel_render(&p);
                }
            }
            printf("%.3f\n", (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC / n);
            continue;
        }
        char skin[32] = "";
        char *kp = strstr(line, "skin:");
        if (kp) {
            sscanf(kp + 5, "%31s", skin);
            *kp = '\0';
        }
        /* food:<index> as the prototype's FOODS */
        int food = 0;
        char *fp = strstr(line, "food:");
        if (fp) {
            food = atoi(fp + 5);
            *fp = '\0';
        }
        char wear[64] = "";
        char *wp = strstr(line, "wear:");
        if (wp) {
            sscanf(wp + 5, "%63s", wear);
            *wp = '\0';
        }
        uint32_t worn = 0;
        for (char *tok = strtok(wear, ","); tok; tok = strtok(NULL, ",")) {
            boopie_acc_t acc;
            if (!boopie_acc_from_key(tok, &acc)) {
                fprintf(stderr, "bad accessory: %s\n", tok);
                return 1;
            }
            worn |= BOOPIE_ACC_BIT(acc);
        }
        char scene[32] = "";
        char *sp = strstr(line, "scene:");
        if (sp) {
            sscanf(sp + 6, "%31s", scene);
            *sp = '\0';
        }
        int got = sscanf(line, "%31s %31s %lf %31s %lf", ckey, ekey, &t, okey, &ot);
        boopie_pixel_pose_t p = { .t = t, .level = -1, .scene_t = t };
        int c = char_from_key(ckey);
        if (got < 3 || c < 0 || !boopie_expr_from_name(ekey, &p.expr)) {
            fprintf(stderr, "bad line: %s", line);
            return 1;
        }
        if (got == 5) {
            boopie_overlay_t o;
            if (!boopie_overlay_from_name(okey, &o)) {
                fprintf(stderr, "bad overlay: %s\n", okey);
                return 1;
            }
            p.overlays = BOOPIE_OVERLAY_BIT(o);
            p.overlay_t[o] = ot;
        }
        if (scene[0]) {
            bool found = false;
            for (int i = 0; i < BOOPIE_SCENE_COUNT; i++) {
                if (strcmp(boopie_scene_key((boopie_scene_t)i), scene) == 0) {
                    p.scene = (boopie_scene_t)i;
                    found = true;
                }
            }
            if (!found) {
                fprintf(stderr, "bad scene: %s\n", scene);
                return 1;
            }
        }
        p.food = (boopie_food_t)food;
        boopie_pixel_set_wear(worn);
        boopie_pixel_set_skin(skin[0] ? boopie_skin_from_key(skin) : -1);
        boopie_pixel_set_character((boopie_char_t)c, BOOPIE_COLOUR_DEFAULT);
        boopie_pixel_render(&p);
        fwrite(boopie_pixel_rgb(), 1, BOOPIE_PX * BOOPIE_PX * 3, stdout);
    }
    return 0;
}
