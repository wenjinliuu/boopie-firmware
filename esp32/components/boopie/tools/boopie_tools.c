/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Doing what boopie_tools_spec.c lists, for Muse's commands and 小智's MCP
 * calls alike. What changes something shows a word on the screen for a
 * moment (the toast), so it's plain the board did it. Some of these write to
 * NVS: the caller's stack must be in internal RAM.
 */

#include "boopie_tools.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "boopie_avatar.h"
#include "boopie_font.h"
#include "boopie_games.h"
#include "boopie_noise_ui.h"
#include "boopie_pixel.h"
#include "boopie_viewers.h"
#include "boopie_world_ui.h"
#include "lvgl.h"
#include "muse_board.h"
#include "muse_settings.h"
#include "muse_state.h"
#include "muse_ui.h"

/* ---- the toast ---- */

static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;

static void toast_gone(lv_timer_t *t)
{
    (void)t;
    if (s_toast) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    }
}

/* A word at the bottom of the screen for a couple of seconds. Takes the display lock. */
static void toast(const char *fmt, ...)
{
    char text[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    muse_board->display_lock(-1);
    if (!s_toast) {
        s_toast = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_font(s_toast, boopie_font_with_cjk(&lv_font_montserrat_20), 0);
        lv_obj_set_style_text_color(s_toast, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x6c4bd8), 0);
        lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
        lv_obj_set_style_radius(s_toast, 22, 0);
        lv_obj_set_style_pad_hor(s_toast, 18, 0);
        lv_obj_set_style_pad_ver(s_toast, 8, 0);
        lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -84);   /* under the pet, over the dots */
        s_toast_timer = lv_timer_create(toast_gone, 2500, NULL);
        lv_timer_pause(s_toast_timer);
    }
    lv_label_set_text(s_toast, text);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);
    lv_timer_reset(s_toast_timer);
    lv_timer_set_repeat_count(s_toast_timer, 1);
    lv_timer_resume(s_toast_timer);
    muse_board->display_unlock();
    muse_state_poke();   /* the screen wakes to show it */
}

/* ---- results ---- */

static cJSON *ok(void)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return r;
}

static cJSON *fail(const char *code, const char *message)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", false);
    cJSON *e = cJSON_AddObjectToObject(r, "error");
    cJSON_AddStringToObject(e, "code", code);
    cJSON_AddStringToObject(e, "message", message ? message : "failed");
    return r;
}

static cJSON *with_status(cJSON *r, const char *status)
{
    cJSON_AddStringToObject(r, "status", status);
    return r;
}

/* A string parameter, or NULL. */
static const char *str(const cJSON *params, const char *key)
{
    const cJSON *v = params ? cJSON_GetObjectItemCaseSensitive(params, key) : NULL;
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

/* ---- each one ---- */

static cJSON *pet_status(void)
{
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    cJSON *r = ok();
    cJSON_AddStringToObject(r, "name", boopie_avatar_pet_name());
    cJSON_AddBoolToObject(r, "hungry", st.hungry);
    cJSON_AddStringToObject(r, "mood", boopie_expr_name(st.mood));
    cJSON_AddNumberToObject(r, "level", st.level);
    cJSON_AddNumberToObject(r, "xp_into_level", st.xp_into);
    cJSON_AddNumberToObject(r, "xp_for_level", st.xp_need);
    cJSON_AddNumberToObject(r, "stars", st.stars);
    return r;
}

static cJSON *pet_name(const cJSON *params)
{
    const char *name = str(params, "name");
    const char *error = NULL;
    if (name && !boopie_avatar_set_pet_name(name, &error)) {
        return fail("bad_param", error);
    }
    if (name) {
        toast("改名啦：%s", boopie_avatar_pet_name());
    }
    cJSON *r = ok();
    cJSON_AddStringToObject(r, "name", boopie_avatar_pet_name());
    cJSON_AddBoolToObject(r, "own_name", boopie_avatar_has_own_name());
    return r;
}

static cJSON *pet_feed(void)
{
    muse_board->display_lock(-1);
    bool fed = boopie_avatar_feed();
    muse_board->display_unlock();
    if (!fed) {
        return with_status(ok(), "not hungry: it didn't eat");
    }
    toast("喂好啦");
    return with_status(ok(), "fed");
}

static cJSON *avatar(const cJSON *params)
{
    const char *error = NULL;
    const char *keys[] = { "avatar", "colour", "expression", "reaction", "background", "skin", "accessory" };
    bool any = false;
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        any |= str(params, keys[i]) != NULL;
    }
    const cJSON *on = params ? cJSON_GetObjectItemCaseSensitive(params, "on") : NULL;
    if (!boopie_avatar_command(str(params, "avatar"), str(params, "colour"), str(params, "expression"),
                               str(params, "reaction"), str(params, "background"), str(params, "skin"),
                               str(params, "accessory"), !cJSON_IsFalse(on), &error)) {
        return fail("bad_param", error);
    }
    if (any && !str(params, "reaction") && !str(params, "expression")) {
        toast("换好啦");
    }
    cJSON *r = ok();
    cJSON_AddStringToObject(r, "avatar", boopie_avatar_key(boopie_avatar_current()));
    cJSON_AddStringToObject(r, "expression", boopie_expr_name(boopie_avatar_pet()));
    cJSON_AddStringToObject(r, "background", boopie_scene_key(boopie_avatar_scene()));
    int worn = boopie_avatar_skin();
    cJSON_AddStringToObject(r, "skin", worn >= 0 ? boopie_skin_key(worn) : "none");
    cJSON *acc = cJSON_AddArrayToObject(r, "accessories");
    for (int i = 0; i < BOOPIE_ACC_COUNT; i++) {
        if (boopie_avatar_accessories() & BOOPIE_ACC_BIT(i)) {
            cJSON_AddItemToArray(acc, cJSON_CreateString(boopie_acc_key((boopie_acc_t)i)));
        }
    }
    return r;
}

