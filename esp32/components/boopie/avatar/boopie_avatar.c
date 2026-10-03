/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * muse_pixel_* for the UI, passing each call to the character on screen.
 * Built into the muse component (it calls the official renderer there) from
 * components/muse/CMakeLists.txt and the simulator's.
 */

#include "boopie_avatar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "muse_pixel.h"
#include "muse_state.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "nvs.h"
#endif

/* The official renderer, avatar/muse_pixel.c, built with these names. */
uint32_t jolly_pixel_accent(muse_mode_t mode);
void jolly_pixel_render(const muse_pose_t *pose);
void jolly_pixel_set_size(int px);
void jolly_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1);

#define LOW_BATTERY_PCT 15

static bool s_loaded;
static int s_avatar = BOOPIE_AVATAR_MUSE;
static uint32_t s_colour[BOOPIE_AVATAR_COUNT];
static volatile int s_pet = BOOPIE_EXPR_IDLE;
static volatile uint8_t s_overlays;            /* the reactions set */
static float s_pet_since = -1;                 /* when the pet expression began, in pose.t */
static float s_overlay_since[BOOPIE_OVERLAY_COUNT];
static uint8_t s_shown_overlays;               /* last frame's, to time new ones from */
static float s_happy_since = -1;
static float s_last_t = -1;

const char *boopie_avatar_key(int avatar)
{
    if (avatar == BOOPIE_AVATAR_MUSE) {
        return "muse";
    }
    return boopie_char_key((boopie_char_t)(avatar - 1));
}

const char *boopie_avatar_name(int avatar)
{
    if (avatar == BOOPIE_AVATAR_MUSE) {
        return "Muse";
    }
    return boopie_char_name((boopie_char_t)(avatar - 1));
}

static int from_key(const char *key)
{
    for (int i = 0; key && i < BOOPIE_AVATAR_COUNT; i++) {
        if (strcmp(boopie_avatar_key(i), key) == 0) {
            return i;
        }
    }
    return -1;
}

bool boopie_avatar_recolourable(int avatar)
{
    return avatar > BOOPIE_AVATAR_MUSE && avatar < BOOPIE_AVATAR_COUNT;
}

static void apply(void)
{
    if (s_avatar != BOOPIE_AVATAR_MUSE) {
        boopie_pixel_set_character((boopie_char_t)(s_avatar - 1), s_colour[s_avatar]);
    }
}

/* ---- keeping the choice ---- */

#ifdef ESP_PLATFORM
static const char *TAG = "boopie_avatar";
#define NS "boopie"

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return;            /* nothing saved yet */
    }
    char key[16];
    size_t n = sizeof key;
    if (nvs_get_str(h, "avatar", key, &n) == ESP_OK) {
        int a = from_key(key);
        s_avatar = a >= 0 ? a : BOOPIE_AVATAR_MUSE;
    }
    for (int i = 1; i < BOOPIE_AVATAR_COUNT; i++) {
        char ck[16];
        snprintf(ck, sizeof ck, "c_%s", boopie_avatar_key(i));
        nvs_get_u32(h, ck, &s_colour[i]);
    }
    nvs_close(h);
    ESP_LOGI(TAG, "avatar %s", boopie_avatar_key(s_avatar));
}

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "can't save the avatar");
        return;
    }
    nvs_set_str(h, "avatar", boopie_avatar_key(s_avatar));
    for (int i = 1; i < BOOPIE_AVATAR_COUNT; i++) {
        char ck[16];
        snprintf(ck, sizeof ck, "c_%s", boopie_avatar_key(i));
        nvs_set_u32(h, ck, s_colour[i]);
    }
    nvs_commit(h);
    nvs_close(h);
}
#else
/* The simulator: BOOPIE_AVATAR=gpt, BOOPIE_COLOUR=7fe3c4, and for screenshots
 * BOOPIE_PET=hungry, BOOPIE_OVERLAY=hearts. */
