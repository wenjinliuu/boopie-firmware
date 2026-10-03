/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_sound.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "boopie_assets.h"

/* A note: a frequency (sliding to f1 if set), how long, a voice, a level. */
typedef enum { SQUARE, PULSE, TRIANGLE, NOISE } voice_t;
typedef struct {
    uint16_t f0, f1;   /* Hz; 0 is a rest */
    uint16_t ms;
    uint8_t voice;
    uint8_t vol;       /* 0..100 */
} note_t;

#define END { 0, 0, 0, 0, 0 }
/* Note frequencies, rounded: C5 523, E5 659, G5 784, C6 1047 ... */
static const note_t BOOT[] = { { 523, 0, 70, PULSE, 60 }, { 659, 0, 70, PULSE, 60 }, { 784, 0, 70, PULSE, 60 },
                               { 1047, 0, 220, PULSE, 60 }, END };
static const note_t OFF[] = { { 1047, 0, 80, TRIANGLE, 70 }, { 784, 0, 80, TRIANGLE, 70 },
                              { 659, 0, 80, TRIANGLE, 70 }, { 523, 0, 260, TRIANGLE, 70 }, END };
static const note_t LISTEN[] = { { 700, 1300, 90, TRIANGLE, 70 }, END };
static const note_t SENT[] = { { 1100, 600, 90, TRIANGLE, 60 }, END };
static const note_t ERROR_[] = { { 220, 0, 110, SQUARE, 45 }, { 0, 0, 60, SQUARE, 0 }, { 196, 0, 160, SQUARE, 45 }, END };
static const note_t LEVEL_UP[] = { { 523, 0, 90, PULSE, 60 }, { 659, 0, 90, PULSE, 60 }, { 784, 0, 90, PULSE, 60 },
                                   { 1047, 0, 120, PULSE, 60 }, { 0, 0, 50, PULSE, 0 }, { 784, 0, 90, PULSE, 60 },
                                   { 1047, 0, 380, PULSE, 65 }, END };
static const note_t EAT[] = { { 440, 330, 70, TRIANGLE, 80 }, { 0, 0, 50, TRIANGLE, 0 }, { 494, 370, 70, TRIANGLE, 80 },
                              { 0, 0, 50, TRIANGLE, 0 }, { 523, 392, 90, TRIANGLE, 80 }, END };
static const note_t POKE[] = { { 380, 900, 80, TRIANGLE, 60 }, END };
static const note_t SCORE[] = { { 988, 0, 40, PULSE, 55 }, { 1319, 0, 110, PULSE, 55 }, END };
static const note_t GOLD[] = { { 1319, 0, 50, PULSE, 55 }, { 1568, 0, 50, PULSE, 55 }, { 2093, 0, 160, PULSE, 55 }, END };
static const note_t CLOUD[] = { { 160, 110, 180, NOISE, 45 }, END };
static const note_t GAME_OVER[] = { { 784, 0, 120, PULSE, 55 }, { 659, 0, 120, PULSE, 55 }, { 523, 0, 120, PULSE, 55 },
                                    { 659, 0, 120, PULSE, 55 }, { 784, 0, 360, PULSE, 60 }, END };
static const note_t NOTIFY[] = { { 1319, 0, 80, TRIANGLE, 65 }, { 1047, 0, 160, TRIANGLE, 65 }, END };

static const struct {
    const char *key;
    const note_t *notes;
} SOUNDS[BOOPIE_SOUND_COUNT] = {
    [BOOPIE_SOUND_BOOT] = { "boot", BOOT },           [BOOPIE_SOUND_OFF] = { "off", OFF },
    [BOOPIE_SOUND_LISTEN] = { "listen", LISTEN },     [BOOPIE_SOUND_SENT] = { "sent", SENT },
    [BOOPIE_SOUND_ERROR] = { "error", ERROR_ },       [BOOPIE_SOUND_LEVEL_UP] = { "level_up", LEVEL_UP },
    [BOOPIE_SOUND_EAT] = { "eat", EAT },              [BOOPIE_SOUND_POKE] = { "poke", POKE },
    [BOOPIE_SOUND_SCORE] = { "score", SCORE },        [BOOPIE_SOUND_GOLD] = { "gold", GOLD },
    [BOOPIE_SOUND_CLOUD] = { "cloud", CLOUD },        [BOOPIE_SOUND_GAME_OVER] = { "game_over", GAME_OVER },
    [BOOPIE_SOUND_NOTIFY] = { "notify", NOTIFY },
};

const char *boopie_sound_key(boopie_sound_t s)
{
    return (int)s >= 0 && s < BOOPIE_SOUND_COUNT ? SOUNDS[s].key : NULL;
}

