/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_world_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "boopie_avatar.h"
#include "boopie_font.h"
#include "boopie_games.h"
#include "boopie_garden.h"
#include "boopie_icons.h"
#include "boopie_input.h"
#include "boopie_noise_ui.h"
#include "boopie_pages.h"
#include "boopie_pixel.h"
#include "boopie_sound.h"
#include "boopie_viewers.h"
#include "boopie_world.h"
#include "boopie_world_draw.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "muse_board.h"
#include "muse_state.h"
#include "muse_ui.h"
#ifdef ESP_PLATFORM
#include "boopie_clock.h"
#endif

#define SIZE 466
#define FRAME_MS 66              /* about 15 a second while it's shown */
#define SAY_S 4.5f               /* a line stays this long */
#define CHAT_EVERY_S 14.0f       /* and the pet says something new this often */

#define INK 0x282c38             /* the panels' dark */
#define PAPER 0xf8f8f0
#define BLUE 0x4068b8

static lv_obj_t *s_tile, *s_image, *s_say, *s_say_text, *s_panel, *s_back_hint;
static lv_image_dsc_t s_dsc;
static uint16_t *s_screen;
static uint8_t *s_rgb;
static boopie_world_t s_world;
static int64_t s_last_us;
static float s_t, s_said_at = -100, s_chat_at;
static uint16_t s_pet[BOOPIE_HEAD_W * BOOPIE_PET_H];
static int s_pet_for = -1;
static bool s_hello;   /* the first visit's hint has been said */

/* ---------------------------------------------------------------- what it says */

static void say(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    lv_label_set_text(s_say_text, buf);
    lv_obj_remove_flag(s_say, LV_OBJ_FLAG_HIDDEN);
    s_said_at = s_t;
}

static bool local_hour(int *hour)
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
    *hour = tm.tm_hour;
    return true;
}

/* Something to say when nothing's happening: hungry, sleepy, the hour, or just chatter. */
static void chatter(const boopie_pet_status_t *st, int hour)
{
    static const char *const WOODS[] = {
        "森林里好安静", "那边有史莱姆！点它试试", "宝箱里会有什么呢？", "小心别迷路哦", "听，有鸟在叫",
    };
    boopie_fest_t fest = boopie_avatar_festival();
    boopie_weather_t wx = boopie_avatar_weather();
    if (!st->hungry && rand() % 3 == 0) {
        if (fest != BOOPIE_FEST_NONE) {
            if (fest == BOOPIE_FEST_BIRTHDAY) {
                say(boopie_avatar_mail_waiting() ? "今天是我的生日！信箱里会有礼物吗？" : "今天是我的生日！");
            } else if (boopie_avatar_mail_waiting()) {
                say("%s快乐！信箱里好像有东西", boopie_fest_name(fest));
            } else {
                say("%s快乐！", boopie_fest_name(fest));
            }
            return;
        }
        if (wx == BOOPIE_WEATHER_RAIN) {
            say(boopie_world_outdoors(s_world.room) ? "雨好大，回屋躲躲雨吧" : "外面在下雨，听滴答滴答");
            return;
        }
        if (wx == BOOPIE_WEATHER_SNOW) {
            say(boopie_world_outdoors(s_world.room) ? "雪花好凉～" : "外面下雪了，出去堆雪人吧");
            return;
        }
    }
    if (s_world.room == BOOPIE_ROOM_WOODS && !st->hungry && hour >= 6 && hour < 22) {
        say("%s", WOODS[rand() % (int)(sizeof WOODS / sizeof WOODS[0])]);
        return;
    }
    static const char *const IDLE[] = {
        "今天也要开心哦", "摸摸我的头吧～", "陪我玩一会儿嘛", "点冒气泡的东西试试看", "我在家里转转",
        "外面天气怎么样？", "等我长大，家里会变漂亮哦",
    };
    if (st->hungry) {
        say("肚子饿了…点食盆喂我吧");
    } else if (hour >= 22 || hour < 6) {
        say(s_world.state == BOOPIE_PET_SLEEPING ? "呼…呼…" : "困了，陪我去睡觉吧");
    } else if (hour >= 6 && hour < 9) {
        say("早上好！");
    } else {
        say("%s", IDLE[rand() % (int)(sizeof IDLE / sizeof IDLE[0])]);
    }
}

/* ---------------------------------------------------------------- panels */

typedef void (*row_cb_t)(int row);
static row_cb_t s_row_cb;
static int s_armed = -1;            /* the shop: a row tapped once, to buy on the second */
static lv_obj_t *s_rows[12];

/* A slime fight, or the line out: every tap is for that. */
static bool busy(void)
{
    return s_world.fight >= 0 || s_world.fishing;
}

static void close_panel(void)
{
    if (s_panel) {
        lv_obj_delete_async(s_panel);
        s_panel = NULL;
        muse_ui_set_swipe_enabled(!busy());   /* a slime fight, or fishing, keeps it off */
    }
    s_row_cb = NULL;
    s_armed = -1;
}

static void on_panel_outside(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == s_panel) {
        close_panel();
    }
}

static void on_row(lv_event_t *e)
{
    int row = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_row_cb) {
        s_row_cb(row);
    }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, boopie_font_with_cjk(font), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_label_set_text(l, text);
    return l;
}

/* A panel in the monster games' way: a white box with a dark edge, a title, rows. */
static lv_obj_t *panel(const char *title, row_cb_t cb)
{
    close_panel();
    s_row_cb = cb;
    s_panel = lv_obj_create(s_tile);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_40, 0);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_panel, on_panel_outside, LV_EVENT_CLICKED, NULL);
    lv_obj_t *box = lv_obj_create(s_panel);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 330, 318);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_bg_color(box, lv_color_hex(PAPER), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(INK), 0);
    lv_obj_set_style_border_width(box, 4, 0);
    lv_obj_set_style_outline_color(box, lv_color_hex(0x8098c8), 0);
    lv_obj_set_style_outline_width(box, 3, 0);
    lv_obj_set_style_pad_all(box, 14, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);   /* taps in it don't close it */
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *t = label(box, &boopie_font_pixel_24, BLUE, title);
    lv_obj_set_style_pad_bottom(t, 4, 0);
    memset(s_rows, 0, sizeof s_rows);
    muse_ui_set_swipe_enabled(false);
    return box;
}

/* A row: a ▶ when tappable, the text, and a value on the right. */
static lv_obj_t *row(lv_obj_t *box, int index, const char *text, const char *value, bool tappable)
{
    lv_obj_t *r = lv_obj_create(box);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), 40);
    lv_obj_set_style_radius(r, 8, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(0xdce4f4), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = label(r, &boopie_font_pixel_24, INK, text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, tappable ? 22 : 6, 0);
    if (tappable) {
        lv_obj_t *arrow = label(r, &lv_font_montserrat_16, INK, LV_SYMBOL_PLAY);
        lv_obj_align(arrow, LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    }
    if (value) {
        lv_obj_t *v = label(r, &lv_font_montserrat_16, 0x6870a0, value);
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, -4, 0);
    }
    if (index >= 0 && index < 12) {
        s_rows[index] = r;
    }
    return r;
}