static void load(void)
{
    int a = from_key(getenv("BOOPIE_AVATAR"));
    if (a >= 0) {
        s_avatar = a;
    }
    const char *c = getenv("BOOPIE_COLOUR");
    if (c && s_avatar != BOOPIE_AVATAR_MUSE) {
        s_colour[s_avatar] = (uint32_t)strtoul(c, NULL, 16);
    }
    boopie_expr_t e;
    if (boopie_expr_from_name(getenv("BOOPIE_PET"), &e)) {
        s_pet = e;
    }
    boopie_overlay_t o;
    if (boopie_overlay_from_name(getenv("BOOPIE_OVERLAY"), &o)) {
        s_overlays |= BOOPIE_OVERLAY_BIT(o);
    }
}

static void save(void)
{
}
#endif

static void ensure_loaded(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
    for (int i = 0; i < BOOPIE_AVATAR_COUNT; i++) {
        s_colour[i] = BOOPIE_COLOUR_DEFAULT;
    }
    load();
    apply();
}

int boopie_avatar_current(void)
{
    ensure_loaded();
    return s_avatar;
}

void boopie_avatar_select(int avatar)
{
    ensure_loaded();
    if (avatar < 0 || avatar >= BOOPIE_AVATAR_COUNT || avatar == s_avatar) {
        return;
    }
    s_avatar = avatar;
    apply();
    save();
}

uint32_t boopie_avatar_colour(void)
{
    ensure_loaded();
    return s_colour[s_avatar];
}

void boopie_avatar_set_colour(uint32_t rgb)
{
    ensure_loaded();
    if (!boopie_avatar_recolourable(s_avatar)) {
        return;
    }
    s_colour[s_avatar] = rgb == BOOPIE_COLOUR_DEFAULT ? rgb : (rgb & 0xffffff);
    apply();
    save();
}

void boopie_avatar_set_pet(boopie_expr_t expr)
{
    s_pet = boopie_expr_valid(expr) && !boopie_expr_is_core(expr) ? expr : BOOPIE_EXPR_IDLE;
    s_pet_since = -1;
}

boopie_expr_t boopie_avatar_pet(void)
{
    return (boopie_expr_t)s_pet;
}

void boopie_avatar_set_overlay(boopie_overlay_t overlay, bool on)
{
    if ((int)overlay < 0 || overlay >= BOOPIE_OVERLAY_COUNT) {
        return;
    }
    if (on) {
        s_overlays |= BOOPIE_OVERLAY_BIT(overlay);
    } else {
        s_overlays &= (uint8_t)~BOOPIE_OVERLAY_BIT(overlay);
    }
}

bool boopie_avatar_command(const char *avatar, const char *colour, const char *pet, const char *reaction,
                           bool on, const char **error)
{
    int a = -1;
    uint32_t rgb = 0;
    boopie_expr_t e = BOOPIE_EXPR_IDLE;
    boopie_overlay_t o = BOOPIE_OVERLAY_COUNT;
    /* Check everything before changing anything. */
    if (avatar && (a = from_key(avatar)) < 0) {
        *error = "unknown avatar";
        return false;
    }
    if (colour) {
        char *end = NULL;
        if (strcmp(colour, "default") == 0) {
            rgb = BOOPIE_COLOUR_DEFAULT;
        } else if (strlen(colour) == 6) {
            rgb = (uint32_t)strtoul(colour, &end, 16);
        }
        if (rgb != BOOPIE_COLOUR_DEFAULT && (!end || *end != '\0')) {
            *error = "colour is RRGGBB or \"default\"";
            return false;
        }
        if (!boopie_avatar_recolourable(a >= 0 ? a : boopie_avatar_current())) {
            *error = "Muse keeps its own colours";
            return false;
        }
    }
    if (pet && !boopie_expr_from_name(pet, &e)) {
        *error = "unknown expression";
        return false;
    }
    if (reaction && (!boopie_overlay_from_name(reaction, &o) || o >= BOOPIE_OVERLAY_LOW_BATTERY)) {
        *error = "unknown reaction";   /* the battery ones follow the battery */
        return false;
    }
    if (a >= 0) {
        boopie_avatar_select(a);
    }
    if (colour) {
        boopie_avatar_set_colour(rgb);
    }
    if (pet) {
        boopie_avatar_set_pet(e);
    }
    if (reaction) {
        boopie_avatar_set_overlay(o, on);
    }
    return true;
}

