/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * muse_pixel_* for the UI, passing each call to the character on screen.
 * Built into the muse component (it calls the official renderer there) from
 * components/muse/CMakeLists.txt and the simulator's.
 */

#ifndef ESP_PLATFORM
#define _POSIX_C_SOURCE 200809L   /* localtime_r in the simulator's strict C11 */
#endif

#include "boopie_avatar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_pet.h"
#include "muse_pixel.h"
#include "muse_state.h"

#include <time.h>

#ifdef ESP_PLATFORM
#include "boopie_clock.h"
#include "boopie_imu.h"
#include "esp_log.h"
#include "nvs.h"
#endif

/* The official renderer, avatar/muse_pixel.c, built with these names. */
uint32_t jolly_pixel_accent(muse_mode_t mode);
void jolly_pixel_render(const muse_pose_t *pose);
void jolly_pixel_set_size(int px);
void jolly_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1);
/* Boopie's additions to it: a pet expression, and the overlays that change the
 * face. A custom avatar (components/muse/avatar/muse_pixel.c) won't have it,
 * and draws the core expressions only. */
__attribute__((weak)) void jolly_pixel_set_extra(int pet, float pet_t, bool blush, bool wide)
{
    (void)pet;
    (void)pet_t;
    (void)blush;
    (void)wide;
}

/* Its frame, to compose the background and overlays over; a custom avatar
 * without it is shown as it draws itself. */
__attribute__((weak)) bool jolly_pixel_frame(const uint8_t **fb, const uint16_t **palette, uint32_t *bg_mask)
{
    (void)fb;
    (void)palette;
    (void)bg_mask;
    return false;
}

static bool s_muse_composed;   /* the last Muse frame went through boopie_pixel_compose */

#define LOW_BATTERY_PCT 15

static bool s_loaded;
static void ensure_loaded(void);
static void save(void);
static void apply(void);
static int s_avatar = BOOPIE_AVATAR_MUSE;
static uint32_t s_colour[BOOPIE_AVATAR_COUNT];
static boopie_scene_t s_scene = BOOPIE_SCENE_DEFAULT;
static int s_worn[BOOPIE_AVATAR_COUNT];   /* the skin each character wears, or -1 */
static uint32_t s_owned;                  /* bit per skin index */

/* The pet, ticked from the frames, and what it shows while idle. */
static boopie_pet_t s_pet_state;
static boopie_expr_t s_pet_mood = BOOPIE_EXPR_IDLE;
static bool s_pet_resumed, s_pet_dirty;
static float s_pet_ticked = -100, s_pet_saved = -1;
static muse_mode_t s_last_mode = MUSE_MODE_COUNT;
static float s_flash_until[BOOPIE_OVERLAY_COUNT];   /* overlays shown for a moment */
static float s_now;                                  /* pose.t of the last frame */
static volatile int s_pet = BOOPIE_EXPR_IDLE;
static volatile uint8_t s_overlays;            /* the reactions set */
static float s_pet_since = -1;                 /* when the pet expression began, in pose.t */
static boopie_expr_t s_pet_was = BOOPIE_EXPR_IDLE;
static float s_overlay_since[BOOPIE_OVERLAY_COUNT];
static uint8_t s_shown_overlays;               /* last frame's, to time new ones from */
static float s_happy_since = -1;
static volatile int s_react = BOOPIE_EXPR_IDLE;   /* a short reaction asked for */
static volatile float s_react_secs;
static int s_reacting = BOOPIE_EXPR_IDLE;         /* the one showing */
static float s_react_since, s_react_until;
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

static int scene_from_key(const char *key)
{
    for (int i = 0; key && i < BOOPIE_SCENE_COUNT; i++) {
        if (strcmp(boopie_scene_key((boopie_scene_t)i), key) == 0) {
            return i;
        }
    }
    return -1;
}

boopie_scene_t boopie_avatar_scene(void)
{
    ensure_loaded();
    return s_scene;
}

void boopie_avatar_set_scene(boopie_scene_t scene)
{
    ensure_loaded();
    if ((int)scene < 0 || scene >= BOOPIE_SCENE_COUNT || scene == s_scene) {
        return;
    }
    s_scene = scene;
    save();
}