static void note(lv_obj_t *box, const char *text)
{
    lv_obj_t *l = label(box, &lv_font_montserrat_16, 0x6870a0, text);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
}

/* ---- the games (the TV) ---- */

static const char *const GAME_IDS[] = { "whack", "catch", "maze", "hop" };
static const char *const GAME_NAMES[] = { "戳戳布比", "接零食", "重力迷宫", "跳跳布比" };

static void on_game(int i)
{
    close_panel();
    boopie_games_open_locked(GAME_IDS[i]);
}

static void games_panel(void)
{
    lv_obj_t *box = panel("玩什么？", on_game);
    for (int i = 0; i < 4; i++) {
        row(box, i, GAME_NAMES[i], NULL, true);
    }
}

/* ---- the bookshelf ---- */

static void on_book(int i)
{
    close_panel();
    if (i == 0) {
        boopie_viewer_album_locked();
    } else {
        boopie_viewer_chat_locked();
    }
}

static void books_panel(void)
{
    lv_obj_t *box = panel("书架", on_book);
    row(box, 0, "相册", NULL, true);
    row(box, 1, "聊天记录", NULL, true);
}

/* ---- 功能 ---- */

static void on_function(int i)
{
    close_panel();
    switch (i) {
    case 0: games_panel(); break;
    case 1: boopie_noise_ui_open_locked(); break;
    case 2:
        if (s_world.room != BOOPIE_ROOM_OUTSIDE) {
            boopie_world_enter(&s_world, BOOPIE_ROOM_OUTSIDE, BOOPIE_DO_OUTSIDE);
        }
        say("去农场看看～");
        break;
    case 3: books_panel(); break;
    case 4: muse_ui_open_settings("avatar"); break;
    default: muse_ui_open_settings(NULL); break;
    }
}

static void function_panel(void)
{
    lv_obj_t *box = panel("功能", on_function);
    row(box, 0, "小游戏", NULL, true);
    row(box, 1, "白噪音", NULL, true);
    row(box, 2, "去农场", NULL, true);
    row(box, 3, "相册和聊天记录", NULL, true);
    row(box, 4, "换装", NULL, true);
    row(box, 5, "设置", NULL, true);
}

/* ---- 背包 ---- */

static void bag_panel(void);

static void on_bag(int i)
{
    if (i == BOOPIE_ITEM_POPPER) {
        if (boopie_avatar_pop()) {
            close_panel();
            muse_ui_show_face();   /* the confetti's on its face */
        }
        return;
    }
    bool fed = false;
    if (!boopie_avatar_snack(i, &fed)) {
        return;
    }
    say(fed ? "好吃！吃饱啦" : "%s真好吃～", boopie_item_name((boopie_item_t)i));
    bag_panel();
}

static void bag_panel(void)
{
    lv_obj_t *box = panel("背包", on_bag);
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    char v[32];
    snprintf(v, sizeof v, "%u", (unsigned)st.stars);
    row(box, -1, "星星", v, false);
    int any = 0;
    for (int i = 0; i < BOOPIE_ITEM_COUNT; i++) {
        int n = boopie_avatar_items(i);
        if (n) {
            bool edible = boopie_item_edible((boopie_item_t)i), popper = i == BOOPIE_ITEM_POPPER;
            snprintf(v, sizeof v, edible ? "× %d 喂它" : popper ? "× %d 放一个" : "× %d", n);
            row(box, edible || popper ? i : -1, boopie_item_name((boopie_item_t)i), v, edible || popper);
            any++;
        }
    }
    int64_t now;
    int minute;
    boopie_garden_t *g = boopie_avatar_garden(&now, &minute);
    for (int p = 1; g && p < BOOPIE_PLANT_COUNT; p++) {
        if (g->harvested[p]) {
            snprintf(v, sizeof v, "收获 %u", (unsigned)g->harvested[p]);
            row(box, -1, boopie_plant_name((boopie_plant_t)p), v, false);
            any++;
        }
    }
    int owned = 0;
    for (int s = 0; s < boopie_skin_count(); s++) {
        owned += boopie_avatar_owns(s);
    }
    snprintf(v, sizeof v, "%d 件", owned);
    row(box, -1, "皮肤", v, false);
    note(box, any ? "点吃的喂给它：饿了能当一顿饭，商店买的零食还加经验。森林里摘蓝莓蘑菇，海边捡贝壳、钓鱼。"
                  : "森林里每天能摘蓝莓和蘑菇，海边能捡贝壳、钓鱼。");
}

/* ---- 商店: the current character's skins, for stars ---- */

static int s_shop_skins[12];
static int s_shop_count;
static void skins_panel(void);

