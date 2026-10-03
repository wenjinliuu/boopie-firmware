/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_games.h"

#include <stdio.h>
#include <string.h>

#include "boopie_avatar.h"
#include "boopie_font.h"
#include "boopie_pixel.h"
#include "boopie_whack.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "muse_board.h"
#include "muse_state.h"

#define CELL 7                        /* screen pixels a grid cell */
#define SIZE (BOOPIE_PX * CELL)       /* 448 */
#define FRAME_MS 33
#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff
#define COLOR_GOLD 0xffd246

typedef enum { G_OFF, G_READY, G_PLAY, G_PAUSE, G_OVER } state_t;

static state_t s_state;
static boopie_whack_t s_game;
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
    boopie_pixel_render_whack(&s_game, head());
    scale();
    lv_obj_invalidate(s_image);
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
    lv_obj_set_size(s_card, 320, buttons ? 280 : 200);
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
    boopie_whack_start(&s_game, (uint32_t)esp_timer_get_time() | 1u);
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
    int xp, stars, best;
    bool record;
    boopie_pet_event_t ev;
    boopie_whack_reward(s_game.score, &xp, &stars);
    boopie_avatar_game_result(BOOPIE_GAME_WHACK, s_game.score, xp, stars, &ev, &best, &record);
    char lines[160];
    int n = snprintf(lines, sizeof lines, "%d 分%s\n最高 %d 分\n", s_game.score, record ? "  新纪录！" : "", best);
    if (ev.xp || ev.stars) {
        snprintf(lines + n, sizeof lines - n, "经验 +%d   ★ +%d", ev.xp, ev.stars);
    } else {
        snprintf(lines + n, sizeof lines - n, "%s", xp || stars ? "今天的奖励领完啦" : "再接再厉");
    }
    show_card(record ? "新纪录！" : "时间到", lines, true);
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
    boopie_whack_tick(&s_game, dt > 0.1f ? 0.1f : dt);
    muse_state_poke();   /* playing keeps the screen on */
    draw();
    if (s_game.over) {
        finish();
    }
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
        break;
    case G_PAUSE:
        set_paused(false);
        break;
    case G_PLAY: {
        lv_area_t a;
        lv_obj_get_coords(s_image, &a);
        boopie_whack_tap(&s_game, (float)(p.x - a.x1) / CELL, (float)(p.y - a.y1) / CELL);
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
    if (!game || strcmp(game, "whack") != 0) {
        return false;
    }
    if (s_state != G_OFF) {
        return true;
    }
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
    s_image = lv_image_create(s_root);
    lv_image_set_src(s_image, &s_dsc);
    lv_obj_center(s_image);
    lv_obj_remove_flag(s_image, LV_OBJ_FLAG_CLICKABLE);   /* presses land on the root */

    boopie_whack_start(&s_game, 1);
    s_game.t = 0;
    s_state = G_READY;
    draw();
    show_card("戳戳布比", "宠物冒头就戳它\n别戳小乌云！\n\n点一下开始", false);
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