/* ---- asking the voice task ---- */

static volatile int s_pending = -1;

void boopie_sound_play(boopie_sound_t s)
{
    if ((int)s >= 0 && s < BOOPIE_SOUND_COUNT) {
        s_pending = (int)s;
    }
}

bool boopie_sound_pending(void)
{
    return s_pending >= 0;
}

bool boopie_sound_take(boopie_sound_t *s)
{
    int p = s_pending;
    if (p < 0) {
        return false;
    }
    s_pending = -1;
    *s = (boopie_sound_t)p;
    return true;
}

/* ---- the sounds ---- */

/* A theme's own: a WAV of 16 kHz mono 16-bit samples. */
static size_t from_wav(const uint8_t *w, size_t n, int16_t *out, size_t max)
{
    if (n < 12 || memcmp(w, "RIFF", 4) != 0 || memcmp(w + 8, "WAVE", 4) != 0) {
        return 0;
    }
    bool ok = false;
    for (size_t i = 12; i + 8 <= n;) {
        uint32_t len = (uint32_t)w[i + 4] | (uint32_t)w[i + 5] << 8 | (uint32_t)w[i + 6] << 16 | (uint32_t)w[i + 7] << 24;
        const uint8_t *body = w + i + 8;
        if (len > n - i - 8) {
            return 0;
        }
        if (memcmp(w + i, "fmt ", 4) == 0 && len >= 16) {
            uint16_t fmt = (uint16_t)(body[0] | body[1] << 8), ch = (uint16_t)(body[2] | body[3] << 8);
            uint32_t rate = (uint32_t)body[4] | (uint32_t)body[5] << 8 | (uint32_t)body[6] << 16 | (uint32_t)body[7] << 24;
            uint16_t bits = (uint16_t)(body[14] | body[15] << 8);
            ok = fmt == 1 && ch == 1 && rate == BOOPIE_SOUND_RATE && bits == 16;
        } else if (memcmp(w + i, "data", 4) == 0 && ok) {
            size_t frames = len / 2 < max ? len / 2 : max;
            for (size_t k = 0; k < frames; k++) {
                out[k] = (int16_t)(body[2 * k] | body[2 * k + 1] << 8);
            }
            return frames;
        }
        i += 8 + len + (len & 1);
    }
    return 0;
}

static size_t synth(const note_t *notes, int16_t *out, size_t max)
{
    size_t n = 0;
    uint32_t noise = 0x12345678u;
    double phase = 0;
    for (const note_t *note = notes; note->ms && n < max; note++) {
        size_t len = (size_t)note->ms * BOOPIE_SOUND_RATE / 1000;
        for (size_t i = 0; i < len && n < max; i++, n++) {
            if (!note->f0 || !note->vol) {
                out[n] = 0;
                continue;
            }
            double k = (double)i / len;
            double f = note->f1 ? note->f0 + (note->f1 - note->f0) * k : note->f0;
            phase += f / BOOPIE_SOUND_RATE;
            phase -= floor(phase);
            double v;
            switch (note->voice) {
            case PULSE:   /* 25 % duty: the classic thin chip tone */
                v = phase < 0.25 ? 1 : -1;
                break;
            case TRIANGLE:
                v = 4 * fabs(phase - 0.5) - 1;
                break;
            case NOISE:
                if (phase < f / BOOPIE_SOUND_RATE) {   /* a new value each cycle */
                    noise ^= noise << 13;
                    noise ^= noise >> 17;
                    noise ^= noise << 5;
                }
                v = (noise & 1) ? 1 : -1;
                break;
            default:
                v = phase < 0.5 ? 1 : -1;
                break;
            }
            /* A quick attack, then a decay to 40 % by the note's end; no clicks at the edges. */
            double env = (i < 48 ? i / 48.0 : 1.0) * (1.0 - 0.6 * k) * (len - i < 48 ? (len - i) / 48.0 : 1.0);
            out[n] = (int16_t)(v * env * note->vol * 90.0);
        }
    }
    return n;
}

size_t boopie_sound_render(boopie_sound_t s, int16_t *out, size_t max)
{
    if ((int)s < 0 || s >= BOOPIE_SOUND_COUNT || !out) {
        return 0;
    }
    char name[48];
    snprintf(name, sizeof name, "sounds/%s.wav", SOUNDS[s].key);
    size_t n = 0;
    const uint8_t *wav = boopie_assets_get(name, &n);
    if (wav) {
        size_t frames = from_wav(wav, n, out, max);
        if (frames) {
            return frames;
        }
    }
    return synth(SOUNDS[s].notes, out, max);
}