static cJSON *game_start(const cJSON *params)
{
    const char *game = str(params, "game");
    if (!boopie_games_open(game ? game : "whack")) {
        return fail("bad_param", "unknown game");
    }
    return ok();
}

/* Whatever covers the pages (a game, a viewer, the noise page), closed. */
static void close_overlays(void)
{
    if (boopie_games_active()) {
        boopie_games_key(false);
    }
    if (boopie_viewer_active()) {
        boopie_viewer_close();   /* these take the lock themselves */
    }
    if (boopie_noise_ui_active()) {
        boopie_noise_ui_close();
    }
}

static cJSON *app_open(const cJSON *params)
{
    static const char *const ROOMS[] = { "living", "bedroom", "outside", "woods", "beach" };
    static const char *const PAGES[] = { "wifi", "bluetooth", "vpn", "avatar", "brain",
                                         "sound", "sleep", "battery", "storage" };
    const char *app = str(params, "app");
    const char *room = str(params, "room");
    const char *page = str(params, "page");
    if (!app) {
        return fail("bad_param", "app is required");
    }
    bool known_room = !room, known_page = !page;
    for (size_t i = 0; room && i < sizeof ROOMS / sizeof *ROOMS; i++) {
        known_room |= !strcmp(room, ROOMS[i]);
    }
    for (size_t i = 0; page && i < sizeof PAGES / sizeof *PAGES; i++) {
        known_page |= !strcmp(page, PAGES[i]);
    }
    if (!known_room || !known_page) {
        return fail("bad_param", !known_room ? "room: living, bedroom, outside, woods or beach" : "unknown page");
    }
    if (strcmp(app, "home") && strcmp(app, "nest") && strcmp(app, "chat") && strcmp(app, "album")
        && strcmp(app, "noise") && strcmp(app, "settings")) {
        return fail("bad_param", "app: home, nest, chat, album, noise or settings");
    }
    close_overlays();
    if (!strcmp(app, "home")) {
        muse_ui_go_home();
        return ok();
    }
    muse_ui_go_home();
    muse_board->display_lock(-1);
    if (!strcmp(app, "nest")) {
        muse_ui_open_nest();
        if (room) {
            boopie_world_ui_go(room);
        }
    } else if (!strcmp(app, "chat")) {
        boopie_viewer_chat_locked();
    } else if (!strcmp(app, "album")) {
        boopie_viewer_album_locked();
    } else if (!strcmp(app, "noise")) {
        boopie_noise_ui_open_locked();
    } else {
        muse_ui_open_settings(page ? page : NULL);
    }
    muse_board->display_unlock();
    muse_state_poke();
    return ok();
}

static cJSON *noise_play(const cJSON *params)
{
    const cJSON *m = params ? cJSON_GetObjectItemCaseSensitive(params, "minutes") : NULL;
    int minutes = cJSON_IsNumber(m) ? (int)m->valuedouble : -1;
    char said[112];
    if (minutes > 600 || !boopie_noise_ui_play(str(params, "kind"), minutes, said, sizeof said)) {
        return fail("bad_param", "kind: white, pink, rain or waves; minutes: 0 to 600");
    }
    toast("正在播放");
    return with_status(ok(), said);
}

static cJSON *noise_stop(void)
{
    char said[64];
    boopie_noise_ui_stop(said, sizeof said);
    return with_status(ok(), said);
}

