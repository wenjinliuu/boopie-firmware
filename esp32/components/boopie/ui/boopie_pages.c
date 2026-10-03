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
    bool ready;
    const char *game;   /* boopie_games_open()'s id */
} app_t;

/* What's built, each a card; what isn't yet, named below. */
static const app_t APPS[] = {
    { "戳戳布比", "宠物冒头就戳它", true, "whack" },
    { "聊天记录", "最近 100 条", true, "chat" },
    { "相册", "Muse 给你看过的图", true, "album" },
};
#define APP_COUNT (int)(sizeof APPS / sizeof APPS[0])
static const char SOON[] = "即将推出\n接零食、重力迷宫、白噪音";

static lv_obj_t *s_app_icons[APP_COUNT];
static int s_app_icon_for = -1;

static void on_app(lv_event_t *e)
{
    const app_t *a = lv_event_get_user_data(e);
    if (strcmp(a->game, "chat") == 0) {
        boopie_viewer_chat_locked();
    } else if (strcmp(a->game, "album") == 0) {
        boopie_viewer_album_locked();
    } else {
        boopie_games_open_locked(a->game);
    }
}

static void build_apps(lv_obj_t *page)
{
    title(page, "应用");
    const int w = 320, h = 74, gap = 10, top = 86;
    for (int i = 0; i < APP_COUNT; i++) {
        lv_obj_t *c = card(page, w, h);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(c, LV_ALIGN_TOP_MID, 0, top + i * (h + gap));
        if (i == 0) {
            /* The game's icon: the pet that pops up in it. */
            s_app_icons[i] = lv_image_create(c);
            lv_obj_remove_flag(s_app_icons[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_align(s_app_icons[i], LV_ALIGN_LEFT_MID, 14, 0);
        } else {
            s_app_icons[i] = NULL;
            lv_obj_t *icon = text(c, &lv_font_montserrat_28, COLOR_ACCENT, i == 1 ? LV_SYMBOL_LIST : LV_SYMBOL_IMAGE);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 30, 0);
        }
        lv_obj_t *n = text(c, &lv_font_montserrat_20, COLOR_TEXT, APPS[i].name);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 92, -12);
        lv_obj_t *d = text(c, &lv_font_montserrat_16, COLOR_DIM, APPS[i].note);
        lv_obj_align(d, LV_ALIGN_LEFT_MID, 92, 14);
        lv_obj_add_event_cb(c, on_app, LV_EVENT_CLICKED, (void *)&APPS[i]);
    }
    lv_obj_t *n = text(page, &lv_font_montserrat_16, COLOR_DIM, SOON);
    lv_obj_set_width(n, 300);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, top + APP_COUNT * (h + gap) + 8);
}

