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
#include "boopie_garden_ui.h"
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
#define GOLD 0xe8c448

static lv_obj_t *s_tile, *s_image, *s_say, *s_say_text, *s_panel, *s_back_hint;
static lv_image_dsc_t s_dsc;
static uint16_t *s_screen;
static uint8_t *s_rgb;
static boopie_world_t s_world;
static int64_t s_last_us;
static float s_t, s_said_at = -100, s_chat_at;
static uint16_t s_pet[BOOPIE_HEAD_W * BOOPIE_HEAD_H];
static int s_pet_for = -1;
static bool s_hello;   /* the first visit's hint has been said */

/* ---------------------------------------------------------------- what it says */

static void say(const char *fmt, ...)
{
    char buf[96];
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

static void close_panel(void)
{
    if (s_panel) {
        lv_obj_delete_async(s_panel);
        s_panel = NULL;
        muse_ui_set_swipe_enabled(true);
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
    case 2: boopie_garden_ui_open_locked(); break;
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
    row(box, 2, "小花园", NULL, true);
    row(box, 3, "相册和聊天记录", NULL, true);
    row(box, 4, "换装", NULL, true);
    row(box, 5, "设置", NULL, true);
}

/* ---- 背包 ---- */

static void bag_panel(void)
{
    lv_obj_t *box = panel("背包", NULL);
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    char v[32];
    snprintf(v, sizeof v, "%u", (unsigned)st.stars);
    row(box, -1, "星星", v, false);
    int64_t now;
    int minute;
    boopie_garden_t *g = boopie_avatar_garden(&now, &minute);
    int any = 0;
    for (int p = 1; g && p < BOOPIE_PLANT_COUNT; p++) {
        if (g->harvested[p]) {
            snprintf(v, sizeof v, "× %u", (unsigned)g->harvested[p]);
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
    note(box, any ? "花园收获的都在这里。更多道具即将到来。" : "小花园收获的东西会放在这里。");
}

/* ---- 商店: the current character's skins, for stars ---- */

static int s_shop_skins[12];
static int s_shop_count;
static void shop_panel(void);

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
        shop_panel();
        return;
    }
    if (s_armed != i) {
        /* Tapped once: say the price; a second tap buys. */
        s_armed = i;
        lv_obj_t *r = s_rows[i];
        if (r) {
            lv_obj_set_style_bg_color(r, lv_color_hex(0xfff0b8), 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_t *v = lv_obj_get_child(r, lv_obj_get_child_count(r) - 1);
            lv_label_set_text(v, "再点一次买");
        }
        return;
    }
    if (boopie_avatar_buy(skin, &error)) {
        boopie_sound_play(BOOPIE_SOUND_GOLD);
        say("买到啦！%s", boopie_skin_name(skin));
    } else {
        boopie_sound_play(BOOPIE_SOUND_ERROR);
        say("%s", error ? error : "买不了");
    }
    shop_panel();
}

static void shop_panel(void)
{
    int armed = s_armed;
    lv_obj_t *box = panel("商店", on_shop);
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
        if (boopie_avatar_owns(s)) {
            snprintf(v, sizeof v, "%s", boopie_avatar_skin() == s ? "穿着" : "已有");
        } else {
            snprintf(v, sizeof v, "★ %d", boopie_skin_price(s));
        }
        row(box, s_shop_count, boopie_skin_name(s), v, true);
        s_shop_skins[s_shop_count++] = s;
    }
    if (!s_shop_count) {
        note(box, "这个角色还没有皮肤卖。换个伙伴看看？");
    } else {
        note(box, "点一下看价格，再点一次买；已有的点一下穿上。家具和种子即将上架。");
    }
}

/* ---- the desk: the pet's status ---- */

static void status_panel(void)
{
    lv_obj_t *box = panel(boopie_avatar_pet_name(), NULL);
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
    note(box, "升级后家里会添新家具：盆栽、沙发、鱼缸……");
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

static void act(boopie_do_t what)
{
    muse_state_poke();
    switch (what) {
    case BOOPIE_DO_GAMES: games_panel(); break;
    case BOOPIE_DO_BOOKS: books_panel(); break;
    case BOOPIE_DO_RADIO: boopie_noise_ui_open_locked(); break;
    case BOOPIE_DO_FEED: feed(); break;
    case BOOPIE_DO_UPSTAIRS: say("到二楼啦"); break;
    case BOOPIE_DO_DOWNSTAIRS: say("下楼咯"); break;
    case BOOPIE_DO_OUTSIDE: say("外面正在建设，很快就能出门啦！"); break;
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
    float x = (float)(p.x - a.x1 + 1) / 3, y = (float)(p.y - a.y1 + 1) / 3;
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    int r = boopie_world_tap(&s_world, st.level, x, y);
    if (r == -3) {
        say("嗯…早上了吗？");
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
        return;
    }
    dt = dt > 0.2f ? 0.2f : dt;
    s_t += dt;
    boopie_pet_status_t st;
    boopie_avatar_pet_status(&st);
    int hour = 12;
    bool known = local_hour(&hour);
    bool night = known && (hour >= 20 || hour < 6);
    boopie_do_t d = boopie_world_tick(&s_world, st.level, dt);
    if (d != BOOPIE_DO_NOTHING) {
        act(d);
    }
    /* Late at night, left alone a while, it goes to bed by itself. */
    if (known && (hour >= 22 || hour < 6) && s_world.state == BOOPIE_PET_IDLE && s_world.state_t > 20) {
        boopie_world_sleep(&s_world, st.level, true);
    }
    int avatar = boopie_avatar_current();
    if (avatar != s_pet_for || ((int)s_t & 7) == 0) {
        boopie_pixel_head_image(avatar == BOOPIE_AVATAR_MUSE ? BOOPIE_SKIN_MUSE : avatar - 1, s_pet, 1);
        s_pet_for = avatar;
    }
    boopie_world_look_t look = { st.level, night, st.hungry, s_t, s_pet, BOOPIE_HEAD_W, BOOPIE_HEAD_H };
    boopie_world_draw(&s_world, &look, s_rgb);
    boopie_world_scale(s_rgb, s_screen, SIZE);
    const char *clock = boopie_pages_clock();
    boopie_world_hud(s_screen, SIZE, clock ? clock : "", st.stars);
    lv_obj_invalidate(s_image);

    /* What it says: over its head, for a while, then something new now and then. */
    if (!s_hello) {
        s_hello = true;
        say("点冒气泡的东西试试看");
        s_chat_at = s_t + CHAT_EVERY_S;
    }
    if (s_t - s_said_at > SAY_S) {
        lv_obj_add_flag(s_say, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_t >= s_chat_at && !s_panel) {
        chatter(&st, hour);
        s_chat_at = s_t + CHAT_EVERY_S + (float)(rand() % 6);
    }
    if (!lv_obj_has_flag(s_say, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_update_layout(s_say);
        int w = lv_obj_get_width(s_say);
        int x = (int)s_world.x * 3 - 1 - w / 2, y = ((int)s_world.y - 20) * 3 - 44;
        x = x < 40 ? 40 : x + w > SIZE - 40 ? SIZE - 40 - w : x;
        y = y < 84 ? 84 : y;
        lv_obj_set_pos(s_say, x, y);
    }
}

/* ---------------------------------------------------------------- building */

static lv_obj_t *button(lv_obj_t *parent, int index, boopie_icon_t icon, const char *name, int x)
{
    /* A gold ring round a blue disc, edged dark, as in the monster games' menus. */
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 52, 52);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(GOLD), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(INK), 0);
    lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_set_style_transform_scale(b, 230, LV_STATE_PRESSED);
    lv_obj_set_style_transform_pivot_x(b, 26, 0);
    lv_obj_set_style_transform_pivot_y(b, 26, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -22);
    lv_obj_t *disc = lv_obj_create(b);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, 38, 38);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(BLUE), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_remove_flag(disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(disc);
    lv_obj_t *img = lv_image_create(disc);
    lv_image_set_src(img, boopie_icon(icon, 2));
    lv_obj_center(img);
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
    lv_obj_align_to(tab, b, LV_ALIGN_OUT_TOP_MID, 0, -2);
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
    bool open = s_panel != NULL;
    close_panel();
    muse_board->display_unlock();
    return open;
}
