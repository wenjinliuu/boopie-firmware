/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Renders every sound (sound/boopie_sound.c): prints each one's frames and
 * peak as JSON, and with an argument writes them all, a gap between, as raw
 * 16 kHz PCM to that file, for test_boopie_sound.py. */

#include <stdio.h>
#include <stdlib.h>

#include "boopie_sound.h"

int main(int argc, char **argv)
{
    static int16_t pcm[BOOPIE_SOUND_MAX_FRAMES];
    static const int16_t gap[BOOPIE_SOUND_RATE / 2];
    FILE *raw = argc > 1 ? fopen(argv[1], "wb") : NULL;
    printf("{");
    for (int s = 0; s < BOOPIE_SOUND_COUNT; s++) {
        size_t n = boopie_sound_render((boopie_sound_t)s, pcm, BOOPIE_SOUND_MAX_FRAMES);
        int peak = 0;
        for (size_t i = 0; i < n; i++) {
            peak = abs(pcm[i]) > peak ? abs(pcm[i]) : peak;
        }
        printf("%s\"%s\":[%zu,%d,%d]", s ? "," : "", boopie_sound_key((boopie_sound_t)s), n, peak,
               n ? abs(pcm[n - 1]) : 0);
        if (raw) {
            fwrite(pcm, sizeof(int16_t), n, raw);
            fwrite(gap, sizeof(int16_t), sizeof gap / sizeof gap[0], raw);
        }
    }
    boopie_sound_t got;
    boopie_sound_play(BOOPIE_SOUND_EAT);
    boopie_sound_play(BOOPIE_SOUND_LEVEL_UP);   /* the newer one replaces it */
    bool took = boopie_sound_take(&got);
    printf(",\"queue\":[%d,%d,%d]}\n", took, got == BOOPIE_SOUND_LEVEL_UP, boopie_sound_pending());
    if (raw) {
        fclose(raw);
    }
    return 0;
}
