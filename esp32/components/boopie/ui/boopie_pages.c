/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_pages.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "boopie_avatar.h"
#include "boopie_font.h"
#include "boopie_games.h"
#include "boopie_heads.h"
#include "boopie_icons.h"
#include "boopie_noise_ui.h"
#include "boopie_world_ui.h"
#include "boopie_viewers.h"
#include "boopie_input.h"
#include "boopie_store.h"
#include "muse_board.h"
#include "muse_input.h"
#include "muse_settings.h"
#include "muse_ui.h"
#ifdef ESP_PLATFORM
#include "boopie_clock.h"
#include "esp_system.h"
#include "nvs_flash.h"
#endif

/* As the settings pages'. Chinese is drawn 24 px (2x the pixel font) at the least:
 * the 12 px the smaller fonts fall back to is about 1 mm on this screen. */
#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff
#define COLOR_GOLD 0xffd246
#define COLOR_DANGER 0xff5c5c
#define TITLE_Y 44

static const char *const WEEKDAYS[7] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };

/* Local time, if known. */
static bool now_local(struct tm *out)
{
#ifdef ESP_PLATFORM
    return boopie_clock_local(out);
#else
    time_t t = time(NULL);
    return localtime_r(&t, out) != NULL;
#endif
}

const char *boopie_pages_clock(void)
{
    static char buf[8];
    struct tm tm;
    if (!now_local(&tm)) {
        return NULL;
    }
    snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, const char *s)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, boopie_font_with_cjk(font), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_obj_set_style_text_line_space(l, 6, 0);   /* the 24 px Chinese is taller than the Latin font's line */
    lv_label_set_text(l, s);
    return l;
}

static void set_text(lv_obj_t *l, const char *s)
{
    if (strcmp(lv_label_get_text(l), s) != 0) {
        lv_label_set_text(l, s);
    }
}

static lv_obj_t *title(lv_obj_t *page, const char *s)
{
    /* In the pixel font, Chinese too, as the face's state is. */
    lv_obj_t *t = text(page, &boopie_font_pixel_24, COLOR_ACCENT, s);
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, TITLE_Y);
    return t;
}

static lv_obj_t *card(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/* ---------------------------------------------------------------- apps */

typedef struct {
    const char *name, *note;
    const char *id;     /* boopie_games_open()'s id, or a viewer */
    int icon;           /* boopie_icon_t, or -1: the pet's head */
} app_t;

/* What's built, a big icon each, in a list that scrolls up and down; the one
 * in the middle full size, the rest smaller and dimmer toward the round edge. */
static const app_t APPS[] = {
    { "戳戳布比", "宠物冒头就戳它", "whack", -1 },
    { "接零食", "倾斜接住掉下的零食", "catch", BOOPIE_ICON_CATCH },
    { "重力迷宫", "倾斜把小球滚出迷宫", "maze", BOOPIE_ICON_MAZE },
    { "跳跳布比", "点一下跳，钻过柱子", "hop", BOOPIE_ICON_HOP },
    { "聊天记录", "最近 100 条", "chat", BOOPIE_ICON_CHAT },
    { "相册", "Muse 给你看过的图", "album", BOOPIE_ICON_ALBUM },
    { "白噪音", "雨声、海浪，助眠专注", "noise", BOOPIE_ICON_NOISE },
};
#define APP_COUNT (int)(sizeof APPS / sizeof APPS[0])
#define APP_ROW_H 112
#define APP_TILE 92

static lv_obj_t *s_app_list;
static lv_obj_t *s_app_head;   /* 戳戳布比's icon: the pet as it is now */
static int s_app_head_for = -1;

static void on_app(lv_event_t *e)
{
    const app_t *a = lv_event_get_user_data(e);
    if (strcmp(a->id, "chat") == 0) {
        boopie_viewer_chat_locked();
    } else if (strcmp(a->id, "album") == 0) {
        boopie_viewer_album_locked();
    } else if (strcmp(a->id, "noise") == 0) {
        boopie_noise_ui_open_locked();
    } else {
        boopie_games_open_locked(a->id);
    }
}

/* Each row by how far it is from the middle: smaller and dimmer toward the edge. */
static void on_app_scroll(lv_event_t *e)
{
    (void)e;
    lv_area_t box;
    lv_obj_get_coords(s_app_list, &box);
    int mid = (box.y1 + box.y2) / 2;
    uint32_t n = lv_obj_get_child_count(s_app_list);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_get_child(s_app_list, (int32_t)i);
        lv_area_t a;
        lv_obj_get_coords(row, &a);
        int d = LV_ABS((a.y1 + a.y2) / 2 - mid);
        int scale = 256 - d * 70 / 200;
        int opa = 255 - d * 170 / 200;
        lv_obj_set_style_transform_scale(row, scale < 170 ? 170 : scale, 0);
        lv_obj_set_style_opa(row, (lv_opa_t)(opa < 60 ? 60 : opa), 0);
    }
}