int boopie_avatar_skin(void)
{
    ensure_loaded();
    return s_worn[s_avatar];
}

bool boopie_avatar_owns(int skin)
{
    ensure_loaded();
    return skin >= 0 && skin < boopie_skin_count() && (s_owned >> skin & 1u);
}

bool boopie_avatar_wear(int skin, const char **error)
{
    ensure_loaded();
    if (skin >= 0 && (int)boopie_skin_character(skin) + 1 != s_avatar) {
        *error = "that skin is for another character";
        return false;
    }
    if (skin >= 0 && !boopie_avatar_owns(skin)) {
        *error = "that skin isn't bought yet";
        return false;
    }
    s_worn[s_avatar] = skin < 0 ? -1 : skin;
    apply();
    save();
    return true;
}

bool boopie_avatar_buy(int skin, const char **error)
{
    ensure_loaded();
    if (skin < 0 || skin >= boopie_skin_count()) {
        *error = "no such skin";
        return false;
    }
    if (!boopie_avatar_owns(skin)) {
        uint32_t price = (uint32_t)boopie_skin_price(skin);
        if (s_pet_state.stars < price) {
            *error = "not enough stars";
            return false;
        }
        s_pet_state.stars -= price;
        s_owned |= 1u << skin;
    }
    s_worn[boopie_skin_character(skin) + 1] = skin;
    apply();
    save();
    return true;
}

/* The background shown: the one chosen, else the worn skin's own. */
static boopie_scene_t shown_scene(void)
{
    if (s_scene == BOOPIE_SCENE_DEFAULT && s_avatar != BOOPIE_AVATAR_MUSE && s_worn[s_avatar] >= 0) {
        return boopie_skin_scene(s_worn[s_avatar]);
    }
    return s_scene;
}

bool boopie_avatar_recolourable(int avatar)
{
    return avatar > BOOPIE_AVATAR_MUSE && avatar < BOOPIE_AVATAR_COUNT;
}

