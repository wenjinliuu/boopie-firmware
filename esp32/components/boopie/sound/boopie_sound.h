/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Boopie's sounds: short 8-bit tunes synthesized here, to go with the pixel
 * characters, or the assets partition's sounds/<key>.wav (16 kHz mono 16-bit)
 * where a theme brings its own. Played by the voice task, which owns the
 * speaker: any task asks, and it plays when it's free. Silent when the
 * speaker is off. Plain C apart from the queue.
 */

#define BOOPIE_SOUND_RATE 16000

typedef enum {
    BOOPIE_SOUND_BOOT = 0,     /* hello */
    BOOPIE_SOUND_OFF,          /* goodbye */
    BOOPIE_SOUND_LISTEN,       /* listening */
    BOOPIE_SOUND_SENT,         /* sent */
    BOOPIE_SOUND_ERROR,
    BOOPIE_SOUND_LEVEL_UP,
    BOOPIE_SOUND_EAT,
    BOOPIE_SOUND_POKE,
    BOOPIE_SOUND_SCORE,        /* games: a hit */
    BOOPIE_SOUND_GOLD,         /* a gold one */
    BOOPIE_SOUND_CLOUD,        /* the rain cloud */
    BOOPIE_SOUND_GAME_OVER,
    BOOPIE_SOUND_NOTIFY,       /* a card, a reminder */
    BOOPIE_SOUND_PURR,         /* stroked: a soft rumble */
    BOOPIE_SOUND_HELLO,        /* picked up: hi! */
    BOOPIE_SOUND_COUNT,
} boopie_sound_t;

const char *boopie_sound_key(boopie_sound_t s);   /* "level_up" */

/* From any task: plays it soon (one waits; a newer one replaces it). */
void boopie_sound_play(boopie_sound_t s);

/* The voice task: the one waiting, if any. */
bool boopie_sound_pending(void);
bool boopie_sound_take(boopie_sound_t *s);

/* The sound as PCM (BOOPIE_SOUND_RATE, mono) into out, up to max frames;
 * the frames written. The asset's if there is one, else synthesized. */
size_t boopie_sound_render(boopie_sound_t s, int16_t *out, size_t max);

/* The longest any built-in sound runs, in frames. */
#define BOOPIE_SOUND_MAX_FRAMES (BOOPIE_SOUND_RATE * 3 / 2)