static void tick_apps(void)
{
    int cur = boopie_avatar_current();
    if (cur == s_app_icon_for) {
        return;
    }
    const lv_image_dsc_t *head = boopie_head(cur, 4);
    if (head && s_app_icons[0]) {
        lv_image_set_src(s_app_icons[0], head);
    }
    s_app_icon_for = cur;
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

static lv_obj_t *s_name, *s_level, *s_bar, *s_xp, *s_stars, *s_mood, *s_pet_head;
static int s_pet_head_for = -1;

/* To the wardrobe: the companion page in settings. */
static void on_dress(lv_event_t *e)
{
    (void)e;
    muse_ui_open_settings("avatar");
}

static void tick_pet(void);

static void named(const char *text, bool done)
{
    const char *error = NULL;
    if (done && !boopie_avatar_set_pet_name(text, &error)) {
        set_text(s_mood, "这个名字用不了");
        return;
    }
    tick_pet();
}

/* Tapping the name renames the pet; left empty, it goes back to its character's. */
static void on_name(lv_event_t *e)
{
    (void)e;
    boopie_input_open("给它起个名字", boopie_avatar_has_own_name() ? boopie_avatar_pet_name() : "",
                      "留空就叫角色名", BOOPIE_PET_NAME_CHARS, named);
}

static void build_pet(lv_obj_t *page)
{
    title(page, "小窝");
    /* The pet itself, over its name. */
    s_pet_head = lv_image_create(page);
    lv_obj_align(s_pet_head, LV_ALIGN_TOP_MID, 0, 80);
    s_name = text(page, &lv_font_montserrat_28, COLOR_TEXT, "");
    lv_obj_align(s_name, LV_ALIGN_TOP_MID, 0, 148);
    lv_obj_add_flag(s_name, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_name, 20);
    lv_obj_add_event_cb(s_name, on_name, LV_EVENT_CLICKED, NULL);
    s_level = text(page, &lv_font_montserrat_20, COLOR_ACCENT, "");
    lv_obj_align(s_level, LV_ALIGN_TOP_MID, -110, 196);
    lv_obj_set_width(s_level, 70);

    s_bar = lv_bar_create(page);
    lv_obj_set_size(s_bar, 180, 12);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 10, 202);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 6, LV_PART_INDICATOR);
    s_xp = text(page, &lv_font_montserrat_16, COLOR_DIM, "");
    lv_obj_align(s_xp, LV_ALIGN_TOP_MID, 10, 220);

    lv_obj_t *c = card(page, 320, 96);
    lv_obj_align(c, LV_ALIGN_TOP_MID, 0, 252);
    s_stars = text(c, &lv_font_montserrat_20, COLOR_GOLD, "");
    lv_obj_align(s_stars, LV_ALIGN_TOP_MID, 0, 16);
    s_mood = text(c, &lv_font_montserrat_20, COLOR_TEXT, "");
    lv_obj_align(s_mood, LV_ALIGN_TOP_MID, 0, 52);

    lv_obj_t *b = card(page, 150, 48);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 362);
    lv_obj_center(text(b, &lv_font_montserrat_20, COLOR_ACCENT, LV_SYMBOL_IMAGE "  换装"));
    lv_obj_add_event_cb(b, on_dress, LV_EVENT_CLICKED, NULL);
}

static const char *mood_name(const boopie_pet_status_t *st)
{
    if (st->hungry) {
        return "饿了，去主屏点食物喂它";
    }
    switch (st->mood) {
    case BOOPIE_EXPR_SLEEPY:
        return "困了";
    case BOOPIE_EXPR_SAD:
        return "有点想你";
    default:
        return "心情不错";
    }
}

static void tick_pet(void)
{
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    char buf[64];
    snprintf(buf, sizeof buf, "%s  " LV_SYMBOL_EDIT, boopie_avatar_pet_name());   /* tap to rename */
    set_text(s_name, buf);
    snprintf(buf, sizeof buf, "Lv %d", st.level);
    set_text(s_level, buf);
    int cur = boopie_avatar_current();
    if (cur != s_pet_head_for) {
        const lv_image_dsc_t *head = boopie_head(cur, 5);
        if (head) {
            lv_image_set_src(s_pet_head, head);
        }
        s_pet_head_for = cur;
    }
    lv_bar_set_range(s_bar, 0, st.xp_need > 0 ? (int32_t)st.xp_need : 1);
    lv_bar_set_value(s_bar, (int32_t)st.xp_into, LV_ANIM_OFF);
    snprintf(buf, sizeof buf, "%u / %u", (unsigned)st.xp_into, (unsigned)st.xp_need);
    set_text(s_xp, buf);
    snprintf(buf, sizeof buf, "★ %u", (unsigned)st.stars);
    set_text(s_stars, buf);
    set_text(s_mood, mood_name(&st));
}

/* ---------------------------------------------------------------- */

static lv_obj_t *s_apps, *s_cards, *s_pet;

void boopie_pages_build(lv_obj_t *apps, lv_obj_t *cards, lv_obj_t *pet)
{
    s_apps = apps;
    s_cards = cards;
    s_pet = pet;
    build_apps(apps);
    build_cards(cards);
    build_pet(pet);
}

void boopie_pages_tick(lv_obj_t *shown)
{
    if (shown && shown == s_cards) {
        tick_cards();
    } else if (shown && shown == s_pet) {
        tick_pet();
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