static cJSON *sound(const cJSON *params)
{
    const cJSON *v = params ? cJSON_GetObjectItemCaseSensitive(params, "volume") : NULL;
    const cJSON *m = params ? cJSON_GetObjectItemCaseSensitive(params, "muted") : NULL;
    if (v && (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > 100)) {
        return fail("bad_param", "volume: 0 to 100");
    }
    if (m && !cJSON_IsBool(m)) {
        return fail("bad_param", "muted: true or false");
    }
    if (v) {
        muse_settings_set_volume((int)v->valuedouble);
        if (!m && !muse_settings_speaker_on()) {
            muse_settings_set_speaker_on(true);   /* turned up: it's to be heard */
        }
    }
    if (m) {
        muse_settings_set_speaker_on(!cJSON_IsTrue(m));
    }
    if (m && cJSON_IsTrue(m)) {
        toast("已静音");
    } else if (v || m) {
        toast("音量 %d", muse_settings_volume());
    }
    cJSON *r = ok();
    cJSON_AddNumberToObject(r, "volume", muse_settings_volume());
    cJSON_AddBoolToObject(r, "muted", !muse_settings_speaker_on());
    return r;
}

static cJSON *brightness(const cJSON *params)
{
    const cJSON *b = params ? cJSON_GetObjectItemCaseSensitive(params, "brightness") : NULL;
    if (b && (!cJSON_IsNumber(b) || b->valuedouble < 10 || b->valuedouble > 100)) {
        return fail("bad_param", "brightness: 10 to 100");
    }
    if (b) {
        muse_settings_set_brightness((int)b->valuedouble);
        toast("亮度 %d", muse_settings_brightness());
    }
    cJSON *r = ok();
    cJSON_AddNumberToObject(r, "brightness", muse_settings_brightness());
    return r;
}

static cJSON *battery(void)
{
    muse_power_t p = muse_state_power();
    if (p.battery_pct < 0) {
        return with_status(ok(), "no battery: on USB power");
    }
    cJSON *r = ok();
    cJSON_AddNumberToObject(r, "percent", p.battery_pct);
    cJSON_AddBoolToObject(r, "charging", p.charging);
    cJSON_AddBoolToObject(r, "usb", p.usb);
    if (p.battery_mv > 0) {
        cJSON_AddNumberToObject(r, "volts", p.battery_mv / 1000.0);
    }
    return r;
}

static cJSON *garden(void)
{
    char said[720];
    boopie_world_ui_farm_status(said, sizeof said);
    cJSON *r = ok();
    cJSON_AddStringToObject(r, "garden", said);
    return r;
}

static cJSON *weather(const cJSON *params)
{
    char said[64];
    if (!boopie_world_ui_set_weather(str(params, "kind"), said, sizeof said)) {
        return fail("bad_param", "kind: sunny, cloudy, rain or snow (and the clock set)");
    }
    return with_status(ok(), said);
}

static cJSON *storage_clear(const cJSON *params)
{
    /* Never straight away: the screen asks, and only a tap clears. */
    const char *what = str(params, "what");
    if (!boopie_viewer_ask_clear(what ? what : "")) {
        return fail("bad_param", "what: chat, album, notes or all");
    }
    return with_status(ok(), "asked on screen; cleared only if the user taps to confirm");
}

cJSON *boopie_tools_call(const char *name, const cJSON *params)
{
    const boopie_tool_t *t = boopie_tools_find(name);
    if (!t) {
        return NULL;
    }
    const char *n = t->name;
    if (!strcmp(n, "pet.status")) {
        return pet_status();
    } else if (!strcmp(n, "pet.name")) {
        return pet_name(params);
    } else if (!strcmp(n, "pet.feed")) {
        return pet_feed();
    } else if (!strcmp(n, "display.avatar")) {
        return avatar(params);
    } else if (!strcmp(n, "game.start")) {
        return game_start(params);
    } else if (!strcmp(n, "app.open")) {
        return app_open(params);
    } else if (!strcmp(n, "noise.play")) {
        return noise_play(params);
    } else if (!strcmp(n, "noise.stop")) {
        return noise_stop();
    } else if (!strcmp(n, "device.sound")) {
        return sound(params);
    } else if (!strcmp(n, "device.brightness")) {
        return brightness(params);
    } else if (!strcmp(n, "device.battery")) {
        return battery();
    } else if (!strcmp(n, "garden.status")) {
        return garden();
    } else if (!strcmp(n, "world.weather")) {
        return weather(params);
    } else if (!strcmp(n, "storage.clear")) {
        return storage_clear(params);
    }
    return fail("unsupported", "not done on this board");
}
