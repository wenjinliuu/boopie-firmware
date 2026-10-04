/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_garden_ui.h"

#include <stdio.h>
#include <string.h>

#include "boopie_avatar.h"
#include "boopie_font.h"
#include "boopie_garden.h"
#include "boopie_pixel.h"
#include "boopie_sound.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "muse_board.h"
#include "muse_state.h"

#define CELL 7
#define SIZE (BOOPIE_PX * CELL)   /* 448 */
#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff

static lv_obj_t *s_root, *s_image, *s_status, *s_card;
static lv_timer_t *s_timer;
static uint16_t *s_buf;
static lv_image_dsc_t s_dsc;
static int s_selected = -1, s_watering = -1, s_harvested = -1, s_planting = -1;
static float s_watered_at, s_harvested_at;
static char s_note[96];   /* a moment's message, over the status line */
static float s_note_until;

static float now_s(void)
{
    return (float)(esp_timer_get_time() / 1000) / 1000.0f;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, const char *s)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, boopie_font_with_cjk(font), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, s);
    return l;
}

/* The grid, 7 x 7 a cell, each cell's last row and column dimmer, as the games draw it. */
static void scale(void)
{
    const uint8_t *rgb = boopie_pixel_rgb();
    for (int cy = 0; cy < BOOPIE_PX; cy++) {
        for (int cx = 0; cx < BOOPIE_PX; cx++) {
            const uint8_t *c = rgb + (cy * BOOPIE_PX + cx) * 3;
            uint16_t full = (uint16_t)((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3);
            uint16_t dim = (uint16_t)(((c[0] * 200 >> 8) >> 3) << 11 | ((c[1] * 200 >> 8) >> 2) << 5 | (c[2] * 200 >> 8) >> 3);
            for (int y = 0; y < CELL; y++) {
                uint16_t *row = s_buf + (cy * CELL + y) * SIZE + cx * CELL;
                for (int x = 0; x < CELL; x++) {
                    row[x] = (x == CELL - 1 || y == CELL - 1) ? dim : full;
                }
            }
        }
    }
}

static void say(const char *msg)
{
    snprintf(s_note, sizeof s_note, "%s", msg);
    s_note_until = now_s() + 3.0f;
}

static void left_text(uint32_t secs, char *out, size_t cap)
{
    uint32_t h = (secs + 3599) / 3600;
    if (h >= 24) {
        snprintf(out, cap, "%u 天 %u 小时", (unsigned)(h / 24), (unsigned)(h % 24));
    } else {
        snprintf(out, cap, "%u 小时", (unsigned)h);
    }
}

static void status_text(const boopie_garden_t *g, int64_t now, char *out, size_t cap)
{
    if (!g) {
        snprintf(out, cap, "还没对上时间，连上网就能种花了");
        return;
    }
    if (s_selected < 0) {
        int dry = 0, ready = 0, empty = 0;
        for (int i = 0; i < BOOPIE_GARDEN_POTS; i++) {
            dry += boopie_garden_dry(g, i, now);
            ready += boopie_garden_stage(g, i) == BOOPIE_STAGE_BLOOM;
            empty += boopie_garden_stage(g, i) == BOOPIE_STAGE_EMPTY;
        }
        if (ready) {
            snprintf(out, cap, "有 %d 盆开好了，点它收获", ready);
        } else if (dry) {
            snprintf(out, cap, "有 %d 盆渴了，点它浇水", dry);
        } else if (empty == BOOPIE_GARDEN_POTS) {
            snprintf(out, cap, "点花盆种一颗种子吧");
        } else {
            snprintf(out, cap, "点花盆看看它长得怎么样");
        }
        return;
    }
    const char *name = boopie_plant_name((boopie_plant_t)g->pots[s_selected].plant);
    switch (boopie_garden_stage(g, s_selected)) {
    case BOOPIE_STAGE_EMPTY:
        snprintf(out, cap, "空花盆：点它种一颗种子");
        return;
    case BOOPIE_STAGE_BLOOM:
        snprintf(out, cap, "%s开好了！点它收获", name);
        return;
    default:
        break;
    }
    if (boopie_garden_dry(g, s_selected, now)) {
        snprintf(out, cap, "%s渴了，点它浇水\n不浇水它就先不长", name);
        return;
    }
    char left[32];
    left_text(boopie_garden_left_s(g, s_selected), left, sizeof left);
    char damp[32];
    left_text((uint32_t)boopie_garden_damp_s(g, s_selected, now), damp, sizeof damp);
    snprintf(out, cap, "%s：再长 %s 开花\n土还能湿 %s", name, left, damp);
}

static void redraw(void)
{
    int64_t now;
    int minute = 12 * 60;
    boopie_garden_t *g = boopie_avatar_garden(&now, &minute);
    static boopie_garden_t empty;
    if (!g) {
        boopie_garden_init(&empty);
        now = 0;
    }
    float t = now_s();
    boopie_pixel_render_garden(g ? g : &empty, now, minute, t, s_selected, s_watering, t - s_watered_at,
                               s_harvested, t - s_harvested_at);
    scale();
    lv_obj_invalidate(s_image);
    char status[128];
    if (t < s_note_until) {
        snprintf(status, sizeof status, "%s", s_note);
    } else {
        status_text(g, now, status, sizeof status);
    }
    if (strcmp(status, lv_label_get_text(s_status)) != 0) {
        lv_label_set_text(s_status, status);
    }
}

static void on_tick(lv_timer_t *t)
{
    (void)t;
    if (s_root) {
        redraw();
    }
}

static void close_card(void)
{
    if (s_card) {
        lv_obj_delete_async(s_card);
        s_card = NULL;
    }
    s_planting = -1;
}

static void on_seed(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    boopie_plant_t plant = (boopie_plant_t)(intptr_t)lv_event_get_user_data(e);
    int64_t now;
    int minute;
    boopie_garden_t *g = boopie_avatar_garden(&now, &minute);
    if (g && s_planting >= 0 && boopie_garden_plant(g, s_planting, plant, now)) {
        boopie_avatar_garden_changed(0, 0);
        boopie_sound_play(BOOPIE_SOUND_WATER);
        s_watering = s_planting;
        s_watered_at = now_s();
        char msg[96];
        snprintf(msg, sizeof msg, "种下了%s！\n每天浇一次水，%d 天开花", boopie_plant_name(plant), boopie_plant_days(plant));
        say(msg);
    }
    close_card();
}

static void on_card_close(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    close_card();
}

/* Which seed: four buttons, each its name and how many days it takes. */
static void seed_card(int pot)
{
    close_card();
    s_planting = pot;
    s_card = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_card);
    lv_obj_set_size(s_card, 310, 250);
    lv_obj_center(s_card);
    lv_obj_set_style_radius(s_card, 28, 0);
    lv_obj_set_style_bg_opa(s_card, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_add_flag(s_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_card, on_card_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t = text(s_card, &lv_font_montserrat_20, COLOR_TEXT, "种什么？");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 18);
    for (int i = 1; i < BOOPIE_PLANT_COUNT; i++) {
        lv_obj_t *b = lv_button_create(s_card);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 130, 70);
        lv_obj_set_style_radius(b, 18, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_CARD_PRESSED), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x40357a), LV_STATE_PRESSED);
        lv_obj_align(b, LV_ALIGN_TOP_MID, ((i - 1) % 2 ? 1 : -1) * 70, 60 + (i - 1) / 2 * 82);
        char label[48];
        snprintf(label, sizeof label, "%s\n%d 天", boopie_plant_name((boopie_plant_t)i), boopie_plant_days((boopie_plant_t)i));
        lv_obj_t *l = text(b, &lv_font_montserrat_20, COLOR_TEXT, label);
        lv_obj_set_style_text_line_space(l, 2, 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, on_seed, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void on_press(lv_event_t *e)
{
    if (s_card) {
        return;
    }
    lv_indev_t *in = lv_event_get_indev(e);
    if (!in) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_area_t a;
    lv_obj_get_coords(s_image, &a);
    float gx = (float)(p.x - a.x1) / CELL, gy = (float)(p.y - a.y1) / CELL;
    int pot = -1;
    for (int i = 0; i < BOOPIE_GARDEN_POTS; i++) {
        int cx, top;
        boopie_pixel_garden_pot(i, &cx, &top);
        if (gx >= cx - 8 && gx <= cx + 8 && gy >= top - 26 && gy <= top + 9) {
            pot = i;
        }
    }
    s_selected = pot;
    int64_t now;
    int minute;
    boopie_garden_t *g = boopie_avatar_garden(&now, &minute);
    if (pot < 0 || !g) {
        redraw();
        return;
    }
    muse_state_poke();
    switch (boopie_garden_stage(g, pot)) {
    case BOOPIE_STAGE_EMPTY:
        seed_card(pot);
        break;
    case BOOPIE_STAGE_BLOOM: {
        const char *name = boopie_plant_name((boopie_plant_t)g->pots[pot].plant);
        int xp = 0, stars = 0;
        if (boopie_garden_harvest(g, pot, &xp, &stars)) {
            boopie_sound_play(BOOPIE_SOUND_GOLD);
            boopie_avatar_garden_changed(xp, stars);
            s_harvested = pot;
            s_harvested_at = now_s();
            char msg[96];
            snprintf(msg, sizeof msg, "收获了%s！\n★ +%d   经验 +%d", name, stars, xp);
            say(msg);
            muse_state_set_caption("收获了%s！", name);
        }
        break;
    }
    default:
        if (boopie_garden_water(g, pot, now)) {
            boopie_sound_play(BOOPIE_SOUND_WATER);
            boopie_avatar_garden_changed(0, 0);
            s_watering = pot;
            s_watered_at = now_s();
            say("浇好了！");
        }
        break;
    }
    redraw();
}

void boopie_garden_ui_open_locked(void)
{
    if (s_root) {
        lv_obj_move_foreground(s_root);
        return;
    }
    if (!s_buf) {
        s_buf = heap_caps_malloc(SIZE * SIZE * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_buf) {
            return;
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
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_root, on_press, LV_EVENT_PRESSED, NULL);
    s_image = lv_image_create(s_root);
    lv_image_set_src(s_image, &s_dsc);
    lv_obj_center(s_image);
    lv_obj_remove_flag(s_image, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *t = text(s_root, &boopie_font_pixel_24, 0xffffff, "小花园");
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);
    s_status = text(s_root, &lv_font_montserrat_16, COLOR_TEXT, "");
    lv_obj_set_style_text_line_space(s_status, 4, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 362);

    s_selected = s_watering = s_harvested = -1;
    s_note_until = 0;
    redraw();
    s_timer = lv_timer_create(on_tick, 120, NULL);
}

bool boopie_garden_ui_active(void)
{
    return s_root != NULL;
}

void boopie_garden_ui_close(void)
{
    muse_board->display_lock(-1);
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    s_card = NULL;
    if (s_root) {
        lv_obj_delete_async(s_root);
        s_root = NULL;
    }
    muse_board->display_unlock();
}

void boopie_garden_ui_status(char *out, size_t cap)
{
    static const char *const STAGES[] = { "a seed", "a sprout", "in leaf", "in bud", "in bloom, ready to pick" };
    static const char *const PLANTS[] = { "", "sunflower", "tulip", "strawberry", "cactus" };
    muse_board->display_lock(-1);
    int64_t now;
    int minute;
    boopie_garden_t *g = boopie_avatar_garden(&now, &minute);
    size_t n = 0;
    if (!g) {
        snprintf(out, cap, "the clock isn't set yet, so the garden can't grow");
    }
    for (int i = 0; g && i < BOOPIE_GARDEN_POTS && n < cap; i++) {
        boopie_stage_t st = boopie_garden_stage(g, i);
        if (st == BOOPIE_STAGE_EMPTY) {
            n += (size_t)snprintf(out + n, cap - n, "pot %d: empty. ", i + 1);
        } else {
            n += (size_t)snprintf(out + n, cap - n, "pot %d: %s, %s, %s, %u h of damp soil to bloom. ", i + 1,
                                  PLANTS[g->pots[i].plant], STAGES[st],
                                  boopie_garden_dry(g, i, now) ? "thirsty" : "watered",
                                  (unsigned)((boopie_garden_left_s(g, i) + 3599) / 3600));
        }
    }
    muse_board->display_unlock();
}