/* Tapped once: the row lit and asking for a second tap, which buys (false). */
static bool arm(int i)
{
    if (s_armed == i) {
        return false;
    }
    s_armed = i;
    lv_obj_t *r = i >= 0 && i < 12 ? s_rows[i] : NULL;
    if (r) {
        lv_obj_set_style_bg_color(r, lv_color_hex(0xfff0b8), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_t *v = lv_obj_get_child(r, lv_obj_get_child_count(r) - 1);
        lv_label_set_text(v, "再点一次买");
    }
    return true;
}

static void on_shop(int i)
{
    if (i < 0 || i >= s_shop_count) {
        return;
    }
    int skin = s_shop_skins[i];
    const char *error = NULL;
    if (boopie_avatar_owns(skin)) {
        bool wearing = boopie_avatar_skin() == skin;
        if (boopie_avatar_wear(wearing ? -1 : skin, &error)) {
            say(wearing ? "换回原来的样子啦" : "好看吗？");
        }
        skins_panel();
        return;
    }
    if (arm(i)) {
        return;
    }
    if (boopie_avatar_buy(skin, &error)) {
        boopie_sound_play(BOOPIE_SOUND_GOLD);
        say("买到啦！%s", boopie_skin_name(skin));
    } else {
        boopie_sound_play(BOOPIE_SOUND_ERROR);
        char why[64];
        if (boopie_avatar_skin_goal(skin) != BOOPIE_GOAL_NONE) {
            boopie_avatar_goal_text(boopie_avatar_skin_goal(skin), why, sizeof why);
            say("这是成就奖励：%s", why);
        } else {
            say("%s", !boopie_avatar_skin_on_sale(skin) ? "节日期间才能买哦" : "星星不够，再攒攒吧");
        }
    }
    skins_panel();
}

static void skins_panel(void)
{
    int armed = s_armed;
    lv_obj_t *box = panel("皮肤", on_shop);
    s_armed = armed;
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    char v[48];
    snprintf(v, sizeof v, "你有 ★ %u", (unsigned)st.stars);
    note(box, v);
    s_shop_count = 0;
    int cur = boopie_avatar_current();
    for (int s = 0; s < boopie_skin_count() && s_shop_count < 12; s++) {
        if (boopie_avatar_of_skin(s) != cur) {
            continue;
        }
        boopie_goal_t goal = boopie_avatar_skin_goal(s);
        if (boopie_avatar_owns(s)) {
            snprintf(v, sizeof v, "%s", boopie_avatar_skin() == s ? "穿着" : "已有");
        } else if (goal != BOOPIE_GOAL_NONE) {
            if (boopie_avatar_goal(goal, NULL, NULL)) {
                snprintf(v, sizeof v, "免费领取");
            } else {
                boopie_avatar_goal_text(goal, v, sizeof v);
            }
        } else if (!boopie_avatar_skin_on_sale(s)) {
            snprintf(v, sizeof v, "%s", boopie_avatar_skin_when(s));
        } else {
            snprintf(v, sizeof v, "★ %d%s", boopie_skin_price(s), *boopie_avatar_skin_when(s) ? " 限定" : "");
        }
        row(box, s_shop_count, boopie_skin_name(s), v, true);
        s_shop_skins[s_shop_count++] = s;
    }
    if (!s_shop_count) {
        note(box, "这个角色还没有皮肤卖。换个伙伴看看？");
    } else {
        note(box, "点一下看价格，再点一次买；已有的点一下穿上。");
    }
}

/* ---- 商店: furniture, each to its own place ---- */

static void furni_panel(void);

static void on_furni(int i)
{
    if (i < 0 || i >= BOOPIE_FURNI_COUNT) {
        return;
    }
    boopie_furni_t f = (boopie_furni_t)i;
    if (boopie_avatar_furni_owned(f)) {
        bool out = boopie_avatar_furniture() >> f & 1;
        boopie_avatar_put_out(f, !out);
        say(out ? "%s收起来了" : "%s摆在%s啦", boopie_furni_name(f), boopie_furni_where(f));
        furni_panel();
        return;
    }
    if (arm(i)) {
        return;
    }
    const char *error = NULL;
    if (boopie_avatar_buy_furni(f, boopie_furni_price(f), &error)) {
        boopie_sound_play(BOOPIE_SOUND_GOLD);
        say("买到%s！摆在%s了", boopie_furni_name(f), boopie_furni_where(f));
    } else {
        boopie_sound_play(BOOPIE_SOUND_ERROR);
        say("%s", error ? error : "买不了");
    }
    furni_panel();
}

static void furni_panel(void)
{
    int armed = s_armed;
    lv_obj_t *box = panel("家具", on_furni);
    s_armed = armed;
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    char v[48];
    snprintf(v, sizeof v, "你有 ★ %u", (unsigned)st.stars);
    note(box, v);
    uint32_t out = boopie_avatar_furniture();
    for (int f = 0; f < BOOPIE_FURNI_COUNT; f++) {
        char name[48];
        snprintf(name, sizeof name, "%s·%s", boopie_furni_name((boopie_furni_t)f), boopie_furni_where((boopie_furni_t)f));
        if (boopie_avatar_furni_owned(f)) {
            snprintf(v, sizeof v, "%s", out >> f & 1 ? "摆着" : "收着");
        } else {
            snprintf(v, sizeof v, "★ %d", boopie_furni_price((boopie_furni_t)f));
        }
        row(box, f, name, v, true);
    }
    note(box, "点一下看价格，再点一次买，买了就摆好；已有的点一下收起或摆出来。");
}

/* ---- 商店: rare seeds, one at a time ---- */

static void seed_shop_panel(void);
static int s_seed_rows[8], s_seed_count;

static void on_seed_shop(int i)
{
    if (i < 0 || i >= s_seed_count) {
        return;
    }
    boopie_plant_t p = (boopie_plant_t)s_seed_rows[i];
    if (arm(i)) {
        return;
    }
    const char *error = NULL;
    if (boopie_avatar_buy_seed(p, boopie_plant_price(p), &error)) {
        boopie_sound_play(BOOPIE_SOUND_GOLD);
        say("买到%s种子！去农场种吧", boopie_plant_name(p));
    } else {
        boopie_sound_play(BOOPIE_SOUND_ERROR);
        say("%s", error ? error : "买不了");
    }
    s_armed = -1;
    seed_shop_panel();
}

static void seed_shop_panel(void)
{
    int armed = s_armed;
    lv_obj_t *box = panel("种子", on_seed_shop);
    s_armed = armed;
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    char v[48];
    snprintf(v, sizeof v, "你有 ★ %u", (unsigned)st.stars);
    note(box, v);
    s_seed_count = 0;
    for (int p = 1; p < BOOPIE_PLANT_COUNT && s_seed_count < 8; p++) {
        int price = boopie_plant_price((boopie_plant_t)p);
        if (!price) {
            continue;
        }
        char name[48];
        snprintf(name, sizeof name, "%s种子 ×%d", boopie_plant_name((boopie_plant_t)p), boopie_avatar_seeds(p));
        snprintf(v, sizeof v, "★ %d", price);
        row(box, s_seed_count, name, v, true);
        s_seed_rows[s_seed_count++] = p;
    }
    note(box, "一次买一颗，收获的星星比种子价钱多，经验也多。普通种子不用买。");
}

/* ---- 商店: treats and poppers, one at a time ---- */

static void treat_panel(void);
static const boopie_item_t TREATS[] = { BOOPIE_ITEM_COOKIE, BOOPIE_ITEM_CAKE, BOOPIE_ITEM_POPPER };

static void on_treat(int i)
{
    if (i < 0 || i >= (int)(sizeof TREATS / sizeof *TREATS)) {
        return;
    }
    if (arm(i)) {
        return;
    }
    const char *error = NULL;
    if (boopie_avatar_buy_item(TREATS[i], &error)) {
        boopie_sound_play(BOOPIE_SOUND_GOLD);
        say("买到%s！放进背包啦", boopie_item_name(TREATS[i]));
    } else {
        boopie_sound_play(BOOPIE_SOUND_ERROR);
        say("%s", error ? error : "买不了");
    }
    s_armed = -1;
    treat_panel();
}

static void treat_panel(void)
{
    int armed = s_armed;
    lv_obj_t *box = panel("零食", on_treat);
    s_armed = armed;
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    char v[48];
    snprintf(v, sizeof v, "你有 ★ %u", (unsigned)st.stars);
    note(box, v);
    for (int i = 0; i < (int)(sizeof TREATS / sizeof *TREATS); i++) {
        char name[48];
        snprintf(name, sizeof name, "%s ×%d", boopie_item_name(TREATS[i]), boopie_avatar_items(TREATS[i]));
        snprintf(v, sizeof v, "★ %d", boopie_item_price(TREATS[i]));
        row(box, i, name, v, true);
    }
    note(box, "饼干和蛋糕在背包里喂它，加经验（不占每天的上限）；礼炮在背包里放，撒花庆祝。");
}

static void on_shop_menu(int i)
{
    if (i == 0) {
        furni_panel();
    } else if (i == 1) {
        seed_shop_panel();
    } else if (i == 3) {
        treat_panel();
    } else {
        skins_panel();
    }
}

static void shop_panel(void)
{
    lv_obj_t *box = panel("商店", on_shop_menu);
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    char v[48];
    snprintf(v, sizeof v, "你有 ★ %u", (unsigned)st.stars);
    note(box, v);
    row(box, 0, "家具", NULL, true);
    row(box, 1, "种子", NULL, true);
    row(box, 3, "零食", NULL, true);
    row(box, 2, "皮肤", NULL, true);
    note(box, "家具摆进家里、院子和农场；稀有种子拿去农场种；零食喂它、礼炮庆祝；皮肤给现在的伙伴穿。");
}

/* ---- the desk: the pet's status ---- */

/* ---- its birthday: a month, then a day ---- */

static int s_birth_month;
static void status_panel(void);
static void birthday_month_panel(void);

static void on_birth_day(int i)
{
    if (boopie_avatar_set_birthday(s_birth_month, i + 1)) {
        boopie_sound_play(BOOPIE_SOUND_SCORE);
        say("记住啦！%d月%d日是我的生日", s_birth_month, i + 1);
    }
    status_panel();
}

static void on_birth_month(int i)
{
    static const int DAYS[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    s_birth_month = i + 1;
    char title[24];
    snprintf(title, sizeof title, "%d月几日？", s_birth_month);
    lv_obj_t *box = panel(title, on_birth_day);
    for (int d = 1; d <= DAYS[i]; d++) {
        char v[16];
        snprintf(v, sizeof v, "%d日", d);
        row(box, d - 1, v, NULL, true);
    }
}

static void birthday_month_panel(void)
{
    lv_obj_t *box = panel("生日是几月？", on_birth_month);
    for (int m = 1; m <= 12; m++) {
        char v[16];
        snprintf(v, sizeof v, "%d月", m);
        row(box, m - 1, v, NULL, true);
    }
}

static void on_status(int i)
{
    if (i == 0) {
        birthday_month_panel();
    }
}

static void status_panel(void)
{
    lv_obj_t *box = panel(boopie_avatar_pet_name(), on_status);
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    char v[48];
    snprintf(v, sizeof v, "Lv %d", st.level);
    row(box, -1, "等级", v, false);
    snprintf(v, sizeof v, "%u / %u", (unsigned)st.xp_into, (unsigned)st.xp_need);
    row(box, -1, "经验", v, false);
    snprintf(v, sizeof v, "★ %u", (unsigned)st.stars);
    row(box, -1, "星星", v, false);
    row(box, -1, "心情", st.hungry ? "饿了" : st.mood == BOOPIE_EXPR_SLEEPY ? "困了"
                                                : st.mood == BOOPIE_EXPR_SAD ? "想你了" : "很好", false);
    snprintf(v, sizeof v, "%u 只", (unsigned)boopie_avatar_slimes_beaten());
    row(box, -1, "打败史莱姆", v, false);
    int bm, bd;
    if (boopie_avatar_birthday(&bm, &bd)) {
        snprintf(v, sizeof v, "%d月%d日", bm, bd);
    } else {
        snprintf(v, sizeof v, "点这里设");
    }
    row(box, 0, "生日", v, true);
    note(box, "升级后家里会添新家具，屋外和森林也会变样。生日那天有蛋糕和礼物。");
}

/* ---- the mirror: a new name ---- */

static void named(const char *text, bool done)
{
    const char *error = NULL;
    if (done && boopie_avatar_set_pet_name(text, &error)) {
        say("以后就叫我%s吧！", boopie_avatar_pet_name());
    } else if (done) {
        say("这个名字用不了");
    }
}

#define DONE_FOR_TODAY "今天玩得很开心，明天再来吧"

/* After a reward's own words: halved past the day's cap, or none at all. */
static const char *tired_note(void)
{
    int t = boopie_avatar_tired();
    return t == 2 ? "\n" DONE_FOR_TODAY : t == 1 ? "（今天玩了好多，减半）" : "";
}

/* ---- the farm: a plot to plant, water or pick ---- */

static int s_plot = -1;   /* the plot the seeds panel is for */

static void on_seed(int i)
{
    int64_t now;
    int minute;
    boopie_garden_t *g = boopie_avatar_garden(&now, &minute);
    boopie_plant_t plant = (boopie_plant_t)(i + 1);
    bool rare = boopie_plant_price(plant) > 0;
    if (rare && !boopie_avatar_seeds(plant)) {
        say("没有%s种子了，去商店买吧", boopie_plant_name(plant));
        return;
    }
    close_panel();
    if (g && s_plot >= 0 && boopie_garden_plant(g, s_plot, plant, now)) {
        if (rare) {
            boopie_avatar_use_seed(plant);
        }
        boopie_avatar_garden_changed(0, 0);
        boopie_sound_play(BOOPIE_SOUND_WATER);
        say("种下%s啦！每天浇一次水", boopie_plant_name(plant));
    }
    s_plot = -1;
}

static void seeds_panel(int plot)
{
    lv_obj_t *box = panel("种什么？", on_seed);
    s_plot = plot;
    for (int p = 1; p < BOOPIE_PLANT_COUNT; p++) {
        char v[24];
        if (boopie_plant_price((boopie_plant_t)p) > 0) {
            int n = boopie_avatar_seeds(p);
            if (!n) {
                continue;   /* rare, and none bought */
            }
            snprintf(v, sizeof v, "%d 天 · 剩 %d", boopie_plant_days((boopie_plant_t)p), n);
        } else {
            snprintf(v, sizeof v, "%d 天", boopie_plant_days((boopie_plant_t)p));
        }
        row(box, p - 1, boopie_plant_name((boopie_plant_t)p), v, true);
    }
    note(box, "种下就浇好了水。土干了它就停下来等你，不会枯死。南瓜、西瓜、蓝玫瑰的种子在商店里买。");
}

static void use_plot(int plot)
{
    int64_t now;
    int minute;
    boopie_garden_t *g = boopie_avatar_garden(&now, &minute);
    if (!g) {
        say("还没对上时间，连上网再来种吧");
        return;
    }
    const char *name = boopie_plant_name((boopie_plant_t)g->pots[plot].plant);
    switch (boopie_garden_stage(g, plot)) {
    case BOOPIE_STAGE_EMPTY:
        seeds_panel(plot);
        return;
    case BOOPIE_STAGE_BLOOM: {
        int xp = 0, stars = 0;
        if (boopie_garden_harvest(g, plot, &xp, &stars)) {
            boopie_sound_play(BOOPIE_SOUND_GOLD);
            int got = boopie_avatar_garden_changed(xp, stars);
            say("收获%s！★ +%d%s", name, got, tired_note());
        }
        return;
    }
    default:
        break;
    }
    if (boopie_garden_water(g, plot, now)) {
        boopie_sound_play(BOOPIE_SOUND_WATER);
        boopie_avatar_garden_changed(0, 0);
        say("浇好水啦");
        return;
    }
    unsigned h = (unsigned)((boopie_garden_left_s(g, plot) + 3599) / 3600);
    if (h >= 24) {
        say("%s还要 %u 天 %u 小时开花", name, h / 24, h % 24);
    } else {
        say("%s还要 %u 小时开花", name, h);
    }
}

/* ---- 森林: chests and slimes ---- */

static const char *const SLIME_NAMES[BOOPIE_SLIME_KINDS] = { "绿史莱姆", "蓝史莱姆", "粉史莱姆", "金史莱姆" };
static bool s_woods_told;   /* how the slimes go, said the first time in */

static void open_chest(int chest)
{
    int stars, xp;
    if (boopie_avatar_open_chest(chest, &stars, &xp)) {
        boopie_sound_play(BOOPIE_SOUND_GOLD);
        if (stars > 0) {
            say("宝箱里有 ★ %d！%s", stars, tired_note());
        } else {
            say("宝箱里空空的…\n" DONE_FOR_TODAY);
        }
    } else if (boopie_avatar_chests_open() >> chest & 1) {
        say("今天开过啦，明天再来");
    } else {
        say("还没对上时间，连上网再来开吧");
    }
}

static void gather(int spot)
{
    boopie_item_t item = boopie_gather_item(spot);
    if (boopie_avatar_gather(spot, item)) {
        boopie_sound_play(BOOPIE_SOUND_SCORE);
        say(item == BOOPIE_ITEM_SHELL ? "捡到%s！放进背包啦" : "摘到%s！放进背包啦", boopie_item_name(item));
    } else if (boopie_avatar_gathered() >> spot & 1) {
        say(item == BOOPIE_ITEM_SHELL ? "今天捡过啦，明天浪会冲来新的" : "今天摘过啦，明天再长出来");
    } else {
        say("还没对上时间，连上网再来摘吧");
    }
}

static void use_furni(int f)
{
    switch (f) {
    case BOOPIE_FURNI_CLOCK: {
        const char *clock = boopie_pages_clock();
        if (clock && *clock) {
            say("现在是 %s", clock);
        } else {
            say("钟还没对时");
        }
        break;
    }
    case BOOPIE_FURNI_RECORD:
        boopie_sound_play(BOOPIE_SOUND_NOTIFY);
        boopie_avatar_react(BOOPIE_EXPR_HAPPY, 3.0f);
        say("♪ 啦啦啦～ 一起跳舞吧");
        break;
    case BOOPIE_FURNI_TEDDY:
        boopie_avatar_stroke(0, true);
        say("抱抱小熊～");
        break;
    case BOOPIE_FURNI_SWING:
        boopie_avatar_react(BOOPIE_EXPR_HAPPY, 3.0f);
        say("荡秋千～ 好高呀！");
        break;
    case BOOPIE_FURNI_WINDMILL: say("风车呼呼地转"); break;
    case BOOPIE_FURNI_GRILL:
        boopie_avatar_react(BOOPIE_EXPR_HAPPY, 3.0f);
        say("烤棉花糖～ 好香！");
        break;
    case BOOPIE_FURNI_CAMPFIRE:
        boopie_avatar_react(BOOPIE_EXPR_HAPPY, 3.0f);
        say("围着营火，暖暖的");
        break;
    case BOOPIE_FURNI_TENT: say("在帐篷里躲一会儿，听风吹树叶"); break;
    case BOOPIE_FURNI_GNOME: say("小矮人在看着农场"); break;
    case BOOPIE_FURNI_SURFBOARD: say("等浪大一点就去冲浪！"); break;
    case BOOPIE_FURNI_KITE: say("风筝飞得好高"); break;
    default: say("好漂亮"); break;
    }
}

static void fight_over(void)
{
    if (!s_panel && !busy()) {
        muse_ui_set_swipe_enabled(true);
    }
}

static void slime_won(void)
{
    boopie_slime_kind_t kind = (boopie_slime_kind_t)s_world.last_slime;
    int got = 0;
    boopie_avatar_slime_beaten(boopie_slime_stars(kind), boopie_slime_xp(kind), &got);
    boopie_sound_play(BOOPIE_SOUND_GOLD);
    if (got > 0) {
        say("打败了%s！★ +%d%s", SLIME_NAMES[kind], got, tired_note());
    } else {
        say("打败了%s！%s", SLIME_NAMES[kind], tired_note());
    }
    fight_over();
}

static void slime_fight(void)
{
    const boopie_slime_t *s = &s_world.slimes[s_world.fight];
    muse_ui_set_swipe_enabled(false);   /* every tap is for the slime */
    boopie_sound_play(BOOPIE_SOUND_POKE);
    say("%s！%d 秒内点中它 %d 次", SLIME_NAMES[s->kind], (int)BOOPIE_SLIME_FIGHT_S, s->hp);
}

/* ---- 海边: fishing off the pier ---- */

static void caught(void)
{
    int got = 0;
    switch (s_world.last_fish) {
    case BOOPIE_FISH_BIG:
        boopie_avatar_world_reward(BOOPIE_ITEM_FISH, 2, 10, 0, &got);
        boopie_sound_play(BOOPIE_SOUND_GOLD);
        say("好大一条鱼！小鱼 +2");
        break;
    case BOOPIE_FISH_GOLD:
        boopie_avatar_world_reward(-1, 0, 15, 3, &got);
        boopie_sound_play(BOOPIE_SOUND_GOLD);
        if (got) {
            say("金色的鱼！★ +%d%s", got, tired_note());
        } else {
            say("金色的鱼！摸一摸放回去啦%s", tired_note());
        }
        break;
    case BOOPIE_FISH_BOOT:
        boopie_sound_play(BOOPIE_SOUND_POKE);
        say("钓到一只旧靴子……");
        break;
    default:
        boopie_avatar_world_reward(BOOPIE_ITEM_FISH, 1, 5, 0, &got);
        boopie_sound_play(BOOPIE_SOUND_SCORE);
        say("钓到一条小鱼！放进背包啦");
        break;
    }
    fight_over();
}

/* ---- its little somethings: a word as it begins one ---- */

static void antic_said(void)
{
    if (s_t - s_said_at < SAY_S) {
        return;   /* something's being said already */
    }
    switch (s_world.antic) {
    case BOOPIE_ANTIC_TV: say("看会儿电视～"); break;
    case BOOPIE_ANTIC_DANCE: say("♪ 跳舞跳舞～"); break;
    case BOOPIE_ANTIC_MIRROR: say("镜子里的我真可爱"); break;
    case BOOPIE_ANTIC_LOVE:
        say(s_world.antic_art == BOOPIE_ART_TEDDY      ? "抱抱小熊～"
            : s_world.antic_art == BOOPIE_ART_FLOWERBED ? "花好香呀"
                                                        : "我堆的沙堡！");
        break;
    case BOOPIE_ANTIC_SWING: say("荡秋千咯～"); break;
    case BOOPIE_ANTIC_BUTTERFLY: say("蝴蝶！等等我～"); break;
    case BOOPIE_ANTIC_SWIM: say("下水游泳咯！"); break;
    default: break;
    }
}

bool boopie_world_ui_antic(const char *name)
{
    static const char *const NAMES[BOOPIE_ANTIC_COUNT] = { "", "tv", "dance", "mirror", "love", "swing", "butterfly",
                                                           "swim" };
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    for (int a = 1; a < BOOPIE_ANTIC_COUNT; a++) {
        if (!strcmp(name, NAMES[a])) {
            return boopie_world_antic(&s_world, st.level, (boopie_antic_t)a);
        }
    }
    return false;
}

/* ---------------------------------------------------------------- doing things */

static void feed(void)
{
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    if (!st.hungry) {
        say("还不饿呢");
        return;
    }
    /* As a tap on the face's food: the middle of its bowl. */
    boopie_avatar_tap((BOOPIE_FOOD_X0 + BOOPIE_FOOD_X1) / 2, (BOOPIE_FOOD_Y0 + BOOPIE_FOOD_Y1) / 2);
    say("好吃！谢谢你～");
}

static void act(boopie_do_t what, int arg)
{
    muse_state_poke();
    switch (what) {
    case BOOPIE_DO_PLOT: use_plot(arg); break;
    case BOOPIE_DO_MAIL: {
        int stars = boopie_avatar_open_mail();
        if (stars) {
            static const char *const GIFT[BOOPIE_FEST_COUNT] = { "", "红包", "月饼", "糖果", "圣诞礼物", "新年贺卡",
                                                                 "巧克力", "粽子", "小玩具", "生日礼物", "汤圆" };
            boopie_sound_play(BOOPIE_SOUND_GOLD);
            say("%s快乐！收到%s ★ +%d", boopie_fest_name(boopie_avatar_festival()), GIFT[boopie_avatar_festival()], stars);
        } else {
            say("没有新的信");
        }
        break;
    }
    case BOOPIE_DO_DECOR: {
        if (arg >= 16) {
            say(arg - 16 == BOOPIE_WEATHER_SNOW ? "我们堆的雪人！" : "踩水坑～啪嗒啪嗒");
        } else {
            static const char *const HELLO[BOOPIE_FEST_COUNT] = {
                "", "新年快乐！恭喜发财！", "中秋快乐！月饼真香", "不给糖就捣蛋！", "圣诞快乐！", "元旦快乐！",
                "情人节快乐！最喜欢你了", "端午安康！粽子好香", "儿童节快乐！", "今天是我的生日！谢谢你～",
                "元宵快乐！一起吃汤圆",
            };
            say("%s", HELLO[arg < BOOPIE_FEST_COUNT ? arg : 0]);
        }
        break;
    }
    case BOOPIE_DO_WILD:
        if (!s_woods_told) {
            s_woods_told = true;
            say("进森林咯！点史莱姆就能和它玩");
        } else {
            say("进森林咯！");
        }
        break;
    case BOOPIE_DO_HOME_PATH: say("回到家门口啦"); break;
    case BOOPIE_DO_CHEST: open_chest(arg); break;
    case BOOPIE_DO_GATHER: gather(arg); break;
    case BOOPIE_DO_ANTIC: antic_said(); break;
    case BOOPIE_DO_SHELTER: say("下雨啦，回家躲雨～"); break;
    case BOOPIE_DO_BEACH: say("到海边啦！去码头钓鱼吧"); break;
    case BOOPIE_DO_FISH:
        muse_ui_set_swipe_enabled(false);   /* every tap is for the line */
        say("抛竿～ 浮漂一沉就点屏幕");
        break;
    case BOOPIE_DO_FISH_BITE:
        boopie_sound_play(BOOPIE_SOUND_NOTIFY);
        say("上钩了！快点！");
        break;
    case BOOPIE_DO_FISH_CAUGHT: caught(); break;
    case BOOPIE_DO_FISH_EARLY:
        say("太早啦，鱼吓跑了");
        fight_over();
        break;
    case BOOPIE_DO_FISH_MISSED:
        say("鱼跑掉了…再试一次");
        fight_over();
        break;
    case BOOPIE_DO_FURNI: use_furni(arg); break;
    case BOOPIE_DO_SLIME_FIGHT: slime_fight(); break;
    case BOOPIE_DO_SLIME_WIN: slime_won(); break;
    case BOOPIE_DO_SLIME_FLED:
        say("%s溜走了…下次快一点", SLIME_NAMES[s_world.last_slime]);
        fight_over();
        break;
    case BOOPIE_DO_INSIDE: say("回家咯"); break;
    case BOOPIE_DO_GAMES: games_panel(); break;
    case BOOPIE_DO_BOOKS: books_panel(); break;
    case BOOPIE_DO_RADIO: boopie_noise_ui_open_locked(); break;
    case BOOPIE_DO_FEED: feed(); break;
    case BOOPIE_DO_UPSTAIRS: say("到二楼啦"); break;
    case BOOPIE_DO_DOWNSTAIRS: say("下楼咯"); break;
    case BOOPIE_DO_OUTSIDE:
        say(boopie_avatar_weather() == BOOPIE_WEATHER_RAIN   ? "下雨啦，撑把伞～"
            : boopie_avatar_weather() == BOOPIE_WEATHER_SNOW ? "哇，下雪了！"
                                                             : "出门啦！");
        break;
    case BOOPIE_DO_SLEEP:
        if (s_world.state == BOOPIE_PET_SLEEPING) {
            say("晚安～");
        } else {
            say("睡醒啦！");
        }
        break;
    case BOOPIE_DO_WARDROBE: muse_ui_open_settings("avatar"); break;
    case BOOPIE_DO_RENAME:
        boopie_input_open("给它起个名字", boopie_avatar_has_own_name() ? boopie_avatar_pet_name() : "",
                          "留空就叫角色名", BOOPIE_PET_NAME_CHARS, named);
        break;
    case BOOPIE_DO_STATUS: status_panel(); break;
    default: break;
    }
}

static void on_tap(lv_event_t *e)
{
    lv_indev_t *in = lv_event_get_indev(e);
    if (!in || s_panel) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_area_t a;
    lv_obj_get_coords(s_image, &a);
    float x = (float)(p.x - a.x1 + 1) / 3 + s_world.cam, y = (float)(p.y - a.y1 + 1) / 3;
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    int r = boopie_world_tap(&s_world, st.level, x, y);
    if (r == -3) {
        say("嗯…早上了吗？");
    } else if (r == -7 || r == -8) {
        /* reeled in: what came up is said next frame */
    } else if (r == -5) {
        boopie_sound_play(BOOPIE_SOUND_SCORE);   /* a hit */
    } else if (r == -4) {
        say("冲呀！");
        muse_state_poke();
    } else if (r >= 0) {
        muse_state_poke();
    }
}

static void on_button(lv_event_t *e)
{
    switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case 0: function_panel(); break;
    case 1: bag_panel(); break;
    default: shop_panel(); break;
    }
}

/* ---------------------------------------------------------------- frames */

static bool shown(void)
{
    lv_obj_t *tv = lv_obj_get_parent(s_tile);
    return tv && lv_tileview_get_tile_active(tv) == s_tile && !muse_state_asleep();
}

static void frame(lv_timer_t *timer)
{
    (void)timer;
    int64_t now = esp_timer_get_time();
    float dt = (float)(now - s_last_us) / 1e6f;
    s_last_us = now;
    if (!shown()) {
        if (busy()) {
            /* Away mid-fight or fishing (the screen off, another page): it's over, and the swipe back. */
            s_world.fight_left = 0;
            boopie_world_tick(&s_world, s_world.level, 0);
            s_world.fishing = s_world.bite = false;
            muse_ui_set_swipe_enabled(true);
        }
        return;
    }
    dt = dt > 0.2f ? 0.2f : dt;
    s_t += dt;
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    int hour = 12;
    bool known = local_hour(&hour);
    bool night = known && (hour >= 20 || hour < 6);
    int using = s_world.pending;
    const boopie_thing_t *thing = boopie_world_thing(&s_world, using);
    int arg = thing ? thing->arg : 0;
    boopie_world_set_furniture(boopie_avatar_furniture());
    boopie_weather_t wx = boopie_avatar_weather();
    boopie_fest_t fest = boopie_avatar_festival();
    boopie_world_set_season(fest, wx);
    boopie_do_t d = boopie_world_tick(&s_world, st.level, dt);
    if (d != BOOPIE_DO_NOTHING) {
        act(d, arg);
    }
    /* Late at night, left alone a while, it goes to bed by itself. */
    if (known && (hour >= 22 || hour < 6) && s_world.state == BOOPIE_PET_IDLE && s_world.state_t > 20) {
        boopie_world_sleep(&s_world, st.level, true);
    }
    int avatar = boopie_avatar_current();
    if (avatar != s_pet_for || ((int)s_t & 7) == 0) {
        boopie_pixel_pet_image(avatar == BOOPIE_AVATAR_MUSE ? BOOPIE_SKIN_MUSE : avatar - 1, s_pet);
        s_pet_for = avatar;
    }
    int64_t epoch = 0;
    int minute = 0;
    const boopie_garden_t *garden = boopie_avatar_garden(&epoch, &minute);
    boopie_world_look_t look = { st.level, night, st.hungry, s_t, s_pet, BOOPIE_HEAD_W, BOOPIE_PET_H, garden, epoch,
                                 boopie_avatar_chests_open(), boopie_avatar_gathered(), wx, fest,
                                 boopie_avatar_mail_waiting() };
    boopie_world_draw(&s_world, &look, s_rgb);
    boopie_world_scale(s_rgb, s_screen, SIZE);
    const char *clock = boopie_pages_clock();
    boopie_world_hud(s_screen, SIZE, clock ? clock : "", st.stars);
    lv_obj_invalidate(s_image);

    /* What it says: over its head, for a while, then something new now and then. */
    if (!s_hello) {
        s_hello = true;   /* once a power-up, and only the first few times ever */
        if (boopie_avatar_world_hint()) {
            say("点冒气泡的东西试试看");
        }
        s_chat_at = s_t + CHAT_EVERY_S;
    }
    if (s_t - s_said_at > SAY_S) {
        lv_obj_add_flag(s_say, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_t >= s_chat_at && !s_panel && !busy() && s_t - s_said_at > SAY_S + 2) {
        chatter(&st, hour);
        s_chat_at = s_t + CHAT_EVERY_S + (float)(rand() % 6);
    }
    if (!lv_obj_has_flag(s_say, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_update_layout(s_say);
        int w = lv_obj_get_width(s_say);
        int x = (int)(s_world.x - s_world.cam) * 3 - 1 - w / 2, y = ((int)s_world.y - 20) * 3 - 44;
        x = x < 40 ? 40 : x + w > SIZE - 40 ? SIZE - 40 - w : x;
        y = y < 84 ? 84 : y;
        lv_obj_set_pos(s_say, x, y);
    }
}

/* ---------------------------------------------------------------- building */

static lv_obj_t *button(lv_obj_t *parent, int index, boopie_icon_t icon, const char *name, int x)
{
    /* The icon alone, big, outlined so it reads over the room; it shrinks a little pressed. */
    lv_obj_t *b = lv_image_create(parent);
    const lv_image_dsc_t *art = boopie_icon_outlined(icon, 5);
    lv_image_set_src(b, art);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(b, 10);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -24);
    if (art) {
        lv_obj_set_style_transform_pivot_x(b, art->header.w / 2, 0);
        lv_obj_set_style_transform_pivot_y(b, art->header.h / 2, 0);
    }
    lv_obj_set_style_transform_scale(b, 220, LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, on_button, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    /* Its name above it, on a dark tab so it reads over anything. */
    lv_obj_t *tab = lv_obj_create(parent);
    lv_obj_remove_style_all(tab);
    lv_obj_set_size(tab, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(tab, lv_color_hex(INK), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_80, 0);
    lv_obj_set_style_radius(tab, 6, 0);
    lv_obj_set_style_pad_hor(tab, 4, 0);
    lv_obj_remove_flag(tab, LV_OBJ_FLAG_CLICKABLE);
    label(tab, &boopie_font_pixel_24, 0xffffff, name);
    lv_obj_align_to(tab, b, LV_ALIGN_OUT_TOP_MID, 0, -4);
    return b;
}

void boopie_world_ui_build(lv_obj_t *tile)
{
    s_tile = tile;
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    s_screen = heap_caps_malloc(SIZE * SIZE * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_rgb = heap_caps_malloc(BOOPIE_WORLD_W * BOOPIE_WORLD_W * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_screen || !s_rgb) {
        label(tile, &lv_font_montserrat_20, 0xffffff, "小窝打不开：内存不够");
        return;
    }
    memset(s_screen, 0, SIZE * SIZE * sizeof(uint16_t));
    s_dsc = (lv_image_dsc_t){
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = SIZE, .h = SIZE,
                    .stride = SIZE * sizeof(uint16_t) },
        .data_size = SIZE * SIZE * sizeof(uint16_t),
        .data = (const uint8_t *)s_screen,
    };
    boopie_world_init(&s_world, (uint32_t)esp_timer_get_time() | 1u);
    s_image = lv_image_create(tile);
    lv_image_set_src(s_image, &s_dsc);
    lv_obj_center(s_image);
    lv_obj_add_flag(s_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_image, on_tap, LV_EVENT_SHORT_CLICKED, NULL);

    /* What it says: a dark capsule with a tail toward it. */
    s_say = lv_obj_create(tile);
    lv_obj_remove_style_all(s_say);
    lv_obj_set_size(s_say, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_say, lv_color_hex(INK), 0);
    lv_obj_set_style_bg_opa(s_say, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_say, 18, 0);
    lv_obj_set_style_pad_hor(s_say, 14, 0);
    lv_obj_set_style_pad_ver(s_say, 6, 0);
    lv_obj_remove_flag(s_say, LV_OBJ_FLAG_CLICKABLE);
    s_say_text = label(s_say, &boopie_font_pixel_24, 0xffffff, "");
    lv_obj_add_flag(s_say, LV_OBJ_FLAG_HIDDEN);

    button(tile, 0, BOOPIE_ICON_BOLT, "功能", -74);
    button(tile, 1, BOOPIE_ICON_BAG, "背包", 0);
    button(tile, 2, BOOPIE_ICON_SHOP, "商店", 74);

    /* By the bottom button: it goes back. */
    s_back_hint = lv_obj_create(tile);
    lv_obj_remove_style_all(s_back_hint);
    lv_obj_set_size(s_back_hint, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_back_hint, lv_color_hex(INK), 0);
    lv_obj_set_style_bg_opa(s_back_hint, LV_OPA_70, 0);
    lv_obj_set_style_radius(s_back_hint, 10, 0);
    lv_obj_set_style_pad_hor(s_back_hint, 6, 0);
    lv_obj_set_style_pad_ver(s_back_hint, 2, 0);
    lv_obj_remove_flag(s_back_hint, LV_OBJ_FLAG_CLICKABLE);
    label(s_back_hint, &boopie_font_pixel_24, 0xffffff, "返回");
    lv_obj_t *arrow = label(s_back_hint, &lv_font_montserrat_16, 0xffffff, LV_SYMBOL_DOWN);
    lv_obj_set_flex_flow(s_back_hint, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_back_hint, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_back_hint, 4, 0);
    (void)arrow;
    /* Just inside the screen's edge from the bottom button, clear of the buttons. */
    const muse_button_hint_t *aux = &muse_board->aux_hint;
    lv_obj_align(s_back_hint, LV_ALIGN_CENTER, aux->x - 8, aux->y - 36);

    s_last_us = esp_timer_get_time();
    lv_timer_create(frame, FRAME_MS, NULL);
}

bool boopie_world_ui_back(void)
{
    muse_board->display_lock(-1);
    bool open = s_panel != NULL || busy();
    close_panel();
    if (s_world.fight >= 0) {
        s_world.fight_left = 0;   /* given up: the slime's off */
    }
    if (s_world.fishing) {
        s_world.fishing = s_world.bite = false;   /* the line in */
        muse_ui_set_swipe_enabled(true);
    }
    muse_board->display_unlock();
    return open;
}

bool boopie_world_ui_go(const char *room)
{
    static const char *const NAMES[] = { [BOOPIE_ROOM_LIVING] = "living", [BOOPIE_ROOM_BEDROOM] = "bedroom",
                                         [BOOPIE_ROOM_OUTSIDE] = "outside", [BOOPIE_ROOM_WOODS] = "woods",
                                         [BOOPIE_ROOM_BEACH] = "beach" };
    for (int r = 0; r < (int)(sizeof NAMES / sizeof NAMES[0]); r++) {
        if (NAMES[r] && !strcmp(room, NAMES[r])) {
            boopie_world_enter(&s_world, (boopie_room_t)r, r == BOOPIE_ROOM_BEACH     ? BOOPIE_DO_BEACH
                                                         : r == BOOPIE_ROOM_WOODS   ? BOOPIE_DO_WILD
                                                         : r == BOOPIE_ROOM_OUTSIDE ? BOOPIE_DO_OUTSIDE
                                                         : r == BOOPIE_ROOM_BEDROOM ? BOOPIE_DO_UPSTAIRS
                                                                                    : BOOPIE_DO_INSIDE);
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------------- the AI */

void boopie_world_ui_farm_status(char *out, size_t cap)
{
    static const char *const STAGES[] = { "a seed", "a sprout", "in leaf", "in bud", "in bloom, ready to pick" };
    static const char *const PLANTS[BOOPIE_PLANT_COUNT] = { "", "sunflower", "tulip", "strawberry", "cactus",
                                                            "pumpkin", "watermelon", "blue rose" };
    muse_board->display_lock(-1);
    int64_t now;
    int minute;
    boopie_garden_t *g = boopie_avatar_garden(&now, &minute);
    size_t n = 0;
    if (!g) {
        snprintf(out, cap, "the clock isn't set yet, so the farm can't grow");
    }
    for (int i = 0; g && i < BOOPIE_GARDEN_POTS && n < cap; i++) {
        boopie_stage_t st = boopie_garden_stage(g, i);
        if (st == BOOPIE_STAGE_EMPTY) {
            n += (size_t)snprintf(out + n, cap - n, "plot %d: empty. ", i + 1);
        } else {
            n += (size_t)snprintf(out + n, cap - n, "plot %d: %s, %s, %s, %u h of damp soil to bloom. ", i + 1,
                                  PLANTS[g->pots[i].plant], STAGES[st],
                                  boopie_garden_dry(g, i, now) ? "thirsty" : "watered",
                                  (unsigned)((boopie_garden_left_s(g, i) + 3599) / 3600));
        }
    }
    muse_board->display_unlock();
}

bool boopie_world_ui_set_weather(const char *key, char *said, size_t cap)
{
    boopie_weather_t w;
    if (!boopie_weather_from_key(key, &w)) {
        return false;
    }
    muse_board->display_lock(-1);
    bool ok = boopie_avatar_set_weather(w);
    muse_board->display_unlock();
    snprintf(said, cap, ok ? "the pet's world shows %s today" : "the clock isn't set yet", key);
    return ok;
}