static void apply(void)
{
    if (s_avatar != BOOPIE_AVATAR_MUSE) {
        boopie_pixel_set_skin(s_worn[s_avatar]);
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
    nvs_get_u32(h, "owned", &s_owned);
    for (int i = 1; i < BOOPIE_AVATAR_COUNT; i++) {
        char wk[16], sk[24];
        size_t sn = sizeof sk;
        snprintf(wk, sizeof wk, "w_%s", boopie_avatar_key(i));
        if (nvs_get_str(h, wk, sk, &sn) == ESP_OK) {
            int k = boopie_skin_from_key(sk);
            s_worn[i] = k >= 0 && (s_owned >> k & 1u) ? k : -1;
        }
    }
    size_t pn = sizeof s_pet_state;
    boopie_pet_t saved;
    if (nvs_get_blob(h, "pet", &saved, &pn) == ESP_OK && pn == sizeof saved && saved.version == BOOPIE_PET_VERSION) {
        s_pet_state = saved;
    }
    n = sizeof key;
    if (nvs_get_str(h, "scene", key, &n) == ESP_OK) {
        int sc = scene_from_key(key);
        s_scene = sc >= 0 ? (boopie_scene_t)sc : BOOPIE_SCENE_DEFAULT;
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
    nvs_set_str(h, "scene", boopie_scene_key(s_scene));
    nvs_set_u32(h, "owned", s_owned);
    for (int i = 1; i < BOOPIE_AVATAR_COUNT; i++) {
        char wk[16];
        snprintf(wk, sizeof wk, "w_%s", boopie_avatar_key(i));
        nvs_set_str(h, wk, s_worn[i] >= 0 ? boopie_skin_key(s_worn[i]) : "");
    }
    nvs_set_blob(h, "pet", &s_pet_state, sizeof s_pet_state);
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
    const char *xp = getenv("BOOPIE_PET_XP");
    if (xp) {
        s_pet_state.xp = (uint32_t)strtoul(xp, NULL, 10);
    }
    if (getenv("BOOPIE_PET_HUNGRY")) {   /* hungry from the start, as in the day */
        s_pet_state.hungry = 1;
        s_pet_state.hungry_since = (int64_t)time(NULL);
    }
    int skin = boopie_skin_from_key(getenv("BOOPIE_SKIN"));
    if (skin >= 0) {   /* owned and worn by its character */
        s_owned |= 1u << skin;
        s_worn[boopie_skin_character(skin) + 1] = skin;
    }
    int sc = scene_from_key(getenv("BOOPIE_SCENE"));
    if (sc >= 0) {
        s_scene = (boopie_scene_t)sc;
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

#ifdef ESP_PLATFORM
static void on_shake(void)
{
    boopie_avatar_react(BOOPIE_EXPR_DIZZY, 3.0f);
}
#endif

static void ensure_loaded(void)
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;
#ifdef ESP_PLATFORM
    boopie_imu_on_shake(on_shake);   /* shaken: dizzy for a moment */
#endif
    for (int i = 0; i < BOOPIE_AVATAR_COUNT; i++) {
        s_colour[i] = BOOPIE_COLOUR_DEFAULT;
        s_worn[i] = -1;
    }
    boopie_pet_init(&s_pet_state);
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

void boopie_avatar_react(boopie_expr_t expr, float seconds)
{
    s_react_secs = seconds;
    s_react = boopie_expr_valid(expr) && !boopie_expr_is_core(expr) ? expr : BOOPIE_EXPR_IDLE;
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
                           const char *scene, const char *skin, bool on, const char **error)
{
    int a = -1, sc = -1, sk = -2;
    if (skin) {
        sk = strcmp(skin, "none") == 0 ? -1 : boopie_skin_from_key(skin);
        if (sk == -1 && strcmp(skin, "none") != 0) {
            *error = "unknown skin";
            return false;
        }
        if (sk >= 0 && !boopie_avatar_owns(sk)) {
            *error = "that skin isn't bought yet";
            return false;
        }
    }
    if (scene && (sc = scene_from_key(scene)) < 0) {
        *error = "unknown background";
        return false;
    }
    if (sc > 0 && !boopie_avatar_unlocked(BOOPIE_UNLOCK_SCENE, sc, NULL)) {
        *error = "that background unlocks at a higher level";
        return false;
    }
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
    if (sc >= 0) {
        boopie_avatar_set_scene((boopie_scene_t)sc);
    }
    if (sk >= 0 && (int)boopie_skin_character(sk) + 1 != boopie_avatar_current()) {
        boopie_avatar_select((int)boopie_skin_character(sk) + 1);   /* wearing it means showing that character */
    }
    if (sk != -2) {
        return boopie_avatar_wear(sk, error);
    }
    return true;
}

/* ---- the pet ---- */

/* Local time now, if known: days since 1970 and the minute of the day. */
static bool local_now(int64_t *now, int32_t *day, int *minute)
{
    struct tm tm;
#ifdef ESP_PLATFORM
    if (!boopie_clock_local(&tm)) {
        return false;
    }
#else
    time_t t = time(NULL);
    localtime_r(&t, &tm);
#endif
    *now = (int64_t)time(NULL);
    /* Days since 1970 by the local date, so a day turns at local midnight. */
    int y = tm.tm_year + 1900 - 1;
    *day = 365 * (y - 1969) + (y / 4 - 1969 / 4) - (y / 100 - 1969 / 100) + (y / 400 - 1969 / 400) + tm.tm_yday;
    *minute = tm.tm_hour * 60 + tm.tm_min;
    return true;
}

static void flash_overlay(boopie_overlay_t o, float secs)
{
    s_flash_until[o] = s_now + secs;
}

/* Show what a pet call earned: eating, a level-up. */
static void show_event(const boopie_pet_event_t *ev)
{
    if (ev->fed) {
        boopie_avatar_react(BOOPIE_EXPR_EATING, 3.0f);
        flash_overlay(BOOPIE_OVERLAY_HEARTS, 4.5f);
    }
    if (ev->levels > 0) {
        flash_overlay(BOOPIE_OVERLAY_CONFETTI, 4.0f);
        muse_state_set_caption("升级啦！Lv %d", boopie_pet_level(s_pet_state.xp, NULL, NULL));
    }
    if (ev->xp || ev->stars || ev->fed) {
        s_pet_dirty = true;
    }
}

static void pet_tick(void)
{
    int64_t now;
    int32_t day;
    int minute;
    bool known = local_now(&now, &day, &minute);
    if (known && !s_pet_resumed) {
        boopie_pet_resume(&s_pet_state, now);   /* time powered off doesn't count */
        s_pet_resumed = true;
    }
    boopie_pet_event_t ev = { 0 };
    s_pet_mood = boopie_pet_tick(&s_pet_state, known, now, day, minute, muse_state_idle_secs(), &ev);
    show_event(&ev);
    if (s_pet_dirty || s_now - s_pet_saved > 600) {   /* now and then, and after a change */
        s_pet_dirty = false;
        s_pet_saved = s_now;
        save();
    }
}

bool boopie_avatar_tap(int gx, int gy)
{
    ensure_loaded();
    bool on_food = gx >= BOOPIE_FOOD_X0 && gx <= BOOPIE_FOOD_X1 && gy >= BOOPIE_FOOD_Y0 && gy <= BOOPIE_FOOD_Y1;
    if (s_pet_state.hungry && !on_food) {
        return false;   /* a poke; only the bowl feeds it */
    }
    int64_t now;
    int32_t day;
    int minute;
    if (!local_now(&now, &day, &minute)) {
        now = (int64_t)time(NULL);
    }
    boopie_pet_event_t ev = { 0 };
    bool fed = boopie_pet_tap(&s_pet_state, now, &ev);
    if (fed) {
        s_pet_mood = BOOPIE_EXPR_IDLE;
    }
    show_event(&ev);
    return fed;
}

void boopie_avatar_pet_status(boopie_pet_status_t *out)
{
    ensure_loaded();
    out->level = boopie_pet_level(s_pet_state.xp, &out->xp_into, &out->xp_need);
    out->xp = s_pet_state.xp;
    out->stars = s_pet_state.stars;
    out->hungry = s_pet_state.hungry;
    out->mood = s_pet_mood;
}

bool boopie_avatar_unlocked(boopie_unlock_kind_t kind, int index, int *level)
{
    ensure_loaded();
    int need = boopie_pet_unlock_level(kind, index);
    if (level) {
        *level = need;
    }
    return need > 0 && boopie_pet_level(s_pet_state.xp, NULL, NULL) >= need;
}

/* ---- muse_pixel.h ---- */

/* What our characters show for a Muse mode: the pet expression while idle. */
static boopie_expr_t shown(muse_mode_t mode)
{
    if (mode == MUSE_MODE_IDLE && s_reacting != BOOPIE_EXPR_IDLE) {
        return (boopie_expr_t)s_reacting;
    }
    if (mode == MUSE_MODE_IDLE && s_pet != BOOPIE_EXPR_IDLE) {
        return (boopie_expr_t)s_pet;
    }
    if (mode == MUSE_MODE_IDLE && s_pet_mood != BOOPIE_EXPR_IDLE) {
        return s_pet_mood;
    }
    return (boopie_expr_t)mode;
}

uint32_t muse_pixel_accent(muse_mode_t mode)
{
    ensure_loaded();
    boopie_expr_t e = shown(mode);
    if (s_avatar == BOOPIE_AVATAR_MUSE && e == (boopie_expr_t)mode) {
        return jolly_pixel_accent(mode);
    }
    return boopie_pixel_accent(e);
}

void muse_pixel_set_size(int px)
{
    jolly_pixel_set_size(px);
    boopie_pixel_set_size(px);
}

void muse_pixel_scale(uint16_t *dst, int stride_px, int x0, int x1, int y0, int y1)
{
    if (s_avatar == BOOPIE_AVATAR_MUSE) {
        if (s_muse_composed) {
            boopie_pixel_scale(dst, stride_px, x0, x1, y0, y1);
        } else {
            jolly_pixel_scale(dst, stride_px, x0, x1, y0, y1);
        }
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

#ifdef ESP_PLATFORM
    boopie_imu_keepalive();
#endif
    s_now = p->t;
    if (p->t - s_pet_ticked >= 2.0f) {
        s_pet_ticked = p->t;
        pet_tick();
    }
    if (p->mode == MUSE_MODE_SPEAKING && s_last_mode != MUSE_MODE_SPEAKING && s_last_mode != MUSE_MODE_COUNT) {
        boopie_pet_event_t ev = { 0 };   /* it was spoken to */
        boopie_pet_talked(&s_pet_state, &ev);
        show_event(&ev);
    }
    s_last_mode = p->mode;
    if (s_react != BOOPIE_EXPR_IDLE) {   /* a new reaction */
        s_reacting = s_react;
        s_react = BOOPIE_EXPR_IDLE;
        s_react_since = p->t;
        s_react_until = p->t + s_react_secs;
    } else if (s_reacting != BOOPIE_EXPR_IDLE && p->t >= s_react_until) {
        s_reacting = BOOPIE_EXPR_IDLE;
    }

    boopie_pixel_pose_t bp = { .level = p->level, .dt = dt };
    if (s_happy_since >= 0) {
        bp.expr = BOOPIE_EXPR_HAPPY;
        bp.t = p->t - s_happy_since;
    } else if (p->mode == MUSE_MODE_IDLE && s_reacting != BOOPIE_EXPR_IDLE) {
        bp.expr = (boopie_expr_t)s_reacting;
        bp.t = p->t - s_react_since;
    } else if (p->mode == MUSE_MODE_IDLE && shown(p->mode) != BOOPIE_EXPR_IDLE) {
        boopie_expr_t pet = shown(p->mode);
        if (s_pet_since < 0 || pet != s_pet_was) {
            s_pet_since = p->t;
            s_pet_was = pet;
        }
        bp.expr = pet;
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
    if (s_pet_state.hungry && p->mode == MUSE_MODE_IDLE && s_happy_since < 0) {
        on |= BOOPIE_OVERLAY_BIT(BOOPIE_OVERLAY_FOOD);   /* the bowl to tap */
    }
    for (int o = 0; o < BOOPIE_OVERLAY_COUNT; o++) {
        if (p->t < s_flash_until[o]) {
            on |= BOOPIE_OVERLAY_BIT(o);
        }
    }
    for (int o = 0; o < BOOPIE_OVERLAY_COUNT; o++) {
        uint8_t bit = BOOPIE_OVERLAY_BIT(o);
        if ((on & bit) && !(s_shown_overlays & bit)) {
            s_overlay_since[o] = p->t;
        }
        bp.overlay_t[o] = p->t - s_overlay_since[o];
    }
    s_shown_overlays = on;
    bp.overlays = on;
    bp.scene = p->mode == MUSE_MODE_OFF ? BOOPIE_SCENE_DEFAULT : shown_scene();
    bp.scene_t = p->t;
    if (s_avatar == BOOPIE_AVATAR_MUSE) {
        /* Muse's own renderer draws the expression, a pet one included, and
         * the face changes of two overlays; the overlay icons go on a layer
         * laid over it as it's scaled. */
        bool pet = bp.expr >= BOOPIE_EXPR_HUNGRY;
        jolly_pixel_set_extra(pet ? (int)bp.expr : BOOPIE_EXPR_IDLE, (float)bp.t,
                              on & BOOPIE_OVERLAY_BIT(BOOPIE_OVERLAY_BLUSH),
                              on & BOOPIE_OVERLAY_BIT(BOOPIE_OVERLAY_SURPRISE));
        jolly_pixel_render(p);
        const uint8_t *fb;
        const uint16_t *palette;
        uint32_t bg;
        s_muse_composed = jolly_pixel_frame(&fb, &palette, &bg);
        if (s_muse_composed) {
            boopie_pixel_compose(fb, palette, bg, &bp);
        }
        return;
    }
    boopie_pixel_render(&bp);
}