static void build_apps(lv_obj_t *page)
{
    s_app_list = lv_obj_create(page);
    lv_obj_remove_style_all(s_app_list);
    lv_obj_set_size(s_app_list, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_app_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_app_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    /* The first row and the last can come to the middle. */
    lv_obj_set_style_pad_top(s_app_list, 233 - APP_ROW_H / 2, 0);
    lv_obj_set_style_pad_bottom(s_app_list, 233 - APP_ROW_H / 2, 0);
    lv_obj_set_scroll_dir(s_app_list, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(s_app_list, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(s_app_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_app_list, on_app_scroll, LV_EVENT_SCROLL, NULL);

    for (int i = 0; i < APP_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(s_app_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 340, APP_ROW_H);
        lv_obj_set_style_radius(row, 26, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_CARD_PRESSED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_transform_pivot_x(row, 170, 0);
        lv_obj_set_style_transform_pivot_y(row, APP_ROW_H / 2, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *tile;
        if (APPS[i].icon < 0) {
            tile = boopie_icon_tile_custom(row, NULL, 0x2a2150, APP_TILE);
            s_app_head = lv_image_create(tile);
            lv_obj_remove_flag(s_app_head, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_center(s_app_head);
        } else {
            tile = boopie_icon_tile(row, (boopie_icon_t)APPS[i].icon, APP_TILE, 5);
        }
        lv_obj_align(tile, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_t *n = text(row, boopie_font_ui(28) ? boopie_font_ui(28) : &lv_font_montserrat_28, COLOR_TEXT,
                           APPS[i].name);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, APP_TILE + 26, -16);
        lv_obj_t *d = text(row, &lv_font_montserrat_16, COLOR_DIM, APPS[i].note);
        lv_obj_align(d, LV_ALIGN_LEFT_MID, APP_TILE + 26, 20);
        lv_obj_add_event_cb(row, on_app, LV_EVENT_CLICKED, (void *)&APPS[i]);
    }

    /* The title over the list, on a band the rows pass under. */
    lv_obj_t *band = lv_obj_create(page);
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, lv_pct(100), TITLE_Y + 46);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(band, lv_color_black(), 0);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    title(page, "应用");
    lv_obj_update_layout(s_app_list);   /* where the rows are, for their sizes */
    on_app_scroll(NULL);
}

static void tick_apps(void)
{
    int cur = boopie_avatar_current();
    if (cur == s_app_head_for) {
        return;
    }
    const lv_image_dsc_t *head = boopie_head(cur, 5);
    if (head && s_app_head) {
        lv_image_set_src(s_app_head, boopie_icon_keyed(head));
    }
    s_app_head_for = cur;
}

/* ---------------------------------------------------------------- cards */

static lv_obj_t *s_date;

static void build_cards(lv_obj_t *page)
{
    title(page, "今天");
    s_date = text(page, &lv_font_montserrat_28, COLOR_TEXT, "");
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_t *c = card(page, 330, 150);
    lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_t *t = text(c, &lv_font_montserrat_20, COLOR_TEXT, "还没有卡片");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_t *n = text(c, &lv_font_montserrat_20, COLOR_DIM, "AI 推送的天气、\n日程和提醒会在这里");
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 70);
}

static void tick_cards(void)
{
    struct tm tm;
    char buf[48];
    if (now_local(&tm)) {
        snprintf(buf, sizeof buf, "%d月%d日  %s", tm.tm_mon + 1, tm.tm_mday, WEEKDAYS[tm.tm_wday % 7]);
    } else {
        snprintf(buf, sizeof buf, "%s", "等待校时…");
    }
    set_text(s_date, buf);
}

/* ---------------------------------------------------------------- the pet */

/* ---------------------------------------------------------------- */

static lv_obj_t *s_apps, *s_cards, *s_pet;

void boopie_pages_build(lv_obj_t *apps, lv_obj_t *cards, lv_obj_t *pet)
{
    s_apps = apps;
    s_cards = cards;
    s_pet = pet;
    build_apps(apps);
    build_cards(cards);
    boopie_world_ui_build(pet);   /* 小窝: the pet's home */
}

void boopie_pages_tick(lv_obj_t *shown)
{
    if (shown && shown == s_cards) {
        tick_cards();
    } else if (shown && shown == s_apps) {
        tick_apps();
    }
}

/* ---------------------------------------------------------------- the power menu */

static lv_obj_t *s_menu;

typedef enum { ACT_OFF, ACT_RESTART, ACT_MUTE, ACT_RESET, ACT_RESET_SURE, ACT_CANCEL } action_t;

static void build_menu(bool reset_ask);

static void menu_close_locked(void)
{
    if (s_menu) {
        lv_obj_delete_async(s_menu);   /* often from one of its own buttons' events */
        s_menu = NULL;
    }
}

static void on_action(lv_event_t *e)
{
    action_t a = (action_t)(intptr_t)lv_event_get_user_data(e);
    lv_event_stop_bubbling(e);
    switch (a) {
    case ACT_OFF:
        menu_close_locked();
        muse_input_request_power_off();
        break;
    case ACT_RESTART:
        menu_close_locked();
#ifdef ESP_PLATFORM
        boopie_clock_save();
        esp_restart();
#endif
        break;
    case ACT_MUTE:
        muse_settings_set_speaker_on(!muse_settings_speaker_on());
        menu_close_locked();
        break;
    case ACT_RESET:
        menu_close_locked();
        build_menu(true);   /* asked again before anything's lost */
        break;
    case ACT_RESET_SURE:
        menu_close_locked();
#ifdef ESP_PLATFORM
        boopie_store_wipe();   /* chat history, pictures, notes */
        nvs_flash_erase();     /* the pet, Wi-Fi, pairing, every setting */
        esp_restart();
#endif
        break;
    default:
        menu_close_locked();
        break;
    }
}

static void menu_button(lv_obj_t *col, const char *label, uint32_t colour, action_t a)
{
    lv_obj_t *b = card(col, 260, 54);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *t = text(b, &lv_font_montserrat_20, colour, label);
    lv_obj_center(t);
    lv_obj_add_event_cb(b, on_action, LV_EVENT_CLICKED, (void *)(intptr_t)a);
}

static void build_menu(bool reset_ask)
{
    s_menu = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_menu);
    lv_obj_set_size(s_menu, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_menu, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_90, 0);
    lv_obj_add_flag(s_menu, LV_OBJ_FLAG_CLICKABLE);   /* a tap outside the buttons closes it */
    lv_obj_add_event_cb(s_menu, on_action, LV_EVENT_CLICKED, (void *)(intptr_t)ACT_CANCEL);

    lv_obj_t *col = lv_obj_create(s_menu);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 280, LV_SIZE_CONTENT);
    lv_obj_center(col);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 10, 0);
    if (reset_ask) {
        lv_obj_t *q = text(col, &lv_font_montserrat_20, COLOR_TEXT, "恢复出厂？\n宠物、Wi-Fi、配对\n都会清除");
        lv_obj_set_style_text_align(q, LV_TEXT_ALIGN_CENTER, 0);
        menu_button(col, "确定清除", COLOR_DANGER, ACT_RESET_SURE);
        menu_button(col, "取消", COLOR_TEXT, ACT_CANCEL);
        return;
    }
    menu_button(col, "关机", COLOR_TEXT, ACT_OFF);
    menu_button(col, "重启", COLOR_TEXT, ACT_RESTART);
    menu_button(col, muse_settings_speaker_on() ? "静音" : "取消静音", COLOR_TEXT, ACT_MUTE);
    menu_button(col, "恢复出厂", COLOR_DANGER, ACT_RESET);
}

void boopie_pages_power_menu(void)
{
    muse_board->display_lock(-1);
    menu_close_locked();
    build_menu(false);
    muse_board->display_unlock();
}

bool boopie_pages_menu_open(void)
{
    return s_menu != NULL;
}

void boopie_pages_menu_close(void)
{
    muse_board->display_lock(-1);
    menu_close_locked();
    muse_board->display_unlock();
}
