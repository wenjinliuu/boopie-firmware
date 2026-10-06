/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_guide.h"

#include <stdio.h>
#include <string.h>

#include "boopie_avatar.h"
#include "boopie_font.h"
#include "boopie_input.h"
#include "boopie_pixel.h"
#include "boopie_setup.h"
#include "boopie_setup_web.h"
#include "boopie_xiaozhi.h"
#include "muse_ui.h"

#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff
#define HEAD_SCALE 5

typedef enum { S_HELLO, S_ONLINE, S_BRAIN, S_BRAIN_SETUP, S_PET, S_TALK, S_FEED, S_SWIPE, S_COUNT } step_t;

static lv_obj_t *s_root;
static step_t s_step;
static bool s_away;            /* sent to a settings page; back when they're on the face */
static uint16_t *s_heads[BOOPIE_AVATAR_COUNT];
static lv_image_dsc_t s_head_dsc[BOOPIE_AVATAR_COUNT];

static void show_step(step_t step);

/* From a button's own event: the page it's on goes, so after the event. */
static step_t s_pending;

static void show_pending(void *arg)
{
    (void)arg;
    if (s_root) {
        show_step(s_pending);
    }
}

static void go(step_t step)
{
    s_pending = step;
    lv_async_call(show_pending, NULL);
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

static lv_obj_t *button(lv_obj_t *parent, int w, const char *label, bool main, lv_event_cb_t cb, intptr_t user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, 52);
    lv_obj_set_style_radius(b, 18, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(main ? COLOR_ACCENT : COLOR_CARD), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(main ? 0x8c63e6 : COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_center(text(b, &lv_font_montserrat_20, main ? 0x14102a : COLOR_TEXT, label));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)user);
    return b;
}

/* A step's page: title, a few lines, and room for its buttons below (returned). */
static lv_obj_t *page(const char *title, const char *body)
{
    lv_obj_clean(s_root);
    lv_obj_t *t = text(s_root, &lv_font_montserrat_28, COLOR_TEXT, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 70);
    if (body) {
        lv_obj_t *b = text(s_root, &lv_font_montserrat_20, COLOR_DIM, body);
        lv_obj_set_width(b, 330);
        lv_label_set_long_mode(b, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 122);
    }
    /* Progress: a dot a step. */
    for (int i = 0; i < S_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(s_root);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(i == (int)s_step ? COLOR_ACCENT : 0x3a3358), 0);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i - (S_COUNT - 1) / 2.0f) * 16, -22);
    }
    lv_obj_t *col = lv_obj_create(s_root);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 300, LV_SIZE_CONTENT);
    lv_obj_align(col, LV_ALIGN_BOTTOM_MID, 0, -54);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 10, 0);
    return col;
}

static void finish(void)
{
    boopie_avatar_set_guided(true);
    s_away = false;
    if (s_root) {
        lv_obj_delete_async(s_root);
        s_root = NULL;
    }
}

static void on_next(lv_event_t *e)
{
    (void)e;
    if (s_step + 1 >= S_COUNT) {
        finish();
    } else {
        go(s_step + 1);
    }
}

static void on_wifi(lv_event_t *e)
{
    (void)e;
    /* Off to the Wi-Fi page; the guide picks up at the brain when they're back on the face. */
    s_away = true;
    s_step = S_BRAIN;
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    muse_ui_open_settings("wifi");
}

/* Phone setup can set the brain and Muse's token too: if it did, on to the
 * pet; if not, the brain is next, as from the Wi-Fi page. */
static void phone_done(uint32_t saved)
{
    if (s_root) {
        s_step = saved & BOOPIE_SETUP_SAVED_BRAIN ? S_PET : S_BRAIN;
    }
}

static void on_phone(lv_event_t *e)
{
    (void)e;
    s_away = true;
    s_step = s_step == S_BRAIN_SETUP ? S_PET : S_BRAIN;
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    boopie_setup_open(s_step == S_PET ? NULL : phone_done);
}

static void on_brain(lv_event_t *e)
{
    boopie_avatar_set_brain((boopie_brain_t)(intptr_t)lv_event_get_user_data(e));
    boopie_xiaozhi_start();   /* if that was 小智 */
    go(S_BRAIN_SETUP);
}

static void on_pick(lv_event_t *e)
{
    boopie_avatar_select((int)(intptr_t)lv_event_get_user_data(e));
    go(S_PET);   /* redrawn with the pick marked and its name */
}

static void named(const char *name, bool done)
{
    const char *error;
    if (done) {
        boopie_avatar_set_pet_name(name, &error);
    }
    if (s_root) {
        go(S_PET);
    }
}

static void on_name(lv_event_t *e)
{
    (void)e;
    boopie_input_open("给它起个名字", boopie_avatar_has_own_name() ? boopie_avatar_pet_name() : "",
                      "留空就叫角色名", BOOPIE_PET_NAME_CHARS, named);
}

