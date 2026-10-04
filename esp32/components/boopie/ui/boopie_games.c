/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_games.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "boopie_avatar.h"
#include "boopie_font.h"
#include "boopie_pixel.h"
#include "boopie_sound.h"
#include "boopie_whack.h"
#include "boopie_catch.h"
#include "boopie_maze.h"
#include "boopie_hop.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "muse_board.h"
#include "muse_state.h"
#ifdef ESP_PLATFORM
#include "boopie_imu.h"
#endif

#define CELL 7                        /* screen pixels a grid cell */
#define SIZE (BOOPIE_PX * CELL)       /* 448 */
#define FRAME_MS 33
#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff
#define COLOR_GOLD 0xffd246

/* Tilt, for 接零食 and 重力迷宫: gravity in the screen's frame
 * (boopie_imu_gravity), less where it pulled when the round started. */
#define TILT_DEAD 0.04f   /* g */

typedef enum { G_OFF, G_READY, G_PLAY, G_PAUSE, G_OVER } state_t;

/* What steers: the finger held on the screen (grid cells), else the tilt (g). */
typedef struct {
    bool held;
    float fx, fy;
    float tx, ty;
} steer_t;

typedef struct {
    const char *id, *title, *intro;
    const char *done;   /* the results card's title, when it isn't a record */
    boopie_game_t which;
    void (*start)(uint32_t seed);
    /* Plays dt seconds; the sound to play for it, or BOOPIE_SOUND_COUNT. */
    boopie_sound_t (*tick)(float dt, const steer_t *in);
    void (*tap)(float x, float y);   /* NULL: taps don't play */
    bool (*over)(void);
    int (*score)(void);
    void (*reward)(int score, int *xp, int *stars);
    void (*render)(int head);
    /* The results card's extra line ("过了 3 关"), into out. */
    void (*extra)(char *out, size_t cap);
} game_def_t;

static boopie_whack_t s_whack;
static boopie_catch_t s_catch;
static boopie_maze_t s_maze;
static boopie_hop_t s_hop;

static state_t s_state;
static const game_def_t *s_def;
static steer_t s_steer;
static bool s_hold_armed;   /* a finger down since the round began steers; the one that started it doesn't */
#ifdef ESP_PLATFORM
static float s_bias[3];   /* the accelerometer held level */
#endif
static lv_obj_t *s_root, *s_image, *s_card;
static uint16_t *s_buf;
static lv_image_dsc_t s_dsc;
static lv_timer_t *s_timer;
static int64_t s_last_us;

bool boopie_games_active(void)
{
    return s_state != G_OFF;
}

/* The pet popping up: the character on screen, or Muse's own. */
static int head(void)
{
    int a = boopie_avatar_current();
    return a == BOOPIE_AVATAR_MUSE ? BOOPIE_SKIN_MUSE : a - 1;
}

