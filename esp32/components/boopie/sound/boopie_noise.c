/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_noise.h"

#include <math.h>
#include <stdatomic.h>

#include "boopie_sound.h"

#define LEVEL 5200.0f   /* about -16 dBFS: under the voice, which the volume setting scales too */

/* What's asked for: set by any task, read by the voice task. Times are ms;
 * the end is 0 for none. A generation number tells the renderer to start over. */
static atomic_int s_kind = BOOPIE_NOISE_WHITE;
static atomic_llong s_start, s_end, s_stop_at;   /* s_stop_at: a stop's fade began (0: none) */
static atomic_int s_gen;
static atomic_bool s_on;

void boopie_noise_play(boopie_noise_t kind, int minutes, int64_t now_ms)
{
    if ((int)kind < 0 || kind >= BOOPIE_NOISE_COUNT) {
        kind = BOOPIE_NOISE_WHITE;
    }
    atomic_store(&s_kind, (int)kind);
    atomic_store(&s_start, now_ms);
    atomic_store(&s_end, minutes > 0 ? now_ms + (int64_t)minutes * 60000 : 0);
    atomic_store(&s_stop_at, 0);
    atomic_fetch_add(&s_gen, 1);
    atomic_store(&s_on, true);
}

void boopie_noise_stop(int64_t now_ms)
{
    if (atomic_load(&s_on) && !atomic_load(&s_stop_at)) {
        atomic_store(&s_stop_at, now_ms);
    }
}

const char *boopie_noise_name(boopie_noise_t kind)
{
    static const char *const NAMES[BOOPIE_NOISE_COUNT] = { "白噪音", "粉红噪音", "雨声", "海浪" };
    return (int)kind >= 0 && kind < BOOPIE_NOISE_COUNT ? NAMES[kind] : "";
}

/* When it falls silent: the end of its time, or of a stop's fade. 0: never. */
static int64_t silent_at(void)
{
    int64_t stop = atomic_load(&s_stop_at), end = atomic_load(&s_end);
    int64_t faded = stop ? stop + BOOPIE_NOISE_FADE_OUT_MS / 4 : 0;   /* a stop fades quicker */
    if (faded && (!end || faded < end)) {
        return faded;
    }
    return end;
}

bool boopie_noise_playing(int64_t now_ms, boopie_noise_t *kind, int *left_s)
{
    if (!atomic_load(&s_on)) {
        return false;
    }
    int64_t silent = silent_at();
    if (silent && now_ms >= silent) {
        return false;
    }
    if (kind) {
        *kind = (boopie_noise_t)atomic_load(&s_kind);
    }
    if (left_s) {
        *left_s = silent ? (int)((silent - now_ms + 999) / 1000) : -1;
    }
    return true;
}

/* ---- the voice task's side ---- */

typedef struct {
    int gen;
    uint32_t rng;
    float b0, b1, b2, brown;       /* pink and brown filters */
    float drop, drop_k;            /* rain: the drop sounding, its decay */
    float hp;                      /* rain: a drop's high pass */
    float wave_t, wave_len;        /* waves: where in this wave, its length (s) */
    float lp;                      /* waves: a soft low pass */
} gen_t;

static gen_t s_g = { .gen = -1 };

static float white(gen_t *g)
{
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return (float)(int32_t)g->rng / 2147483648.0f;   /* -1 .. 1 */
}

static float pink(gen_t *g, float w)
{
    /* Paul Kellet's economy filter: -3 dB an octave. */
    g->b0 = 0.99765f * g->b0 + w * 0.0990460f;
    g->b1 = 0.96300f * g->b1 + w * 0.2965164f;
    g->b2 = 0.57000f * g->b2 + w * 1.0526913f;
    return (g->b0 + g->b1 + g->b2 + w * 0.1848f) * 0.22f;
}

static float brown(gen_t *g, float w)
{
    g->brown = (g->brown + 0.02f * w) / 1.02f;
    return g->brown * 3.5f;
}

static float sample(gen_t *g, boopie_noise_t kind)
{
    const float dt = 1.0f / BOOPIE_SOUND_RATE;
    float w = white(g);
    switch (kind) {
    case BOOPIE_NOISE_PINK:
        return pink(g, w);
    case BOOPIE_NOISE_RAIN: {
        /* A soft hiss, and drops: about 40 a second, each a short tick that dies away. */
        float hiss = pink(g, w) * 0.6f;
        if ((g->rng >> 8) % (BOOPIE_SOUND_RATE / 40) == 0) {
            float a = 0.25f + 0.75f * (float)((g->rng >> 4) & 255) / 255.0f;
            g->drop = a * a;
            g->drop_k = 0.990f + 0.006f * (float)((g->rng >> 12) & 255) / 255.0f;
        }
        float n = white(g);
        float hp = n - g->hp;   /* drops are bright */
        g->hp = n;
        float d = g->drop * hp;
        g->drop *= g->drop_k;
        return hiss + d * 1.3f;
    }
    case BOOPIE_NOISE_WAVES: {
        /* Brown rumble swelling and falling, a wave every 7 to 11 seconds, with hiss at its crest. */
        g->wave_t += dt;
        if (g->wave_t >= g->wave_len) {
            g->wave_t = 0;
            g->wave_len = 7.0f + 4.0f * (float)((g->rng >> 9) & 1023) / 1023.0f;
        }
        float p = g->wave_t / g->wave_len;
        float swell = 0.5f - 0.5f * cosf(6.2831853f * p);
        swell = swell * sqrtf(swell);
        float rumble = brown(g, w);
        g->lp += 0.15f * (pink(g, w) - g->lp);
        return rumble * (0.25f + 0.75f * swell) + g->lp * swell * 0.8f;
    }
    default:
        return w * 0.55f;
    }
}

bool boopie_noise_render(int16_t *out, size_t n, int64_t now_ms)
{
    if (!atomic_load(&s_on)) {
        for (size_t i = 0; i < n; i++) {
            out[i] = 0;
        }
        return false;
    }
    int gen = atomic_load(&s_gen);
    if (gen != s_g.gen) {
        uint32_t seed = s_g.rng ? s_g.rng : 0x9e3779b9u;
        s_g = (gen_t){ .gen = gen, .rng = seed, .wave_len = 8.0f };
    }
    boopie_noise_t kind = (boopie_noise_t)atomic_load(&s_kind);
    int64_t start = atomic_load(&s_start), stop = atomic_load(&s_stop_at), end = atomic_load(&s_end);
    int64_t silent = silent_at();
    bool any = false;
    for (size_t i = 0; i < n; i++) {
        int64_t t = now_ms + (int64_t)(i * 1000 / BOOPIE_SOUND_RATE);
        float gain = 1;
        if (t - start < BOOPIE_NOISE_FADE_IN_MS) {
            gain = (float)(t - start) / BOOPIE_NOISE_FADE_IN_MS;
        }
        if (end && end - t < BOOPIE_NOISE_FADE_OUT_MS) {
            gain = fminf(gain, (float)(end - t) / BOOPIE_NOISE_FADE_OUT_MS);
        }
        if (stop) {
            gain = fminf(gain, 1 - (float)(t - stop) / (BOOPIE_NOISE_FADE_OUT_MS / 4));
        }
        if (silent && t >= silent) {
            gain = 0;
        }
        gain = gain < 0 ? 0 : gain;
        any |= gain > 0;
        float v = sample(&s_g, kind) * gain * LEVEL;
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    if (!any && silent && now_ms >= silent) {
        atomic_store(&s_on, false);
        return false;
    }
    return true;
}