static void build_heads(void)
{
    for (int a = 0; a < BOOPIE_AVATAR_COUNT; a++) {
        if (s_heads[a]) {
            continue;
        }
        size_t n = (size_t)BOOPIE_HEAD_W * HEAD_SCALE * BOOPIE_HEAD_H * HEAD_SCALE;
        s_heads[a] = lv_malloc(n * sizeof(uint16_t));
        if (!s_heads[a]) {
            continue;
        }
        boopie_pixel_head_image(a == BOOPIE_AVATAR_MUSE ? BOOPIE_SKIN_MUSE : a - 1, s_heads[a], HEAD_SCALE);
        s_head_dsc[a] = (lv_image_dsc_t){
            .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                        .w = BOOPIE_HEAD_W * HEAD_SCALE, .h = BOOPIE_HEAD_H * HEAD_SCALE,
                        .stride = BOOPIE_HEAD_W * HEAD_SCALE * sizeof(uint16_t) },
            .data_size = (uint32_t)(n * sizeof(uint16_t)),
            .data = (const uint8_t *)s_heads[a],
        };
    }
}

/* The characters as heads to tap, Boopie first: four over three. */
static void build_picker(void)
{
    build_heads();
    static const int ORDER[BOOPIE_AVATAR_COUNT] = { 1, 2, 3, 4, 5, 6, 0 };   /* Boopie ... Doubao, Muse */
    int cur = boopie_avatar_current();
    for (int i = 0; i < BOOPIE_AVATAR_COUNT; i++) {
        int a = ORDER[i];
        int row = i < 4 ? 0 : 1, col = row ? i - 4 : i, cols = row ? 3 : 4;
        lv_obj_t *b = lv_button_create(s_root);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 76, 62);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(a == cur ? 0x3a2d6e : COLOR_CARD), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_border_width(b, a == cur ? 2 : 0, 0);
        lv_obj_align(b, LV_ALIGN_TOP_MID, (int)((col - (cols - 1) / 2.0f) * 82), 118 + row * 68);
        if (s_heads[a]) {
            lv_obj_t *img = lv_image_create(b);
            lv_image_set_src(img, &s_head_dsc[a]);
            lv_obj_center(img);
            lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_add_event_cb(b, on_pick, LV_EVENT_CLICKED, (void *)(intptr_t)a);
    }
}

static void show_step(step_t step)
{
    s_step = step;
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *col;
    char line[96];
    switch (step) {
    case S_HELLO:
        col = page("你好呀！", "我是你的新伙伴。\n花一分钟，把我设置好吧。");
        button(col, 220, "开始", true, on_next, 0);
        break;
    case S_ONLINE:
        col = page("先连上网", "用手机扫码，一次填完 Wi-Fi、\nAI 助手和密钥；或者在我这里选。");
        button(col, 260, "手机扫码设置", true, on_phone, 0);
        button(col, 260, "在屏幕上选 Wi-Fi", false, on_wifi, 0);
        button(col, 260, "稍后再说", false, on_next, 0);
        break;
    case S_BRAIN:
        col = page("选 AI 助手", "说话时用哪个 AI 回答？\n以后在设置里也能换。");
        button(col, 300, "Muse（推荐，需海外网络）", true, on_brain, BOOPIE_BRAIN_MUSE);
        button(col, 300, "小智（备用，国内网络）", false, on_brain, BOOPIE_BRAIN_XIAOZHI);
        break;
    case S_BRAIN_SETUP:
        if (boopie_avatar_brain() == BOOPIE_BRAIN_MUSE) {
            col = page("Muse", "需要你自己的开发者 token\n（gadgets.muse.ai 生成）\n和 VPN。用手机扫码粘贴。");
            button(col, 220, "手机扫码填写", true, on_phone, 0);
            button(col, 220, "稍后再说", false, on_next, 0);
            break;
        } else {
            col = page("小智", "联网后，设置 › AI 助手 ›\n小智接入 里会显示激活码，\n到 xiaozhi.me 添加设备。");
        }
        button(col, 220, "好的", true, on_next, 0);
        break;
    case S_PET:
        col = page("选一个伙伴", NULL);
        build_picker();
        snprintf(line, sizeof line, "它叫 %s", boopie_avatar_pet_name());
        lv_obj_align(text(s_root, &lv_font_montserrat_20, COLOR_DIM, line), LV_ALIGN_TOP_MID, 0, 258);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_ROW);   /* the two buttons side by side */
        lv_obj_set_style_pad_column(col, 12, 0);
        button(col, 130, "起名字", false, on_name, 0);
        button(col, 130, "下一步", true, on_next, 0);
        break;
    case S_TALK:
        col = page("和我说话", "点一下上面的键开始说，\n说完我会自己听出来。\n也可以按住说，松开就发。");
        button(col, 220, "下一步", true, on_next, 0);
        break;
    case S_FEED:
        col = page("喂我吃饭", "我饿了会出现吃的，\n点一下它就能喂我。\n玩游戏、聊天都能升级。");
        button(col, 220, "下一步", true, on_next, 0);
        break;
    default:
        col = page("四个方向滑", "右滑：应用和游戏\n左滑：设置\n下拉：信息卡片\n上滑：我的小窝");
        button(col, 220, "开始吧", true, on_next, 0);
        break;
    }
}

void boopie_guide_start(void)
{
    if (!s_root) {
        s_root = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_root);
        lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    }
    s_away = false;
    show_step(S_HELLO);
}

bool boopie_guide_active(void)
{
    return s_root != NULL;
}

void boopie_guide_tick(bool on_face)
{
    if (s_root && s_away && on_face) {
        s_away = false;
        show_step(s_step);
    }
}