/* The grid, 7 x 7 a cell, each cell's last row and column dimmer so the pixel grid shows. */
static void scale(void)
{
    const uint8_t *rgb = boopie_pixel_rgb();
    for (int cy = 0; cy < BOOPIE_PX; cy++) {
        for (int cx = 0; cx < BOOPIE_PX; cx++) {
            const uint8_t *c = rgb + (cy * BOOPIE_PX + cx) * 3;
            uint16_t full = (uint16_t)((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3);
            uint16_t dim = (uint16_t)(((c[0] * 184 >> 8) >> 3) << 11 | ((c[1] * 184 >> 8) >> 2) << 5 | (c[2] * 184 >> 8) >> 3);
            for (int y = 0; y < CELL; y++) {
                uint16_t *row = s_buf + (cy * CELL + y) * SIZE + cx * CELL;
                for (int x = 0; x < CELL; x++) {
                    row[x] = (x == CELL - 1 || y == CELL - 1) ? dim : full;
                }
            }
        }
    }
}

static void draw(void)
{
    s_def->render(head());
    scale();
    lv_obj_invalidate(s_image);
}

/* ---- the games ---- */

static void whack_start(uint32_t seed) { boopie_whack_start(&s_whack, seed); }
static boopie_sound_t whack_tick(float dt, const steer_t *in)
{
    (void)in;
    boopie_whack_tick(&s_whack, dt);
    return BOOPIE_SOUND_COUNT;
}
static int s_tap_points;
static void whack_tap(float x, float y) { s_tap_points = boopie_whack_tap(&s_whack, x, y); }
static bool whack_over(void) { return s_whack.over; }
static int whack_score(void) { return s_whack.score; }
static void whack_render(int h) { boopie_pixel_render_whack(&s_whack, h); }
static void whack_extra(char *out, size_t cap) { snprintf(out, cap, "戳中 %d 次", s_whack.hits); }

static void catch_start(uint32_t seed) { boopie_catch_start(&s_catch, seed); }
static boopie_sound_t catch_tick(float dt, const steer_t *in)
{
    /* Held: toward the finger. Tilted: 0.4 g is flat out. */
    float move = in->held ? (in->fx - s_catch.x) / 4 : in->tx * 2.5f;
    boopie_catch_kind_t what = BOOPIE_CATCH_NONE;
    int points = boopie_catch_tick(&s_catch, dt, move, &what);
    if (!points) {
        return BOOPIE_SOUND_COUNT;
    }
    return what == BOOPIE_CATCH_CLOUD ? BOOPIE_SOUND_CLOUD : what == BOOPIE_CATCH_GOLD ? BOOPIE_SOUND_GOLD
                                                                                       : BOOPIE_SOUND_SCORE;
}
static bool catch_over(void) { return s_catch.over; }
static int catch_score(void) { return s_catch.score; }
static void catch_render(int h) { boopie_pixel_render_catch(&s_catch, h); }
static void catch_extra(char *out, size_t cap) { snprintf(out, cap, "接住 %d 个", s_catch.caught); }

static void maze_start(uint32_t seed) { boopie_maze_start(&s_maze, seed); }
static boopie_sound_t maze_tick(float dt, const steer_t *in)
{
    /* Held: leaning toward the finger from the middle. Tilted: 0.4 g is all the way. */
    float tx = in->held ? (in->fx - 32) / 20 : in->tx * 2.5f;
    float ty = in->held ? (in->fy - 32) / 20 : in->ty * 2.5f;
    int level = s_maze.level, stars = s_maze.stars;
    boopie_maze_tick(&s_maze, dt, tx, ty);
    return s_maze.level != level ? BOOPIE_SOUND_LEVEL_UP : s_maze.stars != stars ? BOOPIE_SOUND_GOLD
                                                                                  : BOOPIE_SOUND_COUNT;
}
static bool maze_over(void) { return s_maze.over; }
static int maze_score(void) { return s_maze.score; }
static void maze_render(int h)
{
    (void)h;
    boopie_pixel_render_maze(&s_maze);
}
static void maze_extra(char *out, size_t cap) { snprintf(out, cap, "过了 %d 关  ★ %d", s_maze.level, s_maze.stars); }

static void hop_start(uint32_t seed) { boopie_hop_start(&s_hop, seed); }
static boopie_sound_t hop_tick(float dt, const steer_t *in)
{
    (void)in;
    switch (boopie_hop_tick(&s_hop, dt)) {
    case BOOPIE_HOP_PASSED: return BOOPIE_SOUND_SCORE;
    case BOOPIE_HOP_STAR: return BOOPIE_SOUND_GOLD;
    case BOOPIE_HOP_CRASHED: return BOOPIE_SOUND_CLOUD;
    default: return BOOPIE_SOUND_COUNT;
    }
}
static void hop_tap(float x, float y)
{
    (void)x;
    (void)y;
    boopie_hop_flap(&s_hop);
    s_tap_points = 0;
}
static bool hop_over(void) { return s_hop.over; }
static int hop_score(void) { return s_hop.score; }
static void hop_render(int h) { boopie_pixel_render_hop(&s_hop, h); }
static void hop_extra(char *out, size_t cap) { snprintf(out, cap, "过了 %d 根  ★ %d", s_hop.passed, s_hop.stars); }

static const game_def_t GAMES[] = {
    { "whack", "戳戳布比", "宠物冒头就戳它\n别戳小乌云！\n\n点一下开始", "时间到", BOOPIE_GAME_WHACK, whack_start, whack_tick,
      whack_tap, whack_over, whack_score, boopie_whack_reward, whack_render, whack_extra },
    { "catch", "接零食", "左右倾斜或按住屏幕\n接住掉下来的零食\n躲开雷雨云！\n点一下开始", "时间到", BOOPIE_GAME_CATCH, catch_start,
      catch_tick, NULL, catch_over, catch_score, boopie_catch_reward, catch_render, catch_extra },
    { "maze", "重力迷宫", "倾斜板子或按住屏幕\n把小球滚到绿色出口\n顺路摘星星加分\n点一下开始", "时间到", BOOPIE_GAME_MAZE, maze_start,
      maze_tick, NULL, maze_over, maze_score, boopie_maze_reward, maze_render, maze_extra },
    { "hop", "跳跳布比", "点屏幕让宠物往上跳\n钻过柱子中间的缝\n别碰柱子和地面！\n点一下开始", "撞到啦", BOOPIE_GAME_HOP,
      hop_start, hop_tick, hop_tap, hop_over, hop_score, boopie_hop_reward, hop_render, hop_extra },
};
#define GAME_COUNT (int)(sizeof GAMES / sizeof GAMES[0])

/* The tilt now, less the level the round started at; false without an IMU. */
static bool read_tilt(float *tx, float *ty, bool level)
{
#ifdef ESP_PLATFORM
    float g[3];
    boopie_imu_keepalive();   /* read it fast: the face isn't drawing meanwhile */
    if (!boopie_imu_gravity(g)) {
        return false;
    }
    if (level) {
        memcpy(s_bias, g, sizeof s_bias);
    }
    float x = g[0] - s_bias[0];
    float y = g[1] - s_bias[1];
    *tx = fabsf(x) < TILT_DEAD ? 0 : x;
    *ty = fabsf(y) < TILT_DEAD ? 0 : y;
    return true;
#else
    (void)level;
    *tx = *ty = 0;
    return false;
#endif
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, const char *s)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, boopie_font_with_cjk(font), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(l, 6, 0);
    lv_label_set_text(l, s);
    return l;
}

