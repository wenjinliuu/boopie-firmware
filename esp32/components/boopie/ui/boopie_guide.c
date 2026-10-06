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
#include "boopie_sdk_token.h"
#include "boopie_setup_web.h"
#include "muse_ble.h"
#include "muse_link.h"
#include "muse_ui.h"
#include "muse_wifi.h"
#include "muse_state.h"
#include "boopie_xiaozhi.h"

#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff
#define COLOR_OK 0x7fe3a0
#define HEAD_SCALE 5

/* The brain first, as it decides the rest: Muse gets online through the Muse
 * app's pairing (which sends the Wi-Fi), then wants its developer token and a
 * VPN; 小智 needs only Wi-Fi. Both restarts on Muse's way (after the app's
 * Wi-Fi, after the token) come back to the step they left (boopie_avatar_guide_at),
 * and a step already done (paired, token saved, online) is passed over. */
typedef enum {
    S_HELLO, S_BRAIN, S_MUSE_PAIR, S_MUSE_KEY, S_ONLINE, S_XZ, S_PET, S_TALK, S_FEED, S_SWIPE, S_COUNT
} step_t;

static lv_obj_t *s_root;
static step_t s_step;
static bool s_away;            /* sent to a settings page; back when they're on the face */
static int s_pair_shown = -1;  /* the pairing state the Muse step shows, to redraw on change */
static char s_note[96];        /* a line over the next page: what was skipped, or the restart's why */
static char s_xz_shown[40];    /* what the 小智 step shows, to redraw on change */
static uint16_t *s_heads[BOOPIE_AVATAR_COUNT];
static lv_image_dsc_t s_head_dsc[BOOPIE_AVATAR_COUNT];

static void show_step(step_t step);
static step_t next_of(step_t step);

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
    /* What happened before this page (a step done already, a restart). */
    if (s_note[0]) {
        lv_obj_t *n = text(s_root, &lv_font_montserrat_16, COLOR_OK, s_note);
        lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 40);
        s_note[0] = '\0';
    }
    /* Progress: a dot a step of this brain's way through, and "n / m". */
    int steps = 0, at = 0;
    for (step_t s = S_HELLO; s < S_COUNT && steps < S_COUNT; s = next_of(s)) {
        if (s == s_step) {
            at = steps;
        }
        steps++;
    }
    for (int i = 0; i < steps; i++) {
        lv_obj_t *d = lv_obj_create(s_root);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(i == at ? COLOR_ACCENT : i < at ? 0x6d5aa8 : 0x3a3358), 0);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (int)((i - (steps - 1) / 2.0f) * 16), -22);
    }
    char count[32];
    snprintf(count, sizeof count, "%d / %d", at + 1, steps);
    lv_obj_align(text(s_root, &lv_font_montserrat_14, COLOR_DIM, count), LV_ALIGN_BOTTOM_MID, 0, -34);
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

/* Where "next" goes from each step: the two brains part after S_BRAIN and
 * meet again at S_PET. */
static step_t next_of(step_t step)
{
    switch (step) {
    case S_BRAIN:
        return boopie_avatar_brain() == BOOPIE_BRAIN_MUSE ? S_MUSE_PAIR : S_ONLINE;
    case S_MUSE_KEY:
        return S_PET;
    default:
        return step + 1;
    }
}

static bool muse_paired(void)
{
    return muse_link_hatch_linked();
}

static bool muse_has_token(void)
{
    char t[BOOPIE_SDK_TOKEN_LEN + 1];
    bool have = boopie_sdk_token(t);
    memset(t, 0, sizeof t);
    return have;
}

/* A step there's nothing left to do on. */
static bool done_already(step_t step)
{
    switch (step) {
    case S_MUSE_PAIR:
        return muse_paired();
    case S_MUSE_KEY:
        return muse_has_token();
    case S_ONLINE:
        return muse_wifi_connected();
    default:
        return false;
    }
}

static void on_next(lv_event_t *e)
{
    (void)e;
    step_t n = next_of(s_step);
    if (n >= S_COUNT) {
        finish();
    } else {
        go(n);
    }
}

