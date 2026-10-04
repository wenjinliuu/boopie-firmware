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
#include "boopie_sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_pet.h"
#include "boopie_season.h"
#include "boopie_garden.h"
#include "boopie_world.h"
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

/* A Muse skin's colours (NULL for its own); a custom avatar ignores them. */
__attribute__((weak)) void jolly_pixel_set_colours(const boopie_muse_colours_t *c)
{
    (void)c;
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

/* Where Muse's accessories go this frame (hat x, y; scarf x, y, width); a
 * custom avatar without it wears none. */
__attribute__((weak)) bool jolly_pixel_slots(float out[5])
{
    (void)out;
    return false;
}

static bool s_muse_composed;   /* the last Muse frame went through boopie_pixel_compose */

#define LOW_BATTERY_PCT 15

static bool s_loaded;
static void ensure_loaded(void);
static void save(void);
static void apply(void);
static bool local_date(int *year, int *month, int *mday, int32_t *day);
static int s_avatar = BOOPIE_AVATAR_BOOPIE;   /* Boopie, until one's chosen */
static uint32_t s_colour[BOOPIE_AVATAR_COUNT];
static boopie_scene_t s_scene = BOOPIE_SCENE_DEFAULT;
static int s_worn[BOOPIE_AVATAR_COUNT];   /* the skin each character wears, or -1 */
static uint32_t s_owned;                  /* bit per skin index */
static uint32_t s_acc[BOOPIE_AVATAR_COUNT];  /* the accessories each wears, BOOPIE_ACC_BIT()s */
static char s_name[BOOPIE_PET_NAME_MAX];  /* the pet's name, or "" for its character's */
static uint32_t s_best[BOOPIE_GAME_COUNT];   /* each game's best score */
static uint8_t s_brain = BOOPIE_BRAIN_MUSE;   /* the AI assistant: Muse, recommended, unless 小智 is chosen */
static uint8_t s_posture = 1;             /* 姿势感应: on unless turned off */
static float s_soothed_until = -1;        /* stroked or hugged till then, in pose.t */
static bool s_soothed_hug;
#ifdef ESP_PLATFORM
static uint8_t s_guided;                  /* the setup guide's been through */
#else
static uint8_t s_guided = 1;              /* the simulator: BOOPIE_GUIDE shows it */
#endif

/* The pet, ticked from the frames, and what it shows while idle. */
static boopie_pet_t s_pet_state;
static boopie_garden_t s_garden;   /* 小花园 */
/* 小窝's world: the day each chest was last opened, the slimes beaten, the day
 * each spot was picked, what's in the bag, the furniture bought and the
 * furniture put away. Kept in NVS as it is: only ever append. */
#define WOODS_CHESTS 4
#define WOODS_SPOTS 12
#define BAG_ITEMS 12
static struct {
    int32_t chest_day[WOODS_CHESTS];
    uint32_t slimes;
    int32_t spot_day[WOODS_SPOTS];
    uint16_t items[BAG_ITEMS];
    uint32_t furni_owned, furni_away;
    uint8_t seeds[16];   /* rare seeds bought, by boopie_plant_t */
    int32_t weather_day; /* the day the weather was told (by the AI), and what it is */
    uint8_t weather;
    uint8_t pad[3];
    int32_t mail_key;    /* the festival whose gift's been opened: year * 16 + festival */
    uint8_t birth_month, birth_day;   /* the pet's birthday (0: not set) */
    uint8_t hints;                    /* times 小窝's how-to has been said */
} s_woods;
static int s_sim_fest = -1;   /* the simulator's BOOPIE_FEST */
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

int boopie_avatar_of_skin(int skin)
{
    boopie_char_t c = boopie_skin_character(skin);
    return c == BOOPIE_SKIN_MUSE ? BOOPIE_AVATAR_MUSE : (int)c + 1;
}

bool boopie_avatar_wear(int skin, const char **error)
{
    ensure_loaded();
    if (skin >= 0 && boopie_avatar_of_skin(skin) != s_avatar) {
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

bool boopie_avatar_skin_on_sale(int skin)
{
    uint32_t limited = boopie_skin_limited(skin);
    boopie_fest_t f = boopie_avatar_festival();
    return !limited || (f != BOOPIE_FEST_NONE && (limited >> f & 1));
}

const char *boopie_avatar_skin_when(int skin)
{
    uint32_t limited = boopie_skin_limited(skin);
    return limited >> BOOPIE_FEST_SPRING & 1  ? "春节限定"
           : limited >> BOOPIE_FEST_HALLOWEEN & 1 ? "万圣节限定"
           : limited >> BOOPIE_FEST_XMAS & 1      ? "圣诞限定"
                                                  : "";
}

bool boopie_avatar_buy(int skin, const char **error)
{
    ensure_loaded();
    if (skin < 0 || skin >= boopie_skin_count()) {
        *error = "no such skin";
        return false;
    }
    if (!boopie_avatar_owns(skin)) {
        if (!boopie_avatar_skin_on_sale(skin)) {
            *error = "on sale only in its festival";
            return false;
        }
        uint32_t price = (uint32_t)boopie_skin_price(skin);
        if (s_pet_state.stars < price) {
            *error = "not enough stars";
            return false;
        }
        s_pet_state.stars -= price;
        s_owned |= 1u << skin;
    }
    s_worn[boopie_avatar_of_skin(skin)] = skin;
    apply();
    save();
    return true;
}

/* The background shown: the one chosen, else the worn skin's own. */
static boopie_scene_t shown_scene(void)
{
    if (s_scene == BOOPIE_SCENE_DEFAULT && s_worn[s_avatar] >= 0) {
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
    boopie_pixel_set_wear(s_acc[s_avatar]);
    if (s_avatar != BOOPIE_AVATAR_MUSE) {
        boopie_pixel_set_skin(s_worn[s_avatar]);
        boopie_pixel_set_character((boopie_char_t)(s_avatar - 1), s_colour[s_avatar]);
        return;
    }
    boopie_muse_colours_t c;
    jolly_pixel_set_colours(s_worn[s_avatar] >= 0 && boopie_skin_muse_colours(s_worn[s_avatar], &c) ? &c : NULL);
}

/* ---- keeping the choice ---- */

#ifdef ESP_PLATFORM
static const char *TAG = "boopie_avatar";
static const char *const GAME_KEYS[BOOPIE_GAME_COUNT] = {   /* NVS */
    [BOOPIE_GAME_WHACK] = "best_whack", [BOOPIE_GAME_CATCH] = "best_catch", [BOOPIE_GAME_MAZE] = "best_maze",
    [BOOPIE_GAME_HOP] = "best_hop",
};
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
        s_avatar = a >= 0 ? a : BOOPIE_AVATAR_BOOPIE;
    }
    for (int i = 1; i < BOOPIE_AVATAR_COUNT; i++) {
        char ck[16];
        snprintf(ck, sizeof ck, "c_%s", boopie_avatar_key(i));
        nvs_get_u32(h, ck, &s_colour[i]);
    }
    nvs_get_u32(h, "owned", &s_owned);
    for (int i = 0; i < BOOPIE_AVATAR_COUNT; i++) {
        char wk[16], sk[24];
        size_t sn = sizeof sk;
        snprintf(wk, sizeof wk, "w_%s", boopie_avatar_key(i));
        if (nvs_get_str(h, wk, sk, &sn) == ESP_OK) {
            int k = boopie_skin_from_key(sk);
            s_worn[i] = k >= 0 && (s_owned >> k & 1u) ? k : -1;
        }
        snprintf(wk, sizeof wk, "a_%s", boopie_avatar_key(i));
        nvs_get_u32(h, wk, &s_acc[i]);
    }
    n = sizeof s_name;
    if (nvs_get_str(h, "name", s_name, &n) != ESP_OK) {
        s_name[0] = '\0';
    }
    for (int i = 0; i < BOOPIE_GAME_COUNT; i++) {
        nvs_get_u32(h, GAME_KEYS[i], &s_best[i]);
    }
    nvs_get_u8(h, "brain", &s_brain);
    nvs_get_u8(h, "guided", &s_guided);
    nvs_get_u8(h, "posture", &s_posture);
    size_t pn = sizeof s_pet_state;
    boopie_pet_t saved;
    if (nvs_get_blob(h, "pet", &saved, &pn) == ESP_OK) {
        boopie_pet_load(&s_pet_state, &saved, pn);   /* an older version is brought up to date */
    }
    boopie_garden_t garden;
    size_t gn = sizeof garden;
    if (nvs_get_blob(h, "garden", &garden, &gn) == ESP_OK) {
        boopie_garden_load(&s_garden, &garden, gn);
    }
    size_t wn = sizeof s_woods;
    nvs_get_blob(h, "world", &s_woods, &wn);   /* shorter, from an older build: the rest stays 0 */
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
    for (int i = 0; i < BOOPIE_AVATAR_COUNT; i++) {
        char wk[16];
        snprintf(wk, sizeof wk, "w_%s", boopie_avatar_key(i));
        nvs_set_str(h, wk, s_worn[i] >= 0 ? boopie_skin_key(s_worn[i]) : "");
        snprintf(wk, sizeof wk, "a_%s", boopie_avatar_key(i));
        nvs_set_u32(h, wk, s_acc[i]);
    }
    nvs_set_str(h, "name", s_name);
    for (int i = 0; i < BOOPIE_GAME_COUNT; i++) {
        nvs_set_u32(h, GAME_KEYS[i], s_best[i]);
    }
    nvs_set_u8(h, "brain", s_brain);
    nvs_set_u8(h, "guided", s_guided);
    nvs_set_u8(h, "posture", s_posture);
    nvs_set_blob(h, "pet", &s_pet_state, sizeof s_pet_state);
    nvs_set_blob(h, "garden", &s_garden, sizeof s_garden);
    nvs_set_blob(h, "world", &s_woods, sizeof s_woods);
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
        s_worn[boopie_avatar_of_skin(skin)] = skin;
    }
    const char *wear = getenv("BOOPIE_WEAR");   /* bow,scarf */
    while (wear && *wear) {
        char k[16];
        size_t len = strcspn(wear, ",");
        snprintf(k, sizeof k, "%.*s", (int)(len < sizeof k ? len : sizeof k - 1), wear);
        boopie_acc_t acc;
        if (boopie_acc_from_key(k, &acc)) {
            s_acc[s_avatar] |= BOOPIE_ACC_BIT(acc);
        }
        wear += len + (wear[len] == ',');
    }
    if (getenv("BOOPIE_GUIDE")) {
        s_guided = 0;
    }
    const char *garden = getenv("BOOPIE_GARDEN");
    if (garden) {
        /* Part grown: a sunflower in bud (damp), a tulip sprout gone dry, and an
         * empty pot, or with "bloom" a strawberry ready to pick. */
        int64_t now = (int64_t)time(NULL);
        boopie_garden_plant(&s_garden, 0, BOOPIE_PLANT_SUNFLOWER, now - 50 * 3600);
        s_garden.pots[0].damp_until = now + 3600;
        boopie_garden_plant(&s_garden, 1, BOOPIE_PLANT_TULIP, now - 40 * 3600);
        if (!strcmp(garden, "bloom")) {
            boopie_garden_plant(&s_garden, 2, BOOPIE_PLANT_STRAWBERRY, now - 130 * 3600);
            s_garden.pots[2].damp_until = now;
        }
        boopie_garden_update(&s_garden, now);
    }
    if (getenv("BOOPIE_FURNI")) {
        s_woods.furni_owned = (uint32_t)strtoul(getenv("BOOPIE_FURNI"), NULL, 0);   /* bought, and out */
    }
    if (getenv("BOOPIE_BAG")) {
        s_woods.items[0] = 3;   /* berries */
        s_woods.items[1] = 2;   /* mushrooms */
        s_woods.items[2] = 4;   /* shells */
        s_woods.items[3] = 1;   /* fish */
        s_woods.items[4] = 2;   /* cookies */
        s_woods.items[6] = 1;   /* a popper */
        s_woods.seeds[5] = 2;   /* pumpkin seeds */
    }
    boopie_weather_t wk;
    int year, month, mday;
    int32_t day;
    if (boopie_weather_from_key(getenv("BOOPIE_WEATHER"), &wk) && local_date(&year, &month, &mday, &day)) {
        s_woods.weather_day = day;   /* as if the AI had said it */
        s_woods.weather = (uint8_t)wk;
    }
    if (getenv("BOOPIE_FEST")) {
        s_sim_fest = atoi(getenv("BOOPIE_FEST"));   /* boopie_fest_t */
    }
    const char *name = getenv("BOOPIE_NAME");
    if (name) {
        snprintf(s_name, sizeof s_name, "%s", name);
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
    boopie_garden_init(&s_garden);
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

uint32_t boopie_avatar_accessories(void)
{
    ensure_loaded();
    return s_acc[s_avatar];
}

bool boopie_avatar_set_accessory(boopie_acc_t acc, bool on, const char **error)
{
    ensure_loaded();
    if ((int)acc < 0 || acc >= BOOPIE_ACC_COUNT) {
        *error = "unknown accessory";
        return false;
    }
    if (on && !boopie_avatar_unlocked(BOOPIE_UNLOCK_ACCESSORY, acc, NULL)) {
        *error = "that accessory unlocks at a higher level";
        return false;
    }
    uint32_t worn = s_acc[s_avatar];
    if (on && boopie_acc_is_hat(acc)) {   /* one hat at a time */
        for (int i = 0; i < BOOPIE_ACC_COUNT; i++) {
            if (boopie_acc_is_hat((boopie_acc_t)i)) {
                worn &= ~BOOPIE_ACC_BIT(i);
            }
        }
    }
    worn = on ? worn | BOOPIE_ACC_BIT(acc) : worn & ~BOOPIE_ACC_BIT(acc);
    if (worn != s_acc[s_avatar]) {
        s_acc[s_avatar] = worn;
        apply();
        save();
    }
    return true;
}

/* What each character's pet is called until the user names it: short, as
 * the AI says it. */
static const char *const PET_NAMES[BOOPIE_AVATAR_COUNT] = {
    [BOOPIE_AVATAR_MUSE] = "Muse",
    [BOOPIE_CHAR_BOOPIE + 1] = "布比",
    [BOOPIE_CHAR_GPT + 1] = "GPT",
    [BOOPIE_CHAR_CODEX + 1] = "Codex",
    [BOOPIE_CHAR_KLAUDE + 1] = "小克",
    [BOOPIE_CHAR_WHALE + 1] = "小鲸鱼",
    [BOOPIE_CHAR_DOUBAO + 1] = "豆包",
};
_Static_assert(BOOPIE_AVATAR_COUNT == 7, "a pet name for each character");

const char *boopie_avatar_pet_name(void)
{
    ensure_loaded();
    return s_name[0] ? s_name : PET_NAMES[s_avatar];
}

bool boopie_avatar_has_own_name(void)
{
    ensure_loaded();
    return s_name[0] != '\0';
}

/* Characters in UTF-8 text, or -1 if it isn't valid UTF-8 or has a control
 * character in it. */
static int utf8_chars(const char *s)
{
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; n++) {
        int len = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : (*p >> 3) == 30 ? 4 : 0;
        if (len == 0 || (len == 1 && *p < 0x20)) {
            return -1;
        }
        for (int i = 1; i < len; i++) {
            if ((p[i] & 0xc0) != 0x80) {
                return -1;
            }
        }
        p += len;
    }
    return n;
}

bool boopie_avatar_set_pet_name(const char *name, const char **error)
{
    ensure_loaded();
    while (name && *name == ' ') {
        name++;
    }
    size_t len = name ? strlen(name) : 0;
    while (len > 0 && name[len - 1] == ' ') {
        len--;
    }
    char clean[BOOPIE_PET_NAME_MAX];
    if (len >= sizeof clean) {
        *error = "that name is too long";
        return false;
    }
    memcpy(clean, name ? name : "", len);
    clean[len] = '\0';
    int chars = utf8_chars(clean);
    if (chars < 0 || chars > BOOPIE_PET_NAME_CHARS) {
        *error = chars < 0 ? "that name has characters it can't use" : "that name is too long";
        return false;
    }
    if (strcmp(clean, s_name) != 0) {
        memcpy(s_name, clean, len + 1);
        save();
    }
    return true;
}

bool boopie_avatar_command(const char *avatar, const char *colour, const char *pet, const char *reaction,
                           const char *scene, const char *skin, const char *accessory, bool on,
                           const char **error)
{
    int a = -1, sc = -1, sk = -2;
    boopie_acc_t acc = BOOPIE_ACC_COUNT;
    if (accessory && !boopie_acc_from_key(accessory, &acc)) {
        *error = "unknown accessory";
        return false;
    }
    if (accessory && on && !boopie_avatar_unlocked(BOOPIE_UNLOCK_ACCESSORY, acc, NULL)) {
        *error = "that accessory unlocks at a higher level";
        return false;
    }
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
    if (sk >= 0 && boopie_avatar_of_skin(sk) != boopie_avatar_current()) {
        boopie_avatar_select(boopie_avatar_of_skin(sk));   /* wearing it means showing that character */
    }
    if (sk != -2 && !boopie_avatar_wear(sk, error)) {
        return false;
    }
    if (accessory) {
        return boopie_avatar_set_accessory(acc, on, error);
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

static void flash_overlay(boopie_overlay_t o, float secs);

/* The local date, if known: year, month (1 to 12), day of the month, and days since 1970. */
static bool local_date(int *year, int *month, int *mday, int32_t *day)
{
    int64_t now;
    int minute;
    if (!local_now(&now, day, &minute)) {
        return false;
    }
    struct tm tm;
#ifdef ESP_PLATFORM
    boopie_clock_local(&tm);
#else
    time_t t = time(NULL);
    localtime_r(&t, &tm);
#endif
    *year = tm.tm_year + 1900;
    *month = tm.tm_mon + 1;
    *mday = tm.tm_mday;
    return true;
}

boopie_weather_t boopie_avatar_weather(void)
{
    ensure_loaded();
    int year, month, mday;
    int32_t day;
    if (!local_date(&year, &month, &mday, &day)) {
        return BOOPIE_WEATHER_SUNNY;
    }
    if (s_woods.weather_day == day && s_woods.weather < BOOPIE_WEATHER_COUNT) {
        return (boopie_weather_t)s_woods.weather;   /* as the AI said */
    }
    if (boopie_fest_on(year, month, mday) == BOOPIE_FEST_XMAS && mday >= 24 && mday <= 25) {
        return BOOPIE_WEATHER_SNOW;   /* a white Christmas, always */
    }
    return boopie_weather_on(day, month);
}

bool boopie_avatar_set_weather(boopie_weather_t w)
{
    ensure_loaded();
    int year, month, mday;
    int32_t day;
    if ((int)w < 0 || w >= BOOPIE_WEATHER_COUNT || !local_date(&year, &month, &mday, &day)) {
        return false;
    }
    s_woods.weather_day = day;
    s_woods.weather = (uint8_t)w;
    save();
    return true;
}

boopie_fest_t boopie_avatar_festival(void)
{
    if (s_sim_fest >= 0) {
        return (boopie_fest_t)s_sim_fest;
    }
    int year, month, mday;
    int32_t day;
    if (!local_date(&year, &month, &mday, &day)) {
        return BOOPIE_FEST_NONE;
    }
    ensure_loaded();
    if (s_woods.birth_month == month && s_woods.birth_day == mday) {
        return BOOPIE_FEST_BIRTHDAY;   /* its own day comes first */
    }
    return boopie_fest_on(year, month, mday);
}

bool boopie_avatar_world_hint(void)
{
    ensure_loaded();
    if (s_woods.hints >= 3) {
        return false;
    }
    s_woods.hints++;
    save();
    return true;
}

bool boopie_avatar_birthday(int *month, int *mday)
{
    ensure_loaded();
    *month = s_woods.birth_month;
    *mday = s_woods.birth_day;
    return s_woods.birth_month != 0;
}

bool boopie_avatar_set_birthday(int month, int mday)
{
    static const uint8_t DAYS[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    ensure_loaded();
    if (month < 1 || month > 12 || mday < 1 || mday > DAYS[month - 1]) {
        return false;
    }
    s_woods.birth_month = (uint8_t)month;
    s_woods.birth_day = (uint8_t)mday;
    save();
    return true;
}

/* This festival's gift, this year: its key. */
static int32_t mail_key(boopie_fest_t f)
{
    int year = 0, month = 0, mday = 0;
    int32_t day = 0;
    local_date(&year, &month, &mday, &day);
    /* 元旦 runs over the year's end: count its 31 December with the new year. */
    if (f == BOOPIE_FEST_NEW_YEAR && month == 12) {
        year++;
    }
    return year * 16 + (int32_t)f;
}

bool boopie_avatar_mail_waiting(void)
{
    ensure_loaded();
    boopie_fest_t f = boopie_avatar_festival();
    return f != BOOPIE_FEST_NONE && s_woods.mail_key != mail_key(f);
}

int boopie_avatar_open_mail(void)
{
    ensure_loaded();
    if (!boopie_avatar_mail_waiting()) {
        return 0;
    }
    boopie_fest_t f = boopie_avatar_festival();
    s_woods.mail_key = mail_key(f);
    /* A gift, over and above the day's cap for games. */
    int stars = f == BOOPIE_FEST_BIRTHDAY ? 10 : f == BOOPIE_FEST_SPRING || f == BOOPIE_FEST_XMAS ? 5 : 3;
    s_pet_state.stars += (uint32_t)stars;
    boopie_avatar_react(BOOPIE_EXPR_HAPPY, 3.0f);
    flash_overlay(BOOPIE_OVERLAY_CONFETTI, 3.0f);
    save();
    return stars;
}

static void flash_overlay(boopie_overlay_t o, float secs)
{
    s_flash_until[o] = s_now + secs;
}

/* Show what a pet call earned: eating, a level-up. */
static int s_tired;

/*
 * The default background's life: the time of day, today's weather and a
 * festival (boopie_pixel_set_ambient). Worked out once a minute.
 */
static void set_ambient(bool on)
{
    static int64_t s_at = -1;
    static bool s_on;
    int64_t now;
    int32_t day;
    int minute;
    bool known = local_now(&now, &day, &minute);
    if (on == s_on && known && now / 60 == s_at) {
        return;
    }
    s_on = on;
    s_at = known ? now / 60 : -1;
    if (!on || !known) {
        boopie_pixel_set_ambient(NULL);   /* no clock: nothing to tell */
        return;
    }
    static const boopie_amb_fest_t FESTS[BOOPIE_FEST_COUNT] = {
        [BOOPIE_FEST_SPRING] = BOOPIE_AMB_FEST_LANTERNS,   [BOOPIE_FEST_NEW_YEAR] = BOOPIE_AMB_FEST_LANTERNS,
        [BOOPIE_FEST_MOON] = BOOPIE_AMB_FEST_MOON,         [BOOPIE_FEST_HALLOWEEN] = BOOPIE_AMB_FEST_HALLOWEEN,
        [BOOPIE_FEST_XMAS] = BOOPIE_AMB_FEST_XMAS,         [BOOPIE_FEST_VALENTINE] = BOOPIE_AMB_FEST_HEARTS,
        [BOOPIE_FEST_DRAGON] = BOOPIE_AMB_FEST_LEAVES,     [BOOPIE_FEST_CHILDREN] = BOOPIE_AMB_FEST_BALLOONS,
        [BOOPIE_FEST_BIRTHDAY] = BOOPIE_AMB_FEST_BALLOONS,
    };
    boopie_fest_t f = boopie_avatar_festival();
    boopie_ambient_t a = {
        .on = true,
        .sky = minute < 6 * 60 || minute >= 19 * 60 ? BOOPIE_SKY_NIGHT
               : minute < 9 * 60                    ? BOOPIE_SKY_MORNING
               : minute >= 17 * 60                  ? BOOPIE_SKY_DUSK
                                                    : BOOPIE_SKY_DAY,
        .weather = (boopie_amb_weather_t)boopie_avatar_weather(),
        .fest = (int)f >= 0 && f < BOOPIE_FEST_COUNT ? FESTS[f] : BOOPIE_AMB_FEST_NONE,
    };
    boopie_pixel_set_ambient(&a);
}

int boopie_avatar_tired(void)
{
    return s_tired;
}

static void show_event(const boopie_pet_event_t *ev)
{
    s_tired = ev->tired;
    if (ev->levels > 0) {
        boopie_sound_play(BOOPIE_SOUND_LEVEL_UP);
    } else if (ev->fed) {
        boopie_sound_play(BOOPIE_SOUND_EAT);
    }
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
    } else {
        boopie_sound_play(BOOPIE_SOUND_POKE);
    }
    show_event(&ev);
    return fed;
}

bool boopie_avatar_feed(void)
{
    ensure_loaded();
    if (!s_pet_state.hungry) {
        return false;
    }
    return boopie_avatar_tap((BOOPIE_FOOD_X0 + BOOPIE_FOOD_X1) / 2, (BOOPIE_FOOD_Y0 + BOOPIE_FOOD_Y1) / 2);
}

void boopie_avatar_stroke(int strokes, bool hug)
{
    ensure_loaded();
    boopie_avatar_react(BOOPIE_EXPR_HAPPY, hug ? 3.0f : 2.0f);
    flash_overlay(BOOPIE_OVERLAY_BLUSH, hug ? 3.5f : 2.5f);
    flash_overlay(BOOPIE_OVERLAY_HEARTS, hug ? 3.5f : 2.5f);
    s_soothed_until = s_now + (hug ? 3.0f : 2.0f);
    s_soothed_hug = hug;
    if (hug || strokes == 2) {
        /* The first of a time: a purr, and a little experience (as a poke's, capped a day). */
        boopie_sound_play(BOOPIE_SOUND_PURR);
        boopie_pet_event_t ev = { 0 };
        boopie_pet_earn(&s_pet_state, BOOPIE_XP_POKE, -1, &ev);
        show_event(&ev);
    }
    muse_state_poke();
}

boopie_garden_t *boopie_avatar_garden(int64_t *now, int *minute)
{
    ensure_loaded();
    int32_t day;
    if (!local_now(now, &day, minute)) {
        return NULL;   /* no clock yet: nothing grows */
    }
    boopie_garden_update(&s_garden, *now);
    return &s_garden;
}

int boopie_avatar_garden_changed(int xp, int stars)
{
    int got = 0;
    if (xp || stars) {
        /* A harvest: 小窝's, toward its day. */
        boopie_pet_event_t ev = { 0 };
        boopie_pet_world(&s_pet_state, xp, stars, &ev);
        got = ev.stars;
        boopie_avatar_react(BOOPIE_EXPR_HAPPY, 3.0f);
        flash_overlay(BOOPIE_OVERLAY_CONFETTI, 3.0f);
        show_event(&ev);
    }
    save();
    return got;
}

unsigned boopie_avatar_chests_open(void)
{
    ensure_loaded();
    int64_t now;
    int32_t day;
    int minute;
    if (!local_now(&now, &day, &minute)) {
        return 0;
    }
    unsigned open = 0;
    for (int i = 0; i < WOODS_CHESTS; i++) {
        if (s_woods.chest_day[i] == day) {
            open |= 1u << i;
        }
    }
    return open;
}

bool boopie_avatar_open_chest(int chest, int *stars, int *xp)
{
    ensure_loaded();
    int64_t now;
    int32_t day;
    int minute;
    *stars = *xp = 0;
    if (chest < 0 || chest >= WOODS_CHESTS || !local_now(&now, &day, &minute) || s_woods.chest_day[chest] == day) {
        return false;
    }
    s_woods.chest_day[chest] = day;
    /* 2 to 4 stars, the deeper chest 3 to 6, different each day. */
    uint32_t h = (uint32_t)day * 2654435761u + (uint32_t)chest * 40503u;
    int want = chest == 0 ? 2 + (int)(h >> 13) % 3 : 3 + (int)(h >> 13) % 4;
    boopie_pet_event_t ev = { 0 };
    boopie_pet_world(&s_pet_state, 20, want, &ev);
    boopie_avatar_react(BOOPIE_EXPR_HAPPY, 3.0f);
    show_event(&ev);
    *stars = ev.stars;
    *xp = ev.xp;
    save();
    return true;
}

void boopie_avatar_slime_beaten(int stars, int xp, int *got_stars)
{
    ensure_loaded();
    boopie_pet_event_t ev = { 0 };
    boopie_pet_world(&s_pet_state, xp, stars, &ev);
    s_woods.slimes++;
    boopie_avatar_react(BOOPIE_EXPR_HAPPY, 2.0f);
    show_event(&ev);
    if (got_stars) {
        *got_stars = ev.stars;
    }
    save();
}

unsigned boopie_avatar_gathered(void)
{
    ensure_loaded();
    int64_t now;
    int32_t day;
    int minute;
    unsigned got = 0;
    for (int i = 0; local_now(&now, &day, &minute) && i < WOODS_SPOTS; i++) {
        if (s_woods.spot_day[i] == day) {
            got |= 1u << i;
        }
    }
    return got;
}

bool boopie_avatar_gather(int spot, int item)
{
    ensure_loaded();
    int64_t now;
    int32_t day;
    int minute;
    if (spot < 0 || spot >= WOODS_SPOTS || item < 0 || item >= BAG_ITEMS || !local_now(&now, &day, &minute)
        || s_woods.spot_day[spot] == day) {
        return false;
    }
    s_woods.spot_day[spot] = day;
    if (s_woods.items[item] < 999) {
        s_woods.items[item]++;
    }
    save();
    return true;
}

int boopie_avatar_items(int item)
{
    ensure_loaded();
    return item >= 0 && item < BAG_ITEMS ? s_woods.items[item] : 0;
}

bool boopie_avatar_snack(int item, bool *fed)
{
    ensure_loaded();
    *fed = false;
    if (item < 0 || item >= BAG_ITEMS || !s_woods.items[item]) {
        return false;
    }
    s_woods.items[item]--;
    boopie_pet_event_t ev = { 0 };
    if (s_pet_state.hungry) {
        /* As good as the bowl. */
        int64_t now;
        int32_t day;
        int minute;
        if (!local_now(&now, &day, &minute)) {
            now = (int64_t)time(NULL);
        }
        *fed = boopie_pet_tap(&s_pet_state, now, &ev);
        if (*fed) {
            s_pet_mood = BOOPIE_EXPR_IDLE;
        }
    } else {
        boopie_sound_play(BOOPIE_SOUND_EAT);
        boopie_avatar_react(BOOPIE_EXPR_EATING, 2.5f);
        int treat = boopie_item_treat_xp((boopie_item_t)item);
        if (treat) {
            boopie_pet_treat(&s_pet_state, treat, &ev);   /* bought: past the caps */
            flash_overlay(BOOPIE_OVERLAY_HEARTS, 3.0f);
        } else {
            boopie_pet_earn(&s_pet_state, BOOPIE_XP_POKE, -1, &ev);
        }
    }
    if (item == BOOPIE_ITEM_CAKE) {
        flash_overlay(BOOPIE_OVERLAY_CONFETTI, 3.0f);
    }
    show_event(&ev);
    save();
    return true;
}

bool boopie_avatar_buy_item(int item, const char **error)
{
    ensure_loaded();
    int price = item >= 0 && item < BAG_ITEMS ? boopie_item_price((boopie_item_t)item) : 0;
    if (price <= 0) {
        *error = "没有这个";
        return false;
    }
    if (s_woods.items[item] >= 99) {
        *error = "背包装不下啦";
        return false;
    }
    if (s_pet_state.stars < (uint32_t)price) {
        *error = "星星不够，再攒攒吧";
        return false;
    }
    s_pet_state.stars -= (uint32_t)price;
    s_woods.items[item]++;
    save();
    return true;
}

bool boopie_avatar_pop(void)
{
    ensure_loaded();
    if (!s_woods.items[BOOPIE_ITEM_POPPER]) {
        return false;
    }
    s_woods.items[BOOPIE_ITEM_POPPER]--;
    boopie_sound_play(BOOPIE_SOUND_LEVEL_UP);
    boopie_avatar_react(BOOPIE_EXPR_HAPPY, 4.0f);
    flash_overlay(BOOPIE_OVERLAY_CONFETTI, 5.0f);
    flash_overlay(BOOPIE_OVERLAY_HEARTS, 5.0f);
    save();
    return true;
}

void boopie_avatar_world_reward(int item, int count, int xp, int stars, int *got_stars)
{
    ensure_loaded();
    if (item >= 0 && item < BAG_ITEMS) {
        s_woods.items[item] = (uint16_t)(s_woods.items[item] + count > 999 ? 999 : s_woods.items[item] + count);
    }
    boopie_pet_event_t ev = { 0 };
    boopie_pet_world(&s_pet_state, xp, stars, &ev);
    boopie_avatar_react(BOOPIE_EXPR_HAPPY, 2.0f);
    show_event(&ev);
    if (got_stars) {
        *got_stars = ev.stars;
    }
    save();
}

int boopie_avatar_seeds(int plant)
{
    ensure_loaded();
    return plant > 0 && plant < 16 ? s_woods.seeds[plant] : 0;
}

bool boopie_avatar_buy_seed(int plant, int price, const char **error)
{
    ensure_loaded();
    if (plant <= 0 || plant >= 16 || price <= 0) {
        *error = "没有这种种子";
        return false;
    }
    if (s_woods.seeds[plant] >= 99) {
        *error = "种子装不下啦";
        return false;
    }
    if (s_pet_state.stars < (uint32_t)price) {
        *error = "星星不够";
        return false;
    }
    s_pet_state.stars -= (uint32_t)price;
    s_woods.seeds[plant]++;
    save();
    return true;
}

bool boopie_avatar_use_seed(int plant)
{
    ensure_loaded();
    if (plant <= 0 || plant >= 16 || !s_woods.seeds[plant]) {
        return false;
    }
    s_woods.seeds[plant]--;
    save();
    return true;
}

uint32_t boopie_avatar_furniture(void)
{
    ensure_loaded();
    return s_woods.furni_owned & ~s_woods.furni_away;
}

bool boopie_avatar_furni_owned(int f)
{
    ensure_loaded();
    return f >= 0 && f < 32 && (s_woods.furni_owned >> f & 1);
}

bool boopie_avatar_buy_furni(int f, int price, const char **error)
{
    ensure_loaded();
    if (f < 0 || f >= 32) {
        *error = "没有这件家具";
        return false;
    }
    if (!(s_woods.furni_owned >> f & 1)) {
        if (s_pet_state.stars < (uint32_t)price) {
            *error = "星星不够";
            return false;
        }
        s_pet_state.stars -= (uint32_t)price;
        s_woods.furni_owned |= 1u << f;
    }
    s_woods.furni_away &= ~(1u << f);
    save();
    return true;
}

void boopie_avatar_put_out(int f, bool out)
{
    ensure_loaded();
    if (f < 0 || f >= 32) {
        return;
    }
    if (out) {
        s_woods.furni_away &= ~(1u << f);
    } else {
        s_woods.furni_away |= 1u << f;
    }
    save();
}

uint32_t boopie_avatar_slimes_beaten(void)
{
    ensure_loaded();
    return s_woods.slimes;
}

const char *boopie_avatar_soothed(void)
{
    return s_now < s_soothed_until ? (s_soothed_hug ? "抱抱" : "好舒服") : NULL;
}

void boopie_avatar_greet(void)
{
    boopie_avatar_react(BOOPIE_EXPR_HAPPY, 2.5f);
    boopie_sound_play(BOOPIE_SOUND_HELLO);
    muse_state_set_caption("嗨！");
}

void boopie_avatar_upside_down(bool on)
{
    /* The "!" stays up while it's upside down. */
    boopie_avatar_set_overlay(BOOPIE_OVERLAY_SURPRISE, on);
    if (on) {
        boopie_avatar_react(BOOPIE_EXPR_DIZZY, 30.0f);
        muse_state_set_caption("哇！放我下来！");
    } else {
        boopie_avatar_react(BOOPIE_EXPR_HAPPY, 1.5f);
        muse_state_set_caption("呼～");
    }
}

bool boopie_avatar_posture_on(void)
{
    ensure_loaded();
    return s_posture;
}

void boopie_avatar_set_posture_on(bool on)
{
    ensure_loaded();
    if (on != (bool)s_posture) {
        s_posture = on;
        save();
    }
}

boopie_brain_t boopie_avatar_brain(void)
{
    ensure_loaded();
    return s_brain < BOOPIE_BRAIN_COUNT ? (boopie_brain_t)s_brain : BOOPIE_BRAIN_MUSE;
}

void boopie_avatar_set_brain(boopie_brain_t brain)
{
    ensure_loaded();
    if ((int)brain >= 0 && brain < BOOPIE_BRAIN_COUNT && brain != s_brain) {
        s_brain = (uint8_t)brain;
        save();
    }
}

bool boopie_avatar_guided(void)
{
    ensure_loaded();
    return s_guided;
}

void boopie_avatar_set_guided(bool done)
{
    ensure_loaded();
    if (done != (bool)s_guided) {
        s_guided = done;
        save();
    }
}

void boopie_avatar_game_result(boopie_game_t game, int score, int xp, int stars, boopie_pet_event_t *ev,
                               int *best, bool *record)
{
    ensure_loaded();
    boopie_pet_event_t got = { 0 };
    boopie_pet_game(&s_pet_state, xp, stars, &got);
    bool rec = false;
    if ((int)game >= 0 && game < BOOPIE_GAME_COUNT && score > 0 && (uint32_t)score > s_best[game]) {
        s_best[game] = (uint32_t)score;
        rec = true;
    }
    if (ev) {
        *ev = got;
    }
    if (best) {
        *best = (int)game >= 0 && game < BOOPIE_GAME_COUNT ? (int)s_best[game] : 0;
    }
    if (record) {
        *record = rec;
    }
    show_event(&got);
    save();
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

boopie_expr_t boopie_avatar_reacting(void)
{
    return (boopie_expr_t)s_reacting;
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
        on |= BOOPIE_OVERLAY_BIT(BOOPIE_OVERLAY_FOOD);   /* the food to tap */
        /* A different food each meal, the same all through one: from when it got hungry. */
        uint32_t h = (uint32_t)(s_pet_state.hungry_since / 60) * 2654435761u;
        bp.food = (boopie_food_t)((h >> 16) % BOOPIE_FOOD_COUNT);
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
    set_ambient(bp.scene == BOOPIE_SCENE_DEFAULT && p->mode != MUSE_MODE_OFF);
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
        float slots[5];
        boopie_pixel_set_slots(jolly_pixel_slots(slots) ? slots : NULL);
        if (s_muse_composed) {
            boopie_pixel_compose(fb, palette, bg, &bp);
        }
        return;
    }
    boopie_pixel_render(&bp);
}