static void close_card(void)
{
    if (s_card) {
        lv_obj_delete_async(s_card);
        s_card = NULL;
    }
}

static void leave(void);
static void start(void);

static void on_again(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    start();
}

static void on_leave(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    leave();
}

static lv_obj_t *card_button(lv_obj_t *card, const char *label, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(card);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 120, 50);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2e2552), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x40357a), LV_STATE_PRESSED);
    lv_obj_center(text(b, &lv_font_montserrat_20, COLOR_TEXT, label));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

/* A card over the game: a title, a few lines, and (results only) two buttons. */
static void show_card(const char *title, const char *lines, bool buttons)
{
    close_card();
    s_card = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_card);
    lv_obj_set_size(s_card, 320, buttons ? 280 : 230);
    lv_obj_center(s_card);
    lv_obj_set_style_radius(s_card, 28, 0);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_remove_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_card, LV_OBJ_FLAG_CLICKABLE);   /* taps go through to the game */
    lv_obj_t *t = text(s_card, &lv_font_montserrat_28, COLOR_TEXT, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_t *l = text(s_card, &lv_font_montserrat_20, COLOR_DIM, lines);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 76);
    if (buttons) {
        lv_obj_add_flag(s_card, LV_OBJ_FLAG_CLICKABLE);   /* the results stay until a button */
        lv_obj_align(card_button(s_card, "再来一局", on_again), LV_ALIGN_BOTTOM_MID, -66, -22);
        lv_obj_align(card_button(s_card, "返回", on_leave), LV_ALIGN_BOTTOM_MID, 66, -22);
    }
}

static void start(void)
{
    close_card();
    s_def->start((uint32_t)esp_timer_get_time() | 1u);
    float tx, ty;
    read_tilt(&tx, &ty, true);   /* level is as it's held now */
    s_hold_armed = false;
    s_steer.held = false;
    s_state = G_PLAY;
    s_last_us = esp_timer_get_time();
    draw();
}

static void set_paused(bool on)
{
    if (on && s_state == G_PLAY) {
        s_state = G_PAUSE;
        show_card("暂停", "点屏幕或上键继续\n下键退出", false);
    } else if (!on && s_state == G_PAUSE) {
        close_card();
        s_state = G_PLAY;
        s_last_us = esp_timer_get_time();
    }
}

static void finish(void)
{
    s_state = G_OVER;
    boopie_sound_play(BOOPIE_SOUND_GAME_OVER);
    int xp, stars, best;
    bool record;
    boopie_pet_event_t ev;
    int score = s_def->score();
    s_def->reward(score, &xp, &stars);
    boopie_avatar_game_result(s_def->which, score, xp, stars, &ev, &best, &record);
    char extra[48], lines[200];
    s_def->extra(extra, sizeof extra);
    int n = snprintf(lines, sizeof lines, "%d 分%s  %s\n最高 %d 分\n", score, record ? "  新纪录！" : "", extra, best);
    if (ev.xp || ev.stars) {
        snprintf(lines + n, sizeof lines - n, "经验 +%d   ★ +%d", ev.xp, ev.stars);
    } else {
        snprintf(lines + n, sizeof lines - n, "%s", xp || stars ? "今天的奖励领完啦" : "再接再厉");
    }
    show_card(record ? "新纪录！" : s_def->done, lines, true);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (s_state != G_PLAY) {
        return;
    }
    float mode_t;
    if (muse_state_mode(&mode_t) != MUSE_MODE_IDLE || muse_state_asleep()) {
        set_paused(true);   /* Muse is talking, or the screen went dark */
        return;
    }
    int64_t now = esp_timer_get_time();
    float dt = (float)(now - s_last_us) / 1e6f;
    s_last_us = now;
    read_tilt(&s_steer.tx, &s_steer.ty, false);
    boopie_sound_t sound = s_def->tick(dt > 0.1f ? 0.1f : dt, &s_steer);
    if (sound != BOOPIE_SOUND_COUNT) {
        boopie_sound_play(sound);
    }
    muse_state_poke();   /* playing keeps the screen on */
    draw();
    if (s_def->over()) {
        finish();
    }
}