static void on_wifi(lv_event_t *e)
{
    (void)e;
    /* Off to the Wi-Fi page; the guide picks up after it when they're back on the face. */
    s_away = true;
    s_step = next_of(S_ONLINE);
    boopie_avatar_set_guide_at(s_step);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    muse_ui_open_settings("wifi");
}

/* Back from phone setup: past the step if it did what the step's for (a
 * token saved restarts the board, and the guide comes back there anyway). */
static void phone_done(uint32_t saved)
{
    if (!s_root) {
        return;
    }
    if (s_step == S_MUSE_KEY) {
        s_step = saved & BOOPIE_SETUP_SAVED_MUSE ? S_PET : S_MUSE_KEY;
    } else if (s_step == S_ONLINE) {
        s_step = saved & BOOPIE_SETUP_SAVED_WIFI ? S_XZ : S_ONLINE;
    }
}

static void on_phone(lv_event_t *e)
{
    (void)e;
    s_away = true;
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    boopie_setup_open(phone_done);
}

static void on_brain(lv_event_t *e)
{
    bool restarting = boopie_avatar_choose_brain((boopie_brain_t)(intptr_t)lv_event_get_user_data(e));
    if (restarting) {
        /* Back after the restart at the step this brain goes on to. */
        boopie_avatar_set_guide_at(next_of(S_BRAIN));
    }
    go(next_of(S_BRAIN));
}

/* Paired before, or set up with Wi-Fi alone: the app can only pair once Link
 * is back to advertising, which forgets the Wi-Fi and restarts. */
