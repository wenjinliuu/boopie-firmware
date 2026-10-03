/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Renders 白噪音 (sound/boopie_noise.c) for test_boopie_noise.py:
 *   stats KIND SECONDS          one JSON line: level, peak, dark/bright energy, clipping
 *   timeline KIND MINUTES STOP  the level each second for MINUTES*60+15 s; a stop at STOP s (-1: none)
 *   wav KIND SECONDS FILE       a 16 kHz mono WAV to listen to
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_noise.h"
#include "boopie_sound.h"

#define CHUNK 320   /* 20 ms, as the voice task renders */

static double rms(const int16_t *x, size_t n)
{
    double a = 0;
    for (size_t i = 0; i < n; i++) a += (double)x[i] * x[i];
    return sqrt(a / (double)n);
}

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    boopie_noise_t kind = (boopie_noise_t)atoi(argv[2]);
    int16_t buf[CHUNK];
    if (strcmp(argv[1], "stats") == 0) {
        int secs = atoi(argv[3]);
        boopie_noise_play(kind, 0, 0);
        double all = 0, lo = 0, hi = 0, lp = 0, prev = 0;
        int peak = 0, clipped = 0;
        size_t frames = 0;
        for (int64_t t = 0; t < secs * 1000; t += 20) {
            boopie_noise_render(buf, CHUNK, t);
            if (t < 3000) continue;   /* past the fade in */
            for (int i = 0; i < CHUNK; i++) {
                double x = buf[i];
                all += x * x;
                lp += 0.06 * (x - lp);          /* ~150 Hz and under */
                lo += lp * lp;
                double d = x - prev;             /* the brightest */
                hi += d * d;
                prev = x;
                peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
                clipped += buf[i] == 32767 || buf[i] == -32768;
            }
            frames += CHUNK;
        }
        printf("{\"rms\": %.1f, \"peak\": %d, \"clipped\": %d, \"low\": %.4f, \"high\": %.4f}\n",
               sqrt(all / frames), peak, clipped, lo / all, hi / all);
    } else if (strcmp(argv[1], "timeline") == 0) {
        int minutes = atoi(argv[3]), stop = argc > 4 ? atoi(argv[4]) : -1;
        boopie_noise_play(kind, minutes, 0);
        int total = minutes ? minutes * 60 + 15 : 60;
        printf("[");
        for (int s = 0; s < total; s++) {
            double level = 0;
            for (int k = 0; k < 50; k++) {
                int64_t t = (int64_t)s * 1000 + k * 20;
                if (stop >= 0 && t == (int64_t)stop * 1000) boopie_noise_stop(t);
                bool on = boopie_noise_render(buf, CHUNK, t);
                (void)on;
                level += rms(buf, CHUNK) / 50;
            }
            int left = 0;
            bool playing = boopie_noise_playing((int64_t)s * 1000 + 999, NULL, &left);
            printf("%s[%.1f, %d, %d]", s ? ", " : "", level, playing, left);
        }
        printf("]\n");
    } else if (strcmp(argv[1], "wav") == 0 && argc > 4) {
        int secs = atoi(argv[3]);
        FILE *f = fopen(argv[4], "wb");
        if (!f) return 1;
        uint32_t data = (uint32_t)secs * BOOPIE_SOUND_RATE * 2, rate = BOOPIE_SOUND_RATE, byte_rate = rate * 2;
        uint32_t riff = 36 + data, fmt_len = 16;
        uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
        fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
        fwrite(&fmt_len, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
        fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
        fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
        boopie_noise_play(kind, 0, 0);
        for (int64_t t = 0; t < secs * 1000; t += 20) {
            boopie_noise_render(buf, CHUNK, t);
            fwrite(buf, 2, CHUNK, f);
        }
        fclose(f);
    }
    return 0;
}
