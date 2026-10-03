/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 白噪音 (docs/boopie-interaction.md): sounds to fall asleep or focus to,
 * made as they play (nothing stored), at BOOPIE_SOUND_RATE. Any task sets
 * them going or stops them; the voice task renders them between turns, so a
 * question pauses them and they carry on after the answer. They fade in,
 * and out at the end of their time. Plain C apart from the atomics.
 */

typedef enum {
    BOOPIE_NOISE_WHITE = 0,   /* 白噪音 */
    BOOPIE_NOISE_PINK,        /* 粉红噪音: softer, less hiss */
    BOOPIE_NOISE_RAIN,        /* 雨声 */
    BOOPIE_NOISE_WAVES,       /* 海浪 */
    BOOPIE_NOISE_COUNT,
} boopie_noise_t;

#define BOOPIE_NOISE_FADE_IN_MS 2000
#define BOOPIE_NOISE_FADE_OUT_MS 10000

const char *boopie_noise_name(boopie_noise_t kind);   /* "雨声" */

/* Plays `kind` from now_ms for `minutes` (0: until stopped). */
void boopie_noise_play(boopie_noise_t kind, int minutes, int64_t now_ms);
/* Fades out and stops. */
void boopie_noise_stop(int64_t now_ms);

/* Playing (still fading out counts). *kind and *left_s, if given: which, and
 * the seconds left (-1: until stopped). */
bool boopie_noise_playing(int64_t now_ms, boopie_noise_t *kind, int *left_s);

/* The voice task: the next n frames, now_ms at the first. False (and
 * silence) once it's stopped. */
bool boopie_noise_render(int16_t *out, size_t n, int64_t now_ms);

#ifdef __cplusplus
}
#endif