static void on_pair_again(lv_event_t *e)
{
    (void)e;
    boopie_avatar_set_guide_at(S_MUSE_PAIR);
    muse_link_reset_setup();
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

static const char *done_note(step_t step)
{
    switch (step) {
    case S_MUSE_PAIR:
        return "已和 Muse App 配对，跳过这一步";
    case S_MUSE_KEY:
        return "开发者 token 已经填好了";
    case S_ONLINE:
        return "已经连上 Wi-Fi 了";
    default:
        return NULL;
    }
}

static void show_step(step_t step)
{
    while (step < S_COUNT && done_already(step)) {
        const char *why = done_note(step);
        if (why && !s_note[0]) {
            strlcpy(s_note, why, sizeof s_note);
        }
        step = next_of(step);
    }
    if (step >= S_COUNT) {
        finish();
        return;
    }
    s_step = step;
    s_pair_shown = -1;
    boopie_avatar_set_guide_at(step);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *col;
    char line[160];
    switch (step) {
    case S_HELLO:
        col = page("你好呀！", "我是你的新伙伴。\n花一分钟，把我设置好吧。");
        button(col, 220, "开始", true, on_next, 0);
        break;
    case S_BRAIN:
        col = page("选 AI 助手", "说话时用哪个 AI 回答？\n以后在设置里也能换。");
        button(col, 300, "Muse（推荐，需海外网络）", true, on_brain, BOOPIE_BRAIN_MUSE);
        button(col, 300, "小智（备用，国内网络）", false, on_brain, BOOPIE_BRAIN_XIAOZHI);
        break;
    case S_MUSE_PAIR: {
        /* The app pairs over BLE, then sends the Wi-Fi; the board restarts
         * on it and comes back here, paired, to go on. */
        muse_link_state_t st = muse_link_state();
        s_pair_shown = st;
        muse_ble_status_t b;
        muse_ble_status(&b);
        if (st == MUSE_LINK_CONFIRM) {
            col = page("按上面的键", "确认是你在配对。");
        } else if (st == MUSE_LINK_PAIRING) {
            col = page("连上 App 了", "稍等，按 App 里的提示走。\n它会让你填 Wi-Fi，\n填完我会重启一下。");
        } else if (st == MUSE_LINK_UNPAIRED) {
            snprintf(line, sizeof line, "打开 Muse App，添加设备，\n选「%s」。\n要我确认时，按上面的键。", b.name[0] ? b.name : "MuseGadget");
            col = page("和 Muse App 配对", line);
        } else if (st == MUSE_LINK_BOOT || st == MUSE_LINK_CONNECTING) {
            col = page("和 Muse App 配对", "稍等，蓝牙准备中…");
        } else {
            col = page("和 Muse App 配对", "要先回到配对状态：\n会忘掉现在的 Wi-Fi 并重启，\n回来后接着在这一步。");
            button(col, 260, "开始配对", true, on_pair_again, 0);
        }
        button(col, 220, "稍后再说", false, on_next, 0);
        break;
    }
    case S_MUSE_KEY:
        col = page("填开发者 token", "用手机扫码打开设置网页，\n粘贴开发者 token（在 gadgets.muse.ai 生成）；\n国内网络再填 VPN 订阅。\n保存后我会重启一下。");
        button(col, 240, "手机扫码填写", true, on_phone, 0);
        button(col, 240, "稍后再说", false, on_next, 0);
        break;
    case S_ONLINE:
        col = page("先连上网", "用手机扫码填 Wi-Fi，\n或者在我这里选。");
        button(col, 260, "手机扫码设置", true, on_phone, 0);
        button(col, 260, "在屏幕上选 Wi-Fi", false, on_wifi, 0);
        button(col, 260, "稍后再说", false, on_next, 0);
        break;
    case S_XZ:
    {
        char code[16] = "", said[64] = "";
        boopie_xz_state_t st = boopie_xiaozhi_status(code, sizeof code, said, sizeof said);
        snprintf(s_xz_shown, sizeof s_xz_shown, "%d|%s|%d", (int)st, code, muse_wifi_connected());
        if (st == BOOPIE_XZ_READY) {
            col = page("小智绑好了", "按住上面的键说话试试。");
            button(col, 220, "下一步", true, on_next, 0);
        } else if (st == BOOPIE_XZ_CODE && code[0]) {
            snprintf(line, sizeof line, "激活码  %s\n手机打开 xiaozhi.me ›\n控制台 › 添加设备，输入它。\n绑好后这里会自动更新。", code);
            col = page("绑定小智", line);
            button(col, 220, "稍后再说", false, on_next, 0);
        } else {
            col = page("绑定小智", muse_wifi_connected() ? "正在向小智要激活码…" : "要先连上 Wi-Fi，\n才能拿到激活码。");
            button(col, 220, "稍后再说", false, on_next, 0);
        }
    }
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

static void open_at(step_t step)
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
    show_step(step);
}

void boopie_guide_start(void)
{
    open_at(S_HELLO);
}

void boopie_guide_resume(void)
{
    if (boopie_avatar_brain_just_changed()) {
        /* Back from the restart a change of AI made: say what happened. */
        bool xz = boopie_avatar_brain() == BOOPIE_BRAIN_XIAOZHI;
        strlcpy(s_note, xz ? "已换成小智，重启好了" : "已换成 Muse，重启好了", sizeof s_note);
        if (boopie_avatar_guide_at() < 0) {
            muse_state_set_caption("%s", xz ? "已换成小智，按住上面的键说话吧" : "已换成 Muse，按住上面的键说话吧");
            s_note[0] = '\0';
        }
    }
    int at = boopie_avatar_guide_at();
    if (at >= 0) {
        open_at(at < S_COUNT ? (step_t)at : S_HELLO);
    }
}

bool boopie_guide_active(void)
{
    return s_root != NULL;
}

void boopie_guide_tick(bool on_face)
{
    if (!s_root) {
        return;
    }
    if (s_away) {
        if (on_face) {
            s_away = false;
            show_step(s_step);
        }
        return;
    }
    /* The 小智 step shows the code as it comes, and the binding once it's done. */
    if (s_step == S_XZ) {
        char code[16] = "", now[40];
        boopie_xz_state_t st = boopie_xiaozhi_status(code, sizeof code, NULL, 0);
        snprintf(now, sizeof now, "%d|%s|%d", (int)st, code, muse_wifi_connected());
        if (strcmp(now, s_xz_shown) != 0) {
            show_step(s_step);
        }
    }
    /* The Muse step follows the pairing as it goes, and moves on once it's done. */
    if (s_step == S_MUSE_PAIR && (muse_paired() || (int)muse_link_state() != s_pair_shown)) {
        show_step(s_step);
    }
}