/* ---- muse_pixel.h ---- */

/* What our characters show for a Muse mode: the pet expression while idle. */
static boopie_expr_t shown(muse_mode_t mode)
{
    if (mode == MUSE_MODE_IDLE && s_pet != BOOPIE_EXPR_IDLE) {
        return (boopie_expr_t)s_pet;
    }
    return (boopie_expr_t)mode;
}

uint32_t muse_pixel_accent(muse_mode_t mode)
{
    ensure_loaded();
    if (s_avatar == BOOPIE_AVATAR_MUSE) {
        return jolly_pixel_accent(mode);
    }
    return boopie_pixel_accent(shown(mode));
}

void muse_pixel_set_size(int px)
{
    jolly_pixel_set_size(px);
    boopie_pixel_set_size(px);
}

void muse_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1)
{
    if (s_avatar == BOOPIE_AVATAR_MUSE) {
        jolly_pixel_scale(dst, stride_px, x0, x1, y0, y1);
    } else {
        boopie_pixel_scale(dst, stride_px, x0, x1, y0, y1);
    }
}

static uint8_t device_overlays(void)
{
    muse_power_t pw = muse_state_power();
    if (pw.charging) {
        return BOOPIE_OVERLAY_BIT(BOOPIE_OVERLAY_CHARGING);
    }
    if (pw.battery_pct >= 0 && pw.battery_pct <= LOW_BATTERY_PCT && !pw.usb) {
        return BOOPIE_OVERLAY_BIT(BOOPIE_OVERLAY_LOW_BATTERY);
    }
    return 0;
}

void muse_pixel_render(const muse_pose_t *p)
{
    ensure_loaded();
    float dt = s_last_t >= 0 ? p->t - s_last_t : 0;
    s_last_t = p->t;

    if (p->happy > 0 && s_happy_since < 0) {
        s_happy_since = p->t;
    } else if (p->happy <= 0) {
        s_happy_since = -1;
    }

    if (s_avatar == BOOPIE_AVATAR_MUSE) {
        /* Muse's own character draws the core expressions only: a pet one
         * shows its fallback, HAPPY as the official pet reaction. */
        muse_pose_t q = *p;
        if (p->mode == MUSE_MODE_IDLE && s_pet != BOOPIE_EXPR_IDLE) {
            boopie_expr_t e = boopie_expr_resolve((boopie_expr_t)s_pet, BOOPIE_EXPR_SET_CORE);
            if (e == BOOPIE_EXPR_HAPPY) {
                q.happy = 1;
            } else if (e != BOOPIE_EXPR_OFF) {   /* OFF would fade the screen out */
                q.mode = (muse_mode_t)e;
            }
        }
        jolly_pixel_render(&q);
        return;
    }

    boopie_pixel_pose_t bp = { .level = p->level, .dt = dt };
    if (s_happy_since >= 0) {
        bp.expr = BOOPIE_EXPR_HAPPY;
        bp.t = p->t - s_happy_since;
    } else if (p->mode == MUSE_MODE_IDLE && s_pet != BOOPIE_EXPR_IDLE) {
        if (s_pet_since < 0) {
            s_pet_since = p->t;
        }
        bp.expr = (boopie_expr_t)s_pet;
        float since = p->t - s_pet_since;
        bp.t = since < p->mode_t ? since : p->mode_t;
    } else {
        bp.expr = (boopie_expr_t)p->mode;
        bp.t = p->mode_t;
    }
    if (p->mode != MUSE_MODE_LISTENING && p->mode != MUSE_MODE_SPEAKING) {
        bp.level = -1;
    }

    uint8_t on = s_overlays | device_overlays();
    for (int o = 0; o < BOOPIE_OVERLAY_COUNT; o++) {
        uint8_t bit = BOOPIE_OVERLAY_BIT(o);
        if ((on & bit) && !(s_shown_overlays & bit)) {
            s_overlay_since[o] = p->t;
        }
        bp.overlay_t[o] = p->t - s_overlay_since[o];
    }
    s_shown_overlays = on;
    bp.overlays = on;
    boopie_pixel_render(&bp);
}