/* A finger held on the screen steers (接零食, 重力迷宫); lifted, the tilt does. */
static void on_hold(lv_event_t *e)
{
    lv_indev_t *in = lv_event_get_indev(e);
    if (!in || s_state != G_PLAY || !s_hold_armed) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_area_t a;
    lv_obj_get_coords(s_image, &a);
    s_steer.held = true;
    s_steer.fx = (float)(p.x - a.x1) / CELL;
    s_steer.fy = (float)(p.y - a.y1) / CELL;
}

static void on_release(lv_event_t *e)
{
    (void)e;
    s_steer.held = false;
    s_hold_armed = true;
}

static void on_press(lv_event_t *e)
{
    lv_indev_t *in = lv_event_get_indev(e);
    if (!in) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(in, &p);
    switch (s_state) {
    case G_READY:
        start();
        if (s_def->tap == hop_tap) {
            hop_tap(0, 0);   /* the tap that starts it is its first hop */
        }
        break;
    case G_PAUSE:
        set_paused(false);
        break;
    case G_PLAY: {
        if (!s_def->tap) {
            s_hold_armed = true;
            on_hold(e);
            break;
        }
        lv_area_t a;
        lv_obj_get_coords(s_image, &a);
        s_def->tap((float)(p.x - a.x1) / CELL, (float)(p.y - a.y1) / CELL);
        int points = s_tap_points;
        if (points) {
            boopie_sound_play(points < 0 ? BOOPIE_SOUND_CLOUD : points >= 3 ? BOOPIE_SOUND_GOLD : BOOPIE_SOUND_SCORE);
        }
        break;
    }
    default:
        break;
    }
}

static void leave(void)
{
    s_state = G_OFF;
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_card = NULL;
    if (s_root) {
        lv_obj_delete_async(s_root);
        s_root = NULL;
    }
}

bool boopie_games_open_locked(const char *game)
{
    const game_def_t *def = NULL;
    for (int i = 0; game && i < GAME_COUNT; i++) {
        if (strcmp(game, GAMES[i].id) == 0) {
            def = &GAMES[i];
        }
    }
    if (!def) {
        return false;
    }
    if (s_state != G_OFF) {
        return def == s_def;   /* one at a time */
    }
    s_def = def;
    if (!s_buf) {
        s_buf = heap_caps_malloc(SIZE * SIZE * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_buf) {
            return false;
        }
        s_dsc = (lv_image_dsc_t){
            .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = SIZE, .h = SIZE,
                        .stride = SIZE * sizeof(uint16_t) },
            .data_size = SIZE * SIZE * sizeof(uint16_t),
            .data = (const uint8_t *)s_buf,
        };
    }
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, on_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_root, on_hold, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_root, on_release, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_root, on_release, LV_EVENT_PRESS_LOST, NULL);
    s_image = lv_image_create(s_root);
    lv_image_set_src(s_image, &s_dsc);
    lv_obj_center(s_image);
    lv_obj_remove_flag(s_image, LV_OBJ_FLAG_CLICKABLE);   /* presses land on the root */

    s_def->start(1);
    s_state = G_READY;
    draw();
    show_card(s_def->title, s_def->intro, false);
    s_timer = lv_timer_create(tick, FRAME_MS, NULL);
    return true;
}

bool boopie_games_open(const char *game)
{
    muse_board->display_lock(-1);
    bool ok = boopie_games_open_locked(game);
    muse_board->display_unlock();
    return ok;
}

void boopie_games_key(bool top)
{
    muse_board->display_lock(-1);
    if (!top) {
        leave();
    } else if (s_state == G_READY || s_state == G_OVER) {
        start();
    } else {
        set_paused(s_state == G_PLAY);
    }
    muse_board->display_unlock();
}
