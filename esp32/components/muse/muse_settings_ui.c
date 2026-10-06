/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "muse_settings_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_timer.h"

#include "muse_audio.h"
#include "muse_battery.h"
#include "muse_ble.h"
#include "muse_board.h"
#include "muse_chat.h"
#include "muse_input.h"
#include "muse_keypad.h"
#include "muse_link.h"
#include "muse_settings.h"
#include "muse_state.h"
#include "muse_text.h"
#include "muse_ui.h"
#include "muse_voice.h"
#include "muse_wifi.h"
#include "boopie_font.h"
#include "boopie_avatar.h"
#include "boopie_input.h"
#include "boopie_guide.h"
#include "boopie_setup.h"
#include "boopie_heads.h"
#include "boopie_sdk_token.h"
#include "boopie_vpn.h"
#include "boopie_ota.h"
#include "boopie_xiaozhi.h"
#include "boopie_history.h"
#include "boopie_store.h"
#include "boopie_viewers.h"
#include "boopie_icons.h"
#include "boopie_pixel.h"

/* Keep content in a column that stays inside a round panel (and fits a 368 px one). */
#define LIST_W 330
#define LIST_TOP 84
#define ROW_H 58
#define MAX_APS 12

#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff
#define COLOR_OK 0x6ff0bf
#define COLOR_WARN 0xffb45c
#define COLOR_DANGER 0xff5c5c

typedef void (*text_done_cb_t)(const char *text);

/* The screen size the text page is scaled to (see text_px). */
static int s_text_scale = 466;
static lv_obj_t *s_tile;
static lv_obj_t *s_current;
static lv_obj_t *s_home, *s_wifi, *s_hatch, *s_ble, *s_sound, *s_sleep, *s_battery, *s_power, *s_text;
static lv_obj_t *s_avatar, *s_home_avatar;   /* Boopie: the avatar page */

/*
 * Only home is kept. A sub-page is built when it opens and deleted on the way
 * back, so only the one on screen holds RAM: all of them at once cost ~50 KB,
 * which a board without PSRAM needs for Link's session.
 */
typedef struct {
    lv_obj_t **obj;
    void (*build)(lv_obj_t *tile);
} page_t;

/* Home values. */
static lv_obj_t *s_home_wifi, *s_home_ble, *s_home_sound, *s_home_sleep, *s_home_battery, *s_about;

/* Wi-Fi page. */
static lv_obj_t *s_wifi_sw, *s_wifi_status, *s_wifi_saved, *s_wifi_scan_btn, *s_wifi_scan_lbl, *s_wifi_list;
static uint32_t s_shown_scan_gen = UINT32_MAX;
static muse_wifi_ap_t s_aps[MAX_APS];
static muse_wifi_saved_t s_saved[MUSE_WIFI_SAVED_MAX];
static lv_obj_t *s_saved_vals[MUSE_WIFI_SAVED_MAX];
static int s_saved_n = -1;   /* as listed; -1 before the first fill */
static int s_forget_armed = -1;
static int64_t s_forget_armed_us;
static char s_join_ssid[MUSE_SSID_MAX + 1];

/* Hatch page. */
static lv_obj_t *s_hatch_status, *s_hatch_host, *s_hatch_vm, *s_hatch_token;
static lv_obj_t *s_sdk_value;    /* Boopie: whether the developer token's in */
static lv_obj_t *s_link_status, *s_link_state, *s_link_reset_lbl;
static int64_t s_link_reset_armed_us;

/* Bluetooth page. */
static lv_obj_t *s_ble_sw, *s_ble_status;

/* Sound page. */
static lv_obj_t *s_spk_sw, *s_vol_val, *s_vol_sl, *s_gain_val, *s_gain_sl, *s_bright_val, *s_bright_sl, *s_mic_bar, *s_mic_val;

/* Sleep page. */
static const int SLEEP_CHOICES[] = { 0, 30, 60, 120, 300, 600 };
static const char *const SLEEP_NAMES[] = { "从不", "30 秒", "1 分钟", "2 分钟", "5 分钟", "10 分钟" };
#define SLEEP_COUNT (int)(sizeof(SLEEP_CHOICES) / sizeof(SLEEP_CHOICES[0]))
static lv_obj_t *s_sleep_checks[SLEEP_COUNT];

/* Battery page. */
static lv_obj_t *s_batt_status, *s_batt_level, *s_batt_drain, *s_batt_full, *s_batt_off, *s_batt_slept, *s_batt_wakes,
    *s_batt_busy, *s_batt_awake;
static int64_t s_batt_shown_us;

/* Text entry page. */
static lv_obj_t *s_text_title, *s_text_ta;
static lv_obj_t *s_text_kp;                 /* a screen under 2" */
static lv_obj_t *s_text_kb, *s_text_show;   /* a bigger one */
static text_done_cb_t s_text_done;
static lv_obj_t *s_text_back;

/* ---------- building blocks ---------- */

/* Network and phone names can have characters the fonts lack (muse_text.h). */
#define SHOWN_MAX 96

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    char shown[SHOWN_MAX];
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, boopie_font_with_cjk(font), 0);   /* Boopie: Chinese names */
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, muse_text_showable(text, shown, sizeof(shown)));
    return l;
}

static void set_text(lv_obj_t *l, const char *text)
{
    char shown[SHOWN_MAX];
    text = muse_text_showable(text, shown, sizeof(shown));
    if (strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
}

static lv_obj_t *note(lv_obj_t *list, const char *text)
{
    lv_obj_t *l = label(list, &lv_font_montserrat_16, COLOR_DIM, text);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

static void go_back(void);

static void on_gesture(lv_event_t *e)
{
    (void)e;
    /* Dragging a slider right isn't a "back" swipe. */
    lv_obj_t *pressed = lv_indev_get_active_obj();
    if (pressed && lv_obj_check_type(pressed, &lv_slider_class)) {
        return;
    }
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_active());
        go_back();
    }
}

/* Swipe right on a page to go back. By default a swipe bubbles up past the
 * page to the screen, so the page has to stop it. */
static void catch_swipes(lv_obj_t *p)
{
    lv_obj_remove_flag(p, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(p, on_gesture, LV_EVENT_GESTURE, NULL);
}

static void on_back(lv_event_t *e)
{
    (void)e;
    go_back();
}

static lv_obj_t *back_button(lv_obj_t *p)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 56, 48);
    lv_obj_align(b, LV_ALIGN_TOP_MID, -112, 28);
    lv_obj_add_event_cb(b, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = label(b, &lv_font_montserrat_20, COLOR_ACCENT, LV_SYMBOL_LEFT);
    lv_obj_center(arrow);
    return b;
}

/* A page: title, optional back arrow, and a vertically scrolling column. */
static lv_obj_t *page(lv_obj_t *tile, const char *title, bool back, lv_obj_t **list_out)
{
    lv_obj_t *p = lv_obj_create(tile);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    catch_swipes(p);

    /* Boopie: titles in the pixel font, Chinese too, as the face's state is. */
    lv_obj_t *t = label(p, &boopie_font_pixel_24, COLOR_ACCENT, title);
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 44);

    if (back) {
        back_button(p);
    }

    lv_obj_t *list = lv_obj_create(p);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LIST_W, muse_board->height - LIST_TOP);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, LIST_TOP);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_style_pad_bottom(list, 110, 0);   /* lets the last row scroll up out of the bottom curve */
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    *list_out = list;
    return p;
}

static lv_obj_t *card(lv_obj_t *list, bool clickable)
{
    lv_obj_t *c = clickable ? lv_button_create(list) : lv_obj_create(list);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, lv_pct(100), ROW_H);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(c, 16, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c, 12, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/* Tappable row: icon, text, right-aligned value. */
static lv_obj_t *row(lv_obj_t *list, const char *icon, const char *text, lv_obj_t **value_out,
                     lv_event_cb_t cb, void *user)
{
    lv_obj_t *c = card(list, true);
    if (icon) {
        label(c, &lv_font_montserrat_20, COLOR_ACCENT, icon);
    }
    lv_obj_t *t = label(c, &lv_font_montserrat_20, COLOR_TEXT, text);
    lv_obj_set_flex_grow(t, 1);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    if (value_out) {
        lv_obj_t *v = label(c, &lv_font_montserrat_16, COLOR_DIM, "");
        lv_obj_set_style_max_width(v, 130, 0);
        lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
        *value_out = v;
    }
    lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, user);
    return c;
}

/* Boopie: a settings home row, its pixel icon on a coloured tile. */
static lv_obj_t *icon_row(lv_obj_t *list, boopie_icon_t icon, const char *text, lv_obj_t **value_out,
                          lv_event_cb_t cb, void *user)
{
    lv_obj_t *c = row(list, NULL, text, value_out, cb, user);
    lv_obj_t *tile = boopie_icon_tile(c, icon, 36, 2);
    lv_obj_move_to_index(tile, 0);
    return c;
}

/* Boopie: a row leading on to another page: its arrow on the right. */
static lv_obj_t *nav_row(lv_obj_t *list, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *c = row(list, NULL, text, NULL, cb, user);
    label(c, &lv_font_montserrat_16, COLOR_DIM, LV_SYMBOL_RIGHT);
    return c;
}

static lv_obj_t *switch_row(lv_obj_t *list, const char *text, bool on, lv_event_cb_t cb)
{
    lv_obj_t *c = card(list, false);
    lv_obj_t *t = label(c, &lv_font_montserrat_20, COLOR_TEXT, text);
    lv_obj_set_flex_grow(t, 1);
    lv_obj_t *sw = lv_switch_create(c);
    lv_obj_set_size(sw, 60, 32);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x3a3358), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

static lv_obj_t *button(lv_obj_t *list, const char *text, uint32_t color, lv_event_cb_t cb, lv_obj_t **label_out)
{
    lv_obj_t *b = card(list, true);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = label(b, &lv_font_montserrat_20, color, text);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    if (label_out) {
        *label_out = l;
    }
    return b;
}

/* Label + value on one line, slider below. */
static lv_obj_t *slider(lv_obj_t *list, const char *text, int lo, int hi, int value, lv_obj_t **value_out,
                        lv_event_cb_t cb)
{
    lv_obj_t *c = lv_obj_create(list);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(c, 8, 0);
    lv_obj_set_style_pad_ver(c, 6, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    label(c, &lv_font_montserrat_20, COLOR_TEXT, text);
    lv_obj_t *v = label(c, &lv_font_montserrat_20, COLOR_ACCENT, "");
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, 0);
    *value_out = v;

    lv_obj_t *s = lv_slider_create(c);
    lv_obj_set_width(s, lv_pct(94));
    lv_obj_set_height(s, 12);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 40);
    lv_slider_set_range(s, lo, hi);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s, lv_color_hex(0x2a2345), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 6, LV_PART_KNOB);
    lv_obj_set_ext_click_area(s, 16);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s, cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *spacer = lv_obj_create(c);   /* room below the slider */
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_align(spacer, LV_ALIGN_TOP_LEFT, 0, 64);
    return s;
}

/* A column of rows inside the page's list, filled in later. */
static lv_obj_t *column(lv_obj_t *list)
{
    lv_obj_t *l = lv_obj_create(list);
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(l, 10, 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    return l;
}

/* Label and value, not tappable. */
static lv_obj_t *info_row(lv_obj_t *list, const char *text)
{
    lv_obj_t *c = card(list, false);
    lv_obj_set_height(c, 44);
    lv_obj_t *t = label(c, &lv_font_montserrat_16, COLOR_TEXT, text);
    lv_obj_set_flex_grow(t, 1);
    return label(c, &lv_font_montserrat_16, COLOR_ACCENT, "");
}

/* Boopie: how-to steps, a card of them: a heading, then each step on its own
 * lines, big enough to read on the round screen. */
static lv_obj_t *steps(lv_obj_t *list, const char *heading, const char *const *lines, int n)
{
    lv_obj_t *c = lv_obj_create(list);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 8, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    if (heading) {
        label(c, &lv_font_montserrat_20, COLOR_ACCENT, heading);
    }
    for (int i = 0; i < n; i++) {
        char line[160];
        snprintf(line, sizeof line, "%d. %s", i + 1, lines[i]);
        lv_obj_t *l = label(c, &lv_font_montserrat_20, COLOR_TEXT, line);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    }
    return c;
}

/* Boopie: a heading row that opens and closes what's under it (the debug readings). */
static void on_fold(lv_event_t *e)
{
    lv_obj_t *body = lv_event_get_user_data(e);
    lv_obj_t *arrow = lv_obj_get_child(lv_event_get_current_target(e), -1);
    bool hidden = lv_obj_has_flag(body, LV_OBJ_FLAG_HIDDEN);
    if (hidden) {
        lv_obj_remove_flag(body, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(arrow, hidden ? LV_SYMBOL_DOWN : LV_SYMBOL_RIGHT);
}

static lv_obj_t *fold(lv_obj_t *list, const char *text)
{
    lv_obj_t *head = card(list, true);
    lv_obj_set_height(head, 48);
    lv_obj_t *t = label(head, &lv_font_montserrat_16, COLOR_DIM, text);
    lv_obj_set_flex_grow(t, 1);
    label(head, &lv_font_montserrat_16, COLOR_DIM, LV_SYMBOL_RIGHT);
    lv_obj_t *body = column(list);
    lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(head, on_fold, LV_EVENT_CLICKED, body);
    return body;
}

/* ---------- navigation ---------- */

/* Boopie's pages, defined further down; dropped like Muse's. Left out of the
 * list below, a page was deleted but still pointed at, and opened again it
 * crashed (lv_obj_get_display: No screen found, or a freed label written). */
static lv_obj_t *s_brain, *s_xiaozhi, *s_vpn, *s_storage;

static void drop(lv_obj_t *p)
{
    lv_obj_t **const pages[] = { &s_wifi, &s_hatch, &s_ble, &s_sound, &s_sleep, &s_battery, &s_power, &s_text,
                                 &s_avatar, &s_brain, &s_xiaozhi, &s_vpn, &s_storage };
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        if (*pages[i] == p) {
            *pages[i] = NULL;
        }
    }
    lv_obj_delete_async(p);   /* we may be in one of its own events */
}

static void show(lv_obj_t *p)
{
    lv_obj_t *prev = s_current;
    if (prev == p) {
        return;
    }
    if (prev) {
        lv_obj_add_flag(prev, LV_OBJ_FLAG_HIDDEN);
    }
    if (prev == s_sound) {
        muse_voice_set_monitor(false);
    }
    lv_obj_remove_flag(p, LV_OBJ_FLAG_HIDDEN);
    s_current = p;
    if (p == s_sound) {
        muse_voice_set_monitor(true);
    }
    muse_ui_set_swipe_enabled(p == s_home);
    /* Home keeps nothing open; the text page returns to the page that opened it. */
    if (prev && prev != s_home && (p == s_home || prev == s_text)) {
        drop(prev);
    }
}

static void close_text(void);

/* Boopie: a page opened from another page (the brain's Muse page) goes back
 * to that one, not home. */
static lv_obj_t *s_back_to;

/* Boopie: a page opened from elsewhere (小窝's 换装) goes back there, not to
 * the settings list: the list is put back quietly and this is called. */
static void (*s_leave_to)(void);

static void go_back(void)
{
    if (s_current == s_text) {
        close_text();
    } else if (s_leave_to && s_current != s_home) {
        void (*leave)(void) = s_leave_to;
        s_leave_to = NULL;
        s_back_to = NULL;
        show(s_home);
        leave();
    } else if (s_back_to && s_back_to != s_current) {
        lv_obj_t *to = s_back_to;
        s_back_to = NULL;
        show(to);
    } else if (s_current != s_home) {
        s_back_to = NULL;
        show(s_home);
    }
}

static void on_nav(lv_event_t *e)
{
    const page_t *page = lv_event_get_user_data(e);
    if (!*page->obj) {
        page->build(s_tile);
    }
    s_back_to = s_current != s_home ? s_current : NULL;
    show(*page->obj);
    muse_settings_ui_tick(true);   /* fill it in now, not at the next tick */
}

/* ---------- text entry ---------- */

/* Laid out for a 466 px screen; a smaller round one scales it about its centre. */
static int text_px(int v)
{
    return v * s_text_scale / 466;
}

static int text_y(int y)
{
    return muse_board->height / 2 + text_px(y - 233);
}

static void close_text(void)
{
    lv_textarea_set_text(s_text_ta, "");   /* don't leave secrets in the widget */
    show(s_text_back);
}

static void on_text_ready(lv_event_t *e)
{
    (void)e;
    text_done_cb_t done = s_text_done;
    char *text = strdup(lv_textarea_get_text(s_text_ta));
    close_text();
    if (done && text) {
        done(text);
    }
    if (text) {
        memset(text, 0, strlen(text));
        free(text);
    }
}

static void on_text_cancel(lv_event_t *e)
{
    (void)e;
    close_text();
}

static void on_text_show(lv_event_t *e)
{
    (void)e;
    bool pw = !lv_textarea_get_password_mode(s_text_ta);
    lv_textarea_set_password_mode(s_text_ta, pw);
    lv_label_set_text(lv_obj_get_child(s_text_show, 0), pw ? "显示" : "隐藏");
}

/* Beside the keyboard's field, a button to show a password. */
static void fit_show_button(bool password)
{
    int w = text_px(300), show_w = 70, gap = 8;
    lv_obj_set_width(s_text_ta, password ? w - show_w - gap : w);
    lv_obj_align(s_text_ta, LV_ALIGN_TOP_MID, password ? -(show_w + gap) / 2 : 0, text_y(76));
    lv_obj_align(s_text_show, LV_ALIGN_TOP_MID, (w - show_w) / 2, text_y(76));
    lv_label_set_text(lv_obj_get_child(s_text_show, 0), "显示");
    if (password) {
        lv_obj_remove_flag(s_text_show, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_text_show, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_keyboard(void)
{
    s_text_show = lv_button_create(s_text);
    lv_obj_remove_style_all(s_text_show);
    lv_obj_set_size(s_text_show, 70, text_px(48));
    lv_obj_set_style_radius(s_text_show, 14, 0);
    lv_obj_set_style_bg_opa(s_text_show, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_text_show, lv_color_hex(COLOR_CARD), 0);
    lv_obj_add_event_cb(s_text_show, on_text_show, LV_EVENT_CLICKED, NULL);
    lv_obj_center(label(s_text_show, &lv_font_montserrat_16, COLOR_TEXT, "显示"));

    /* Inside the circle, or across the rest of the screen. */
    s_text_kb = lv_keyboard_create(s_text);
    int y = text_y(136);
    if (muse_board->round) {
        lv_obj_set_size(s_text_kb, text_px(384), text_px(206));
    } else {
        int w = muse_board->width - 16;
        int h = muse_board->height - y - 8;
        lv_obj_set_size(s_text_kb, w, LV_MIN(h, w * 206 / 384));
    }
    lv_obj_align(s_text_kb, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_opa(s_text_kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_text_kb, 2, 0);
    lv_obj_set_style_pad_gap(s_text_kb, 4, 0);
    lv_obj_set_style_text_font(s_text_kb, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_text_kb, lv_color_hex(COLOR_CARD), LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_text_kb, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_radius(s_text_kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_text_kb, 0, LV_PART_ITEMS);
    lv_obj_remove_flag(s_text_kb, LV_OBJ_FLAG_GESTURE_BUBBLE);   /* a sloppy swipe mustn't lose the text */
    lv_keyboard_set_textarea(s_text_kb, s_text_ta);
    lv_obj_add_event_cb(s_text_kb, on_text_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_text_kb, on_text_cancel, LV_EVENT_CANCEL, NULL);
}

static void build_text_page(lv_obj_t *tile)
{
    s_text = lv_obj_create(tile);
    lv_obj_remove_style_all(s_text);
    lv_obj_set_size(s_text, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_text, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_text, LV_OBJ_FLAG_HIDDEN);
    catch_swipes(s_text);
    lv_obj_t *back = back_button(s_text);

    /* Between the back arrow and its mirror image. */
    s_text_title = label(s_text, &lv_font_montserrat_20, COLOR_ACCENT, "");
    lv_obj_set_width(s_text_title, 150);
    lv_obj_set_style_text_align(s_text_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_text_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(s_text_title, LV_ALIGN_TOP_MID, 0, 40);

    const lv_font_t *font = &lv_font_montserrat_20;
    int h = text_px(48), border = 2;
    int pad = (h - 2 * border - lv_font_get_line_height(font)) / 2;
    s_text_ta = lv_textarea_create(s_text);
    lv_textarea_set_one_line(s_text_ta, true);
    lv_obj_set_size(s_text_ta, text_px(300), h);
    lv_obj_set_style_text_font(s_text_ta, font, 0);
    lv_obj_set_style_pad_ver(s_text_ta, pad > 0 ? pad : 0, 0);
    lv_obj_set_style_pad_hor(s_text_ta, 14, 0);
    lv_obj_set_style_bg_color(s_text_ta, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_text_color(s_text_ta, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_border_color(s_text_ta, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(s_text_ta, border, 0);
    lv_obj_set_style_radius(s_text_ta, 14, 0);
    lv_obj_set_style_text_font(s_text_ta, &lv_font_montserrat_16, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_color(s_text_ta, lv_color_hex(COLOR_DIM), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_state(s_text_ta, LV_STATE_FOCUSED);
    lv_obj_align(s_text_ta, LV_ALIGN_TOP_MID, 0, text_y(76));

    /* A full keyboard's keys are too small to hit on a screen under 2". */
    if (muse_board->diagonal_in >= 2.0f) {
        build_keyboard();
        return;
    }

    /* The keys out to the screen's edges, and the rest squeezed in above them:
     * the back arrow beside the field, the title over both. */
    lv_obj_align(s_text_title, LV_ALIGN_TOP_MID, 0, text_y(16));
    lv_obj_set_width(s_text_title, text_px(180));
    lv_obj_set_width(s_text_ta, text_px(224));
    lv_obj_align(s_text_ta, LV_ALIGN_TOP_MID, 0, text_y(44));
    lv_obj_set_size(back, text_px(48), h);
    lv_obj_align(back, LV_ALIGN_TOP_MID, -text_px(140), text_y(44));
    s_text_kp = muse_keypad_create(s_text, s_text_ta, muse_board->round);
    int top = text_y(98);
    lv_obj_set_size(s_text_kp, muse_board->width, muse_board->height - top);
    lv_obj_align(s_text_kp, LV_ALIGN_TOP_MID, 0, top);
    if (muse_board->round) {
        /* Lifts the bottom row's labels inside the circle. The button matrix
         * stretches edge keys' taps over padding up to LV_DPI_DEF / 10, and not
         * at all over more, so this much keeps them reaching the edge. */
        lv_obj_set_style_pad_bottom(s_text_kp, LV_DPI_DEF / 10, 0);
    }
    lv_obj_add_event_cb(s_text_kp, on_text_ready, LV_EVENT_READY, NULL);
}

/* The hint shows in the empty field, so keep it short. */
static void open_text(const char *title, const char *initial, bool password, int max_len, const char *hint,
                      text_done_cb_t done, lv_obj_t *back)
{
    if (!s_text) {
        build_text_page(s_tile);
    }
    set_text(s_text_title, title);
    lv_textarea_set_max_length(s_text_ta, max_len);
    lv_textarea_set_password_mode(s_text_ta, password);
    lv_textarea_set_text(s_text_ta, initial ? initial : "");
    lv_textarea_set_placeholder_text(s_text_ta, hint ? hint : "");
    if (s_text_kp) {
        muse_keypad_reset(s_text_kp, password);
    } else {
        lv_keyboard_set_mode(s_text_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
        fit_show_button(password);
    }
    s_text_done = done;
    s_text_back = back;
    show(s_text);
}

/* ---------- Wi-Fi ---------- */

static void on_wifi_sw(lv_event_t *e)
{
    muse_settings_set_wifi_on(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void on_wifi_scan(lv_event_t *e)
{
    (void)e;
    muse_wifi_scan();
}

static void on_wifi_pass(const char *pass)
{
    muse_settings_set_wifi(s_join_ssid, pass);
}

static bool is_saved(const char *ssid)
{
    for (int i = 0; i < s_saved_n; i++) {
        if (!strcmp(s_saved[i].ssid, ssid)) {
            return true;
        }
    }
    return false;
}

/* A saved network asks again too, in case its password changed. */
static void on_wifi_ap(lv_event_t *e)
{
    const muse_wifi_ap_t *ap = &s_aps[(int)(intptr_t)lv_event_get_user_data(e)];
    strlcpy(s_join_ssid, ap->ssid, sizeof(s_join_ssid));
    if (ap->secure) {
        open_text(ap->ssid, "", true, MUSE_PASS_MAX, "密码", on_wifi_pass, s_wifi);
    } else {
        muse_settings_set_wifi(s_join_ssid, "");
    }
}

static void on_other_ssid(const char *ssid)
{
    if (!ssid[0]) {
        return;
    }
    strlcpy(s_join_ssid, ssid, sizeof(s_join_ssid));
    open_text(ssid, "", true, MUSE_PASS_MAX, "开放网络留空", on_wifi_pass, s_wifi);
}

static void on_wifi_other(lv_event_t *e)
{
    (void)e;
    open_text("其他网络", "", false, MUSE_SSID_MAX, "网络名称", on_other_ssid, s_wifi);
}

/* Two taps within a few seconds forget a saved network. */
static void on_wifi_saved(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    int64_t now = esp_timer_get_time();
    if (s_forget_armed == i && now - s_forget_armed_us < 4000000) {
        muse_wifi_forget(s_saved[i].ssid);
        s_forget_armed = -1;
        return;
    }
    s_forget_armed = i;
    s_forget_armed_us = now;
}

static bool same_saved(const muse_wifi_saved_t *a, int n)
{
    if (n != s_saved_n) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(a[i].ssid, s_saved[i].ssid) || a[i].hidden != s_saved[i].hidden) {
            return false;
        }
    }
    return true;
}

static void rebuild_saved_list(void)
{
    muse_wifi_saved_t saved[MUSE_WIFI_SAVED_MAX];
    int n = muse_wifi_saved(saved, MUSE_WIFI_SAVED_MAX);
    if (same_saved(saved, n)) {
        return;
    }
    memcpy(s_saved, saved, n * sizeof(saved[0]));
    s_saved_n = n;
    s_forget_armed = -1;
    s_shown_scan_gen = UINT32_MAX;   /* re-mark the saved ones in the scan list */
    lv_obj_clean(s_wifi_saved);
    if (n) {
        note(s_wifi_saved, "已保存的网络");
    }
    for (int i = 0; i < n; i++) {
        row(s_wifi_saved, LV_SYMBOL_WIFI, saved[i].ssid, &s_saved_vals[i], on_wifi_saved, (void *)(intptr_t)i);
    }
}

static void tick_saved_list(const muse_wifi_status_t *w)
{
    if (s_forget_armed >= 0 && esp_timer_get_time() - s_forget_armed_us >= 4000000) {
        s_forget_armed = -1;
    }
    for (int i = 0; i < s_saved_n; i++) {
        const char *text = "";
        uint32_t color = COLOR_DIM;
        if (i == s_forget_armed) {
            text = "再点一下忘记";
            color = COLOR_DANGER;
        } else if (w->state == MUSE_WIFI_CONNECTED && !strcmp(w->ssid, s_saved[i].ssid)) {
            text = "已连接";
            color = COLOR_OK;
        } else if (s_saved[i].hidden) {
            text = "隐藏网络";
        }
        set_text(s_saved_vals[i], text);
        lv_obj_set_style_text_color(s_saved_vals[i], lv_color_hex(color), 0);
    }
}

/* Returns true when fresh results include a saved network. */
static bool rebuild_scan_list(void)
{
    uint32_t gen;
    int n = muse_wifi_scan_results(s_aps, MAX_APS, &gen);
    if (gen == s_shown_scan_gen) {
        return false;
    }
    bool fresh = s_shown_scan_gen != UINT32_MAX, saved_seen = false;
    s_shown_scan_gen = gen;
    lv_obj_clean(s_wifi_list);
    for (int i = 0; i < n; i++) {
        lv_obj_t *v;
        row(s_wifi_list, NULL, s_aps[i].ssid, &v, on_wifi_ap, (void *)(intptr_t)i);
        bool saved = is_saved(s_aps[i].ssid);
        saved_seen |= saved;
        char buf[24];
        snprintf(buf, sizeof(buf), "%s%d dBm", saved ? "已保存  " : (s_aps[i].secure ? "" : "开放  "), s_aps[i].rssi);
        lv_label_set_text(v, buf);
    }
    if (gen && !n) {
        note(s_wifi_list, "没找到网络");
    }
    return fresh && saved_seen;
}

static void build_wifi_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_wifi = page(tile, "无线网络", true, &list);
    s_shown_scan_gen = UINT32_MAX;   /* the lists start empty */
    s_saved_n = -1;
    s_forget_armed = -1;
    s_wifi_sw = switch_row(list, "Wi-Fi", muse_settings_wifi_on(), on_wifi_sw);
    s_wifi_status = note(list, "");

    s_wifi_saved = column(list);
    s_wifi_scan_btn = button(list, LV_SYMBOL_REFRESH "  Scan for networks", COLOR_ACCENT, on_wifi_scan, &s_wifi_scan_lbl);
    s_wifi_list = column(list);

    row(list, LV_SYMBOL_EDIT, "其他网络…", NULL, on_wifi_other, NULL);
    lv_obj_t *mac = info_row(list, "MAC 地址");
    uint8_t m[6];
    if (esp_read_mac(m, ESP_MAC_WIFI_STA) == ESP_OK) {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
        lv_label_set_text(mac, buf);
    }
    note(list, "最多记住 8 个网络，自动连信号最强的。已保存的网络连点两下可以忘记。");
}

static void tick_wifi(void)
{
    muse_wifi_status_t w;
    muse_wifi_status(&w);
    char buf[128];
    switch (w.state) {
    case MUSE_WIFI_OFF:
        strlcpy(buf, "Wi-Fi 已关", sizeof(buf));
        break;
    case MUSE_WIFI_NO_NETWORK:
        strlcpy(buf, "还没有保存的网络，搜索后选一个。", sizeof(buf));
        break;
    case MUSE_WIFI_CONNECTING:
        snprintf(buf, sizeof(buf), "正在连接 %s\n%s", w.ssid, w.detail);
        break;
    case MUSE_WIFI_CONNECTED:
        snprintf(buf, sizeof(buf), "已连接 %s\n%s    %d dBm", w.ssid, w.ip, w.rssi);
        break;
    case MUSE_WIFI_NOT_NEARBY:
        strlcpy(buf, "附近没有保存过的网络\n一分钟内再找一次", sizeof(buf));
        break;
    case MUSE_WIFI_FAILED:
    default:
        snprintf(buf, sizeof(buf), "连不上 %s\n%s", w.ssid, w.detail);
        break;
    }
    set_text(s_wifi_status, buf);
    lv_obj_set_style_text_color(s_wifi_status, lv_color_hex(w.state == MUSE_WIFI_CONNECTED ? COLOR_OK :
                                                            w.state == MUSE_WIFI_FAILED ? COLOR_WARN : COLOR_DIM), 0);

    bool on = w.state != MUSE_WIFI_OFF;
    if (on != lv_obj_has_state(s_wifi_sw, LV_STATE_CHECKED)) {
        lv_obj_set_state(s_wifi_sw, LV_STATE_CHECKED, on);
    }
    lv_obj_set_flag(s_wifi_scan_btn, LV_OBJ_FLAG_HIDDEN, !on);
    lv_obj_set_flag(s_wifi_list, LV_OBJ_FLAG_HIDDEN, !on);
    set_text(s_wifi_scan_lbl, muse_wifi_scanning() ? "正在搜索…" : LV_SYMBOL_REFRESH "  搜索网络");
    rebuild_saved_list();
    tick_saved_list(&w);
    /* The scan found one: join it now rather than at the next look. */
    if (rebuild_scan_list() && w.state == MUSE_WIFI_NOT_NEARBY) {
        muse_wifi_apply();
    }
}

/* ---------- Hatch ---------- */

/* Boopie: the states in Chinese (muse_hatch_state_name and
 * muse_link_state_name stay English for the serial console). */
static const char *hatch_state_text(muse_hatch_state_t st)
{
    switch (st) {
    case MUSE_HATCH_NOT_SET: return "未设置";
    case MUSE_HATCH_OFFLINE: return "没联网";
    case MUSE_HATCH_UNTESTED: return "还没测试";
    case MUSE_HATCH_TESTING: return "正在测试…";
    case MUSE_HATCH_REACHABLE: return "已连上";
    case MUSE_HATCH_UNREACHABLE: return "连不上";
    default: return "";
    }
}

static const char *link_state_text(muse_link_state_t st)
{
    switch (st) {
    case MUSE_LINK_UNPAIRED: return "等待 App 添加";
    case MUSE_LINK_PAIRING: return "App 已连上";
    case MUSE_LINK_CONFIRM: return "按上面的键确认";
    case MUSE_LINK_CONNECTING: return "正在连接";
    case MUSE_LINK_ONLINE: return "在线";
    case MUSE_LINK_OFFLINE: return "离线";
    case MUSE_LINK_ERROR: return "出错了";
    default: return "启动中";
    }
}

static void on_hatch_host_done(const char *text) { muse_settings_set_hatch_host(text); }
static void on_hatch_vm_done(const char *text) { muse_settings_set_hatch_vm(text); }

static void on_hatch_host(lv_event_t *e)
{
    (void)e;
    char host[MUSE_HOST_MAX + 1];
    muse_settings_hatch_host(host);
    open_text("Muse 服务器", host, false, MUSE_HOST_MAX, "留空用默认", on_hatch_host_done, s_hatch);
}

static void on_hatch_vm(lv_event_t *e)
{
    (void)e;
    char vm[MUSE_VM_MAX + 1];
    muse_settings_hatch_vm(vm);
    open_text("VM ID", vm, false, MUSE_VM_MAX, "可不填", on_hatch_vm_done, s_hatch);
}

/* Boopie: the developer token is pasted on the phone. */
static void on_sdk_token(lv_event_t *e)
{
    (void)e;
    boopie_setup_open(NULL);
}

static void on_hatch_test(lv_event_t *e)
{
    (void)e;
    muse_hatch_test();
}

/* Two taps within a few seconds: this wipes Wi-Fi and the Muse app pairing. */
static void on_link_reset(lv_event_t *e)
{
    (void)e;
    int64_t now = esp_timer_get_time();
    if (s_link_reset_armed_us && now - s_link_reset_armed_us < 5000000) {
        set_text(s_link_reset_lbl, "正在重置…");
        muse_link_reset_setup();
        return;
    }
    s_link_reset_armed_us = now;
    set_text(s_link_reset_lbl, "再点一下确认重置");
}

static void build_hatch_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_hatch = page(tile, "Muse", true, &list);
    s_link_reset_armed_us = 0;
    s_link_status = info_row(list, "App 配对");
    s_link_state = info_row(list, "连接");
    s_hatch_status = info_row(list, "Muse 服务");
    /* Boopie: the developer token, entered by whoever sets it up, and the
     * device token, which pairing fetches: neither is typed here. */
    row(list, NULL, "开发者 token", &s_sdk_value, on_sdk_token, NULL);
    s_hatch_token = info_row(list, "设备 token");
    button(list, "测试连接", COLOR_ACCENT, on_hatch_test, NULL);
    /* Boopie: how to get Muse on, step by step (docs/boopie-interaction.md). */
    static const char *const HOWTO[] = {
        "开发者 token：gadgets.muse.ai 登录，Account › SDK tokens 生成，用手机扫码设置粘贴",
        "打开 设置 › VPN，板子要能上海外网络",
        "手机装 Muse App 并登录，设置 › 设备 › 打开开发者模式",
        "设置 › 设备 › 右上角 +，添加这块板子",
        "屏幕提示时，按一下上面的键确认",
        "上面显示\"已配对\"\"已连上\"就好了",
    };
    steps(list, "怎样接入 Muse", HOWTO, (int)(sizeof HOWTO / sizeof HOWTO[0]));
    note(list, "高级");
    row(list, NULL, "服务器", &s_hatch_host, on_hatch_host, NULL);
    row(list, NULL, "VM ID", &s_hatch_vm, on_hatch_vm, NULL);
    note(list, "VM ID 用来在多台 VM 里选一台，一般不用改。");
    button(list, "重置配对", COLOR_DANGER, on_link_reset, &s_link_reset_lbl);
    note(list, "重置会忘掉 Wi-Fi 和 App 配对，然后重启。");
}

static void tick_hatch(void)
{
    bool linked = muse_link_hatch_linked();
    set_text(s_link_status, linked ? "已配对" : "未配对");
    lv_obj_set_style_text_color(s_link_status, lv_color_hex(linked ? COLOR_OK : COLOR_WARN), 0);
    set_text(s_link_state, link_state_text(muse_link_state()));
    if (s_link_reset_armed_us && esp_timer_get_time() - s_link_reset_armed_us >= 5000000) {
        s_link_reset_armed_us = 0;
        set_text(s_link_reset_lbl, "重置配对");
    }

    muse_hatch_status_t h;
    muse_hatch_status(&h);
    set_text(s_hatch_status, hatch_state_text(h.state));
    lv_obj_set_style_text_color(s_hatch_status, lv_color_hex(h.state == MUSE_HATCH_REACHABLE ? COLOR_OK :
                                                             h.state == MUSE_HATCH_UNREACHABLE ? COLOR_WARN : COLOR_DIM), 0);

    char host[MUSE_HOST_MAX + 1], vm[MUSE_VM_MAX + 1];
    muse_settings_hatch_host(host);
    muse_settings_hatch_vm(vm);
    set_text(s_hatch_host, host);
    set_text(s_hatch_vm, vm[0] ? vm : "未设置");
    set_text(s_hatch_token, muse_settings_hatch_token_len() ? "已获取" : "配对后自动获取");
    char sdk[BOOPIE_SDK_TOKEN_LEN + 1];
    bool has_sdk = boopie_sdk_token(sdk);
    memset(sdk, 0, sizeof sdk);
    set_text(s_sdk_value, has_sdk ? "已填写" : "未填写");
    lv_obj_set_style_text_color(s_sdk_value, lv_color_hex(has_sdk ? COLOR_OK : COLOR_WARN), 0);
}

/* ---------- Bluetooth ---------- */

static void on_ble_sw(lv_event_t *e)
{
    muse_settings_set_ble_on(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void on_ble_forget(lv_event_t *e)
{
    (void)e;
    muse_ble_forget_all();
}

static void build_ble_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_ble = page(tile, "蓝牙", true, &list);
    s_ble_sw = switch_row(list, "蓝牙", muse_settings_ble_on(), on_ble_sw);
    s_ble_status = note(list, "");
    button(list, "忘记已配对的手机", COLOR_DANGER, on_ble_forget, NULL);
    note(list, "Muse App 配对时要用蓝牙。打开会重启一下，下次开机自动关上，平时省下内存。");
}

static void tick_ble(void)
{
    muse_ble_status_t b;
    muse_ble_status(&b);
    char buf[96];
    switch (b.state) {
    case MUSE_BLE_OFF:
        strlcpy(buf, "已关", sizeof(buf));
        break;
    case MUSE_BLE_ADVERTISING:
        snprintf(buf, sizeof(buf), "手机能看到：%s", b.name);
        break;
    case MUSE_BLE_CONNECTED:
    default:
        snprintf(buf, sizeof(buf), "手机已连接\n%s", b.secure ? "已配对" : "等待配对");
        break;
    }
    set_text(s_ble_status, buf);
    lv_obj_set_style_text_color(s_ble_status, lv_color_hex(b.state == MUSE_BLE_CONNECTED && b.secure ? COLOR_OK : COLOR_DIM), 0);
    bool on = muse_settings_ble_on();
    if (on != lv_obj_has_state(s_ble_sw, LV_STATE_CHECKED)) {
        lv_obj_set_state(s_ble_sw, LV_STATE_CHECKED, on);
    }
}

/* ---------- Sound ---------- */

static void set_val(lv_obj_t *l, const char *fmt, int v)
{
    char buf[16];
    snprintf(buf, sizeof(buf), fmt, v);
    set_text(l, buf);
}

static void on_speaker_sw(lv_event_t *e)
{
    muse_settings_set_speaker_on(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void on_volume(lv_event_t *e)
{
    int v = lv_slider_get_value(s_vol_sl);
    set_val(s_vol_val, "%d%%", v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        muse_settings_set_volume(v);
        muse_voice_request_chirp();
    } else {
        muse_audio_set_volume(v);
    }
}

static void on_gain(lv_event_t *e)
{
    int db = lv_slider_get_value(s_gain_sl) * 3;
    set_val(s_gain_val, "%d dB", db);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        muse_settings_set_mic_gain(db);
    } else {
        muse_audio_set_mic_gain(db);
    }
}

static void on_bright(lv_event_t *e)
{
    int v = lv_slider_get_value(s_bright_sl);
    set_val(s_bright_val, "%d%%", v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        muse_settings_set_brightness(v);
    } else {
        muse_ui_preview_brightness(v);
    }
}

static void build_sound_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_sound = page(tile, "声音", true, &list);
    s_spk_sw = switch_row(list, "说话出声", muse_settings_speaker_on(), on_speaker_sw);
    s_vol_sl = slider(list, "音量", 0, 100, muse_settings_volume(), &s_vol_val, on_volume);
    s_gain_sl = slider(list, "麦克风灵敏度", 0, MUSE_MIC_GAIN_MAX / 3, muse_settings_mic_gain() / 3, &s_gain_val, on_gain);
    note(list, "关掉\"说话出声\"，回复只显示文字。");

    /* Boopie: the level meter is for tuning, so it's last, folded away. */
    lv_obj_t *dbg = fold(list, "调试信息：麦克风电平");
    lv_obj_t *meter = lv_obj_create(dbg);
    lv_obj_remove_style_all(meter);
    lv_obj_set_size(meter, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(meter, 8, 0);
    lv_obj_remove_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
    label(meter, &lv_font_montserrat_16, COLOR_DIM, "电平");
    s_mic_val = label(meter, &lv_font_montserrat_16, COLOR_DIM, "");
    lv_obj_align(s_mic_val, LV_ALIGN_TOP_RIGHT, 0, 0);
    s_mic_bar = lv_bar_create(meter);
    lv_obj_set_size(s_mic_bar, lv_pct(94), 10);
    lv_obj_align(s_mic_bar, LV_ALIGN_TOP_MID, 0, 26);
    lv_bar_set_range(s_mic_bar, 0, 60);   /* -70..-10 dBFS */
    lv_obj_set_style_bg_color(s_mic_bar, lv_color_hex(0x2a2345), LV_PART_MAIN);
    lv_obj_set_style_anim_duration(s_mic_bar, 80, 0);
    note(dbg, "离一臂远说话：电平条到绿色\n（-30 到 -15 dBFS），\n不变橙色最合适。");

    set_val(s_vol_val, "%d%%", muse_settings_volume());
    set_val(s_gain_val, "%d dB", muse_settings_mic_gain() / 3 * 3);
}

static void tick_sound(void)
{
    bool on = muse_settings_speaker_on();   /* also toggled from the face */
    if (on != lv_obj_has_state(s_spk_sw, LV_STATE_CHECKED)) {
        lv_obj_set_state(s_spk_sw, LV_STATE_CHECKED, on);
    }
    float db = muse_voice_monitor_db();
    int v = (int)(db + 70.0f);
    v = v < 0 ? 0 : (v > 60 ? 60 : v);
    lv_bar_set_value(s_mic_bar, v, LV_ANIM_ON);
    uint32_t color = db > -12.0f ? COLOR_WARN : (db > -30.0f ? COLOR_OK : COLOR_ACCENT);
    lv_obj_set_style_bg_color(s_mic_bar, lv_color_hex(color), LV_PART_INDICATOR);
    set_val(s_mic_val, "%d dBFS", (int)db);
}

/* ---------- Sleep ---------- */

static void on_sleep_choice(lv_event_t *e)
{
    muse_settings_set_sleep_s(SLEEP_CHOICES[(int)(intptr_t)lv_event_get_user_data(e)]);
}

static void on_sleep_now(lv_event_t *e)
{
    (void)e;
    muse_state_set_asleep(true);
}

static void on_posture(lv_event_t *e)
{
    boopie_avatar_set_posture_on(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
}

static void build_sleep_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_sleep = page(tile, "显示与熄屏", true, &list);
    s_bright_sl = slider(list, "亮度", 10, 100, muse_settings_brightness(), &s_bright_val, on_bright);
    set_val(s_bright_val, "%d%%", muse_settings_brightness());
    note(list, "多久没动静就熄屏：");
    for (int i = 0; i < SLEEP_COUNT; i++) {
        row(list, NULL, SLEEP_NAMES[i], &s_sleep_checks[i], on_sleep_choice, (void *)(intptr_t)i);
        lv_obj_set_style_text_color(s_sleep_checks[i], lv_color_hex(COLOR_ACCENT), 0);
    }
    button(list, LV_SYMBOL_EYE_CLOSE "  现在熄屏", COLOR_ACCENT, on_sleep_now, NULL);
    note(list, "点屏幕或按任一个键就会亮。");
    /* Boopie: 姿势感应. */
    switch_row(list, "姿势感应", boopie_avatar_posture_on(), on_posture);
    note(list, "屏幕朝下扣在桌上就熄屏；翻回来或拿起来就亮，宠物会打招呼。倒过来拿它会慌。");
}

static const char *sleep_name(int secs)
{
    for (int i = 0; i < SLEEP_COUNT; i++) {
        if (SLEEP_CHOICES[i] == secs) {
            return SLEEP_NAMES[i];
        }
    }
    return "自定义";
}

static void tick_sleep(void)
{
    int cur = muse_settings_sleep_s();
    for (int i = 0; i < SLEEP_COUNT; i++) {
        set_text(s_sleep_checks[i], SLEEP_CHOICES[i] == cur ? LV_SYMBOL_OK : "");
    }
}

/* ---------- Battery ---------- */

static void on_battery_reset(lv_event_t *e)
{
    (void)e;
    muse_battery_reset();
    s_batt_shown_us = 0;   /* show it now */
}

static void build_battery_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_battery = page(tile, "电池", true, &list);
    s_batt_shown_us = 0;
    s_batt_level = info_row(list, "电量");
    s_batt_full = info_row(list, "充满能用");
    /* Boopie: the rest measures power use, for tuning: last, folded away. */
    lv_obj_t *dbg = fold(list, "调试信息：耗电测量");
    s_batt_status = note(dbg, "");
    s_batt_drain = info_row(dbg, "已用");
    s_batt_off = info_row(dbg, "熄屏时间");
    s_batt_slept = info_row(dbg, "芯片睡眠");
    s_batt_wakes = info_row(dbg, "唤醒次数");
    s_batt_busy = info_row(dbg, "CPU 忙碌");
    s_batt_awake = note(dbg, "");
    button(dbg, LV_SYMBOL_REFRESH "  重新测量", COLOR_ACCENT, on_battery_reset, NULL);
    note(dbg, "从拔掉 USB 开始测，\n插上 USB 结束。\n测几个小时才准。");
}

/* A per-mille figure as a percentage. */
static void set_pm(lv_obj_t *l, int pm)
{
    char buf[16] = "-";
    if (pm >= 0) {
        snprintf(buf, sizeof(buf), "%d.%d%%", pm / 10, pm % 10);
    }
    set_text(l, buf);
}

static void tick_battery(void)
{
    int64_t now = esp_timer_get_time();
    if (s_batt_shown_us && now - s_batt_shown_us < 1000000) {
        return;
    }
    s_batt_shown_us = now;
    muse_battery_t b;
    muse_battery_read(&b);
    muse_power_t p = muse_state_power();
    char buf[96], t[24];

    int h = (int)(b.secs / 3600), m = (int)(b.secs / 60 % 60);
    if (h) {
        snprintf(t, sizeof(t), "%d 小时 %d 分", h, m);
    } else {
        snprintf(t, sizeof(t), "%d 分钟", m);
    }
    if (!b.started) {
        strlcpy(buf, p.battery_pct < 0 ? "没有电池" : "拔掉 USB 开始测量。", sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), b.running ? "已用电池 %s" : "上次用电池 %s", t);
    }
    set_text(s_batt_status, buf);

    if (p.battery_pct < 0) {
        strlcpy(buf, "无", sizeof(buf));
    } else if (p.battery_mv) {
        snprintf(buf, sizeof(buf), "%s%d%%  %d.%02d V", p.charging ? LV_SYMBOL_CHARGE " " : "", p.battery_pct,
                 p.battery_mv / 1000, p.battery_mv % 1000 / 10);
    } else {
        snprintf(buf, sizeof(buf), "%s%d%%", p.charging ? LV_SYMBOL_CHARGE " " : "", p.battery_pct);
    }
    set_text(s_batt_level, buf);

    int used = b.pct_start - b.pct_now, rate10, full_h;
    if (!b.started) {
        set_text(s_batt_drain, "-");
        set_text(s_batt_full, "-");
    } else if (muse_battery_drain(&b, &rate10, &full_h)) {
        snprintf(buf, sizeof(buf), "%d%%, %d.%d%%/h", used, rate10 / 10, rate10 % 10);
        set_text(s_batt_drain, buf);
        snprintf(buf, sizeof(buf), "约 %d 小时", full_h);
        set_text(s_batt_full, buf);
    } else {
        snprintf(buf, sizeof(buf), "目前 %d%%", used > 0 ? used : 0);
        set_text(s_batt_drain, buf);
        set_text(s_batt_full, "测量中");
    }

    set_pm(s_batt_off, b.started ? b.screen_off_pm : -1);
    set_pm(s_batt_slept, b.started ? b.slept_pm : -1);
    set_pm(s_batt_busy, b.started ? b.busy_pm : -1);
    if (b.started && b.secs && b.slept_pm >= 0) {
        int per10 = (int)(b.sleeps * 10LL / b.secs);
        snprintf(buf, sizeof(buf), "%d.%d/s", per10 / 10, per10 % 10);
        set_text(s_batt_wakes, buf);
    } else {
        set_text(s_batt_wakes, "-");
    }
    buf[0] = '\0';
    if (b.started && b.awake[0]) {
        snprintf(buf, sizeof(buf), "另外让它醒着的：%s", b.awake);
    }
    set_text(s_batt_awake, buf);
}

/* ---------- Power ---------- */

static void on_power_off(lv_event_t *e)
{
    (void)e;
    show(s_home);
    muse_ui_show_face();
    muse_input_request_power_off();
}

static void build_power_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_power = page(tile, "关机", true, &list);
    note(list, "要完全关机吗？");
    button(list, LV_SYMBOL_POWER "  关机", COLOR_DANGER, on_power_off, NULL);
    button(list, "取消", COLOR_TEXT, on_back, NULL);
    note(list, "按上面的键开机。只想熄屏，按一下下面的键。");
}

/* ---------- Avatar (Boopie) ---------- */

/* Body colours to pick from; the first is the character's own. */
static const struct {
    const char *name;
    uint32_t rgb;
} AVATAR_COLOURS[] = {
    { "默认", BOOPIE_COLOUR_DEFAULT },
    { "樱花粉", 0xff9ec8 },
    { "薄荷绿", 0x7fe3c4 },
    { "天空蓝", 0x7fb8ff },
    { "柠檬黄", 0xffd96a },
    { "薰衣草", 0xb9a2ff },
    { "蜜桃橙", 0xffb08a },
    { "珊瑚红", 0xff7a7a },
    { "奶白", 0xf4efe6 },
    /* Boopie: for the long haul, Lv 25 to 40. */
    { "流光金", 0xf2c14e },
    { "极光青", 0x5ee0c8 },
    { "星夜蓝", 0x4a5fd0 },
    { "石墨灰", 0x5a5a6a },
};
#define AVATAR_COLOUR_COUNT (int)(sizeof(AVATAR_COLOURS) / sizeof(AVATAR_COLOURS[0]))

static lv_obj_t *s_avatar_checks[BOOPIE_AVATAR_COUNT];
static lv_obj_t *s_avatar_big;   /* the chosen one's head, big */
static int s_avatar_big_for;
static lv_obj_t *s_colour_checks[AVATAR_COLOUR_COUNT];
static lv_obj_t *s_colour_box, *s_colour_default_swatch;
static lv_obj_t *s_scene_checks[BOOPIE_SCENE_COUNT];
static lv_obj_t *s_pet_line;
#define SKIN_ROWS_MAX 32
static lv_obj_t *s_skin_box, *s_skin_none_check, *s_skin_rows[SKIN_ROWS_MAX], *s_skin_checks[SKIN_ROWS_MAX];
static lv_obj_t *s_acc_checks[BOOPIE_ACC_COUNT];
static bool s_skin_pic[SKIN_ROWS_MAX];   /* the row has its preview: made when first shown */

/* A preview picture (RGB565, 0 round it; freed here) as a see-through image
 * at the row's start, made ARGB here rather than by boopie_icon_keyed, which
 * keeps only a few. */
static void row_picture(lv_obj_t *r, uint16_t *px, int w, int h)
{
    lv_image_dsc_t *dsc = lv_malloc(sizeof(*dsc));
    uint32_t *argb = lv_malloc((size_t)w * h * sizeof(uint32_t));
    if (!dsc || !argb) {
        lv_free(dsc);
        lv_free(argb);
        lv_free(px);
        return;
    }
    for (int i = 0; i < w * h; i++) {
        uint16_t c = px[i];
        uint32_t cr = (c >> 11) * 255 / 31, cg = (c >> 5 & 63) * 255 / 63, cb = (c & 31) * 255 / 31;
        argb[i] = c ? 0xff000000u | cr << 16 | cg << 8 | cb : 0;
    }
    lv_free(px);
    *dsc = (lv_image_dsc_t){
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, .w = w, .h = h,
                    .stride = w * sizeof(uint32_t) },
        .data_size = (uint32_t)(w * h * sizeof(uint32_t)),
        .data = (const uint8_t *)argb,
    };
    lv_obj_t *img = lv_image_create(r);
    lv_image_set_src(img, dsc);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(img, 0);
}

/* Boopie: a skin row's preview, its character's head as it would look wearing it. */
static void skin_picture(int i)
{
    if (s_skin_pic[i]) {
        return;
    }
    s_skin_pic[i] = true;
    int w = BOOPIE_HEAD_W * 3, h = BOOPIE_PET_H * 3;
    uint16_t *px = lv_malloc((size_t)w * h * sizeof(uint16_t));
    if (px) {
        boopie_pixel_skin_head(i, px, 3);
        row_picture(s_skin_rows[i], px, w, h);
    }
}

static void acc_picture(lv_obj_t *r, boopie_acc_t a)
{
    int w = BOOPIE_ACC_ICON_W * 3, h = BOOPIE_ACC_ICON_H * 3;
    uint16_t *px = lv_malloc((size_t)w * h * sizeof(uint16_t));
    if (px) {
        boopie_pixel_acc_icon(a, px, 3);
        row_picture(r, px, w, h);
    }
}
static int s_avatar_shown = -1;
static lv_obj_t *s_pet_name;   /* the name row's value */

static void pet_named(const char *text, bool done)
{
    const char *error = NULL;
    if (done && !boopie_avatar_set_pet_name(text, &error)) {
        muse_state_set_caption("这个名字用不了");
    }
}

/* The name row: the keypad, with the name as it is (empty: the character's own). */
static void on_pet_name(lv_event_t *e)
{
    (void)e;
    boopie_input_open("给它起个名字", boopie_avatar_has_own_name() ? boopie_avatar_pet_name() : "", "留空就叫角色名",
                      BOOPIE_PET_NAME_CHARS, pet_named);
}

static void on_avatar_choice(lv_event_t *e)
{
    boopie_avatar_select((int)(intptr_t)lv_event_get_user_data(e));
}

static int s_skin_short = -1;      /* a skin tapped without stars enough: says so a moment */
static uint32_t s_skin_short_at;

/* A skin row: wear it if owned, else buy it (if there are stars enough). */
static void on_skin_choice(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    const char *error = NULL;
    if (i < 0 || boopie_avatar_owns(i)) {
        boopie_avatar_wear(i, &error);
    } else if (!boopie_avatar_buy(i, &error) && boopie_avatar_skin_goal(i) == BOOPIE_GOAL_NONE) {
        s_skin_short = i;
        s_skin_short_at = lv_tick_get();
    }
}

/* An accessory row: put it on, or take it off. */
static void on_acc_choice(lv_event_t *e)
{
    boopie_acc_t a = (boopie_acc_t)(intptr_t)lv_event_get_user_data(e);
    const char *error = NULL;
    boopie_avatar_set_accessory(a, !(boopie_avatar_accessories() & BOOPIE_ACC_BIT(a)), &error);
}

static void on_scene_choice(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i == 0 || boopie_avatar_unlocked(BOOPIE_UNLOCK_SCENE, i, NULL)) {
        boopie_avatar_set_scene((boopie_scene_t)i);
    }
}

/* What a row shows on the right: a tick, the level it unlocks at, or nothing. */
static void lock_or_tick(lv_obj_t *l, bool chosen, boopie_unlock_kind_t kind, int index)
{
    int level;
    boopie_goal_t goal = kind == BOOPIE_UNLOCK_ACCESSORY ? boopie_avatar_acc_goal((boopie_acc_t)index) : BOOPIE_GOAL_NONE;
    if (goal != BOOPIE_GOAL_NONE && !boopie_avatar_unlocked(kind, index, NULL)) {   /* earned: how far to go */
        char buf[64];
        boopie_avatar_goal_text(goal, buf, sizeof buf);
        set_text(l, buf);
        lv_obj_set_style_text_color(l, lv_color_hex(COLOR_DIM), 0);
        return;
    }
    bool from_start = kind != BOOPIE_UNLOCK_ACCESSORY && (index == 0 || (kind == BOOPIE_UNLOCK_COLOUR && index == 1));
    if (!from_start && !boopie_avatar_unlocked(kind, index, &level)) {
        char buf[16];
        snprintf(buf, sizeof buf, "Lv %d", level);
        set_text(l, buf);
        lv_obj_set_style_text_color(l, lv_color_hex(COLOR_DIM), 0);
        return;
    }
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_ACCENT), 0);
    set_text(l, chosen ? LV_SYMBOL_OK : "");
}

static void on_colour_choice(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i <= 1 || boopie_avatar_unlocked(BOOPIE_UNLOCK_COLOUR, i, NULL)) {
        boopie_avatar_set_colour(AVATAR_COLOURS[i].rgb);
    }
}

static lv_obj_t *swatch(lv_obj_t *row_obj, uint32_t rgb)
{
    lv_obj_t *sw = lv_obj_create(row_obj);
    lv_obj_remove_style_all(sw);
    lv_obj_set_size(sw, 22, 22);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(rgb), 0);
    lv_obj_move_to_index(sw, 0);
    return sw;
}

static void build_avatar_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_avatar = page(tile, "伙伴", true, &list);
    /* The one chosen, big, over its name and level. */
    s_avatar_big = lv_image_create(list);
    s_avatar_big_for = -1;
    s_pet_line = note(list, "");
    row(list, NULL, "名字", &s_pet_name, on_pet_name, NULL);
    for (int i = 0; i < BOOPIE_AVATAR_COUNT; i++) {
        lv_obj_t *r = row(list, NULL, boopie_avatar_name(i), &s_avatar_checks[i], on_avatar_choice, (void *)(intptr_t)i);
        lv_obj_set_style_text_color(s_avatar_checks[i], lv_color_hex(COLOR_ACCENT), 0);
        const lv_image_dsc_t *head = boopie_head(i, 3);
        if (head) {
            lv_obj_t *img = lv_image_create(r);
            lv_image_set_src(img, head);
            lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_move_to_index(img, 0);
        }
    }
    /* The colours, hidden for Muse's own character. */
    s_colour_box = column(list);
    note(s_colour_box, "颜色");
    for (int i = 0; i < AVATAR_COLOUR_COUNT; i++) {
        lv_obj_t *r = row(s_colour_box, NULL, AVATAR_COLOURS[i].name, &s_colour_checks[i], on_colour_choice,
                          (void *)(intptr_t)i);
        lv_obj_set_style_text_color(s_colour_checks[i], lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_t *sw = swatch(r, AVATAR_COLOURS[i].rgb == BOOPIE_COLOUR_DEFAULT ? 0 : AVATAR_COLOURS[i].rgb);
        if (i == 0) {
            s_colour_default_swatch = sw;
        }
    }
    s_skin_box = column(list);
    note(s_skin_box, "皮肤");
    row(s_skin_box, NULL, "原样", &s_skin_none_check, on_skin_choice, (void *)(intptr_t)-1);
    for (int i = 0; i < boopie_skin_count() && i < SKIN_ROWS_MAX; i++) {
        char name[48];
        snprintf(name, sizeof name, "%s%s", boopie_skin_name(i),
                 boopie_avatar_skin_goal(i) != BOOPIE_GOAL_NONE ? "（成就）" : boopie_skin_limited(i) ? "（限定）"
                 : boopie_skin_collector(i) ? "（典藏）" : "");
        s_skin_rows[i] = row(s_skin_box, NULL, name, &s_skin_checks[i], on_skin_choice, (void *)(intptr_t)i);
    }
    note(list, "配饰");
    for (int i = 0; i < BOOPIE_ACC_COUNT; i++) {
        acc_picture(row(list, NULL, boopie_acc_name((boopie_acc_t)i), &s_acc_checks[i], on_acc_choice, (void *)(intptr_t)i),
                    (boopie_acc_t)i);
    }
    note(list, "背景");
    for (int i = 0; i < BOOPIE_SCENE_COUNT; i++) {
        row(list, NULL, boopie_scene_name((boopie_scene_t)i), &s_scene_checks[i], on_scene_choice, (void *)(intptr_t)i);
        lv_obj_set_style_text_color(s_scene_checks[i], lv_color_hex(COLOR_ACCENT), 0);
    }
    note(list, "品牌形象仅供个人使用。");
}

static void tick_avatar(void)
{
    int cur = boopie_avatar_current();
    for (int i = 0; i < BOOPIE_AVATAR_COUNT; i++) {
        set_text(s_avatar_checks[i], i == cur ? LV_SYMBOL_OK : "");
    }
    if (cur != s_avatar_big_for) {
        const lv_image_dsc_t *big = boopie_head(cur, 7);
        if (big) {
            lv_image_set_src(s_avatar_big, big);
        }
        s_avatar_big_for = cur;
    }
    boopie_pet_status_t pet;
    boopie_avatar_pet_status(&pet);
    char line[96];
    snprintf(line, sizeof line, "%s    Lv %d    %u/%u    ★ %u", boopie_avatar_pet_name(), pet.level,
             (unsigned)pet.xp_into, (unsigned)pet.xp_need, (unsigned)pet.stars);
    set_text(s_pet_line, line);
    set_text(s_pet_name, boopie_avatar_pet_name());
    /* Skins: this character's only. */
    bool any = false;
    int worn = boopie_avatar_skin();
    for (int i = 0; i < boopie_skin_count() && i < SKIN_ROWS_MAX; i++) {
        bool mine = boopie_avatar_of_skin(i) == cur;
        any |= mine;
        if (mine == lv_obj_has_flag(s_skin_rows[i], LV_OBJ_FLAG_HIDDEN)) {
            if (mine) {
                lv_obj_remove_flag(s_skin_rows[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_skin_rows[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (!mine) {
            continue;
        }
        skin_picture(i);
        char v[64];
        boopie_goal_t goal = boopie_avatar_skin_goal(i);
        if (i == worn) {
            snprintf(v, sizeof v, "%s", LV_SYMBOL_OK);
        } else if (boopie_avatar_owns(i)) {
            v[0] = '\0';
        } else if (i == s_skin_short && lv_tick_elaps(s_skin_short_at) < 2500) {
            snprintf(v, sizeof v, "%s", boopie_avatar_skin_on_sale(i) ? "星星不够" : "节日才能买");
        } else if (goal != BOOPIE_GOAL_NONE) {
            if (boopie_avatar_goal(goal, NULL, NULL)) {
                snprintf(v, sizeof v, "免费领取");
            } else {
                boopie_avatar_goal_text(goal, v, sizeof v);
            }
        } else if (!boopie_avatar_skin_on_sale(i)) {
            snprintf(v, sizeof v, "%s", boopie_avatar_skin_when(i));
        } else {
            snprintf(v, sizeof v, "★ %d", boopie_skin_price(i));
        }
        set_text(s_skin_checks[i], v);
        lv_obj_set_style_text_color(s_skin_checks[i], lv_color_hex(
            i == worn || pet.stars >= (uint32_t)boopie_skin_price(i) || boopie_avatar_owns(i) ? COLOR_ACCENT : COLOR_DIM), 0);
    }
    set_text(s_skin_none_check, worn < 0 ? LV_SYMBOL_OK : "");
    lv_obj_set_style_text_color(s_skin_none_check, lv_color_hex(COLOR_ACCENT), 0);
    if (any == lv_obj_has_flag(s_skin_box, LV_OBJ_FLAG_HIDDEN)) {
        if (any) {
            lv_obj_remove_flag(s_skin_box, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_skin_box, LV_OBJ_FLAG_HIDDEN);
        }
    }

    uint32_t worn_acc = boopie_avatar_accessories();
    for (int i = 0; i < BOOPIE_ACC_COUNT; i++) {
        lock_or_tick(s_acc_checks[i], worn_acc & BOOPIE_ACC_BIT(i), BOOPIE_UNLOCK_ACCESSORY, i);
    }
    boopie_scene_t scene = boopie_avatar_scene();
    for (int i = 0; i < BOOPIE_SCENE_COUNT; i++) {
        lock_or_tick(s_scene_checks[i], i == (int)scene, BOOPIE_UNLOCK_SCENE, i);
    }
    bool colours = boopie_avatar_recolourable(cur);
    if (colours == lv_obj_has_flag(s_colour_box, LV_OBJ_FLAG_HIDDEN)) {
        if (colours) {
            lv_obj_remove_flag(s_colour_box, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_colour_box, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!colours) {
        return;
    }
    if (cur != s_avatar_shown) {
        s_avatar_shown = cur;
        lv_obj_set_style_bg_color(s_colour_default_swatch,
                                  lv_color_hex(boopie_char_default_colour((boopie_char_t)(cur - 1))), 0);
    }
    uint32_t c = boopie_avatar_colour();
    for (int i = 0; i < AVATAR_COLOUR_COUNT; i++) {
        lock_or_tick(s_colour_checks[i], AVATAR_COLOURS[i].rgb == c, BOOPIE_UNLOCK_COLOUR, i);
    }
}

/* ---------- Home ---------- */

static const page_t WIFI = { &s_wifi, build_wifi_page };
static const page_t HATCH = { &s_hatch, build_hatch_page };
static const page_t BLE = { &s_ble, build_ble_page };
static const page_t SOUND = { &s_sound, build_sound_page };
static const page_t SLEEP = { &s_sleep, build_sleep_page };
static const page_t BATTERY = { &s_battery, build_battery_page };
static const page_t POWER = { &s_power, build_power_page };
static const page_t AVATAR = { &s_avatar, build_avatar_page };   /* Boopie */

/* ---------- Brain (Boopie) ---------- */

static lv_obj_t *s_brain, *s_home_brain, *s_brain_checks[BOOPIE_BRAIN_COUNT], *s_xiaozhi;
static lv_obj_t *s_xz_code, *s_xz_note;

static void on_brain_choice(lv_event_t *e)
{
    boopie_avatar_choose_brain((boopie_brain_t)(intptr_t)lv_event_get_user_data(e));   /* restarts on a change */
}

static void on_xz_recheck(lv_event_t *e)
{
    (void)e;
    boopie_xiaozhi_recheck();
}

/* Two taps within a few seconds: a fresh activation code, the old binding forgotten. */
static lv_obj_t *s_xz_rebind_lbl;
static int64_t s_xz_rebind_armed_us;

static void on_xz_rebind(lv_event_t *e)
{
    (void)e;
    int64_t now = esp_timer_get_time();
    if (s_xz_rebind_armed_us && now - s_xz_rebind_armed_us < 5000000) {
        s_xz_rebind_armed_us = 0;
        set_text(s_xz_rebind_lbl, "重新绑定（换新激活码）");
        boopie_xiaozhi_rebind();
        return;
    }
    s_xz_rebind_armed_us = now;
    set_text(s_xz_rebind_lbl, "再点一下确认");
}

static void build_xiaozhi_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_xiaozhi = page(tile, "小智", true, &list);
    s_xz_code = note(list, "");
    lv_obj_set_style_text_font(s_xz_code, boopie_font_with_cjk(&lv_font_montserrat_28), 0);
    lv_obj_set_style_text_color(s_xz_code, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_letter_space(s_xz_code, 6, 0);
    s_xz_note = note(list, "");
    button(list, LV_SYMBOL_REFRESH "  重新连接", COLOR_ACCENT, on_xz_recheck, NULL);
    button(list, "重新绑定（换新激活码）", COLOR_DANGER, on_xz_rebind, &s_xz_rebind_lbl);
    note(list, "还是没出激活码：先在 xiaozhi.me 控制台\n删掉这台设备，再点重新绑定。");
    s_xz_rebind_armed_us = 0;
    static const char *const HOWTO[] = {
        "在 AI 助手 里选\"小智\"，连上网",
        "这里会显示 6 位激活码",
        "手机打开 xiaozhi.me 登录",
        "点\"添加设备\"，输入激活码",
    };
    steps(list, "怎样绑定小智", HOWTO, (int)(sizeof HOWTO / sizeof HOWTO[0]));
    note(list, "绑定一次就好。\n角色、音色在控制台里改。");
}

static void tick_xiaozhi(void)
{
    char code[16], said[96];
    boopie_xz_state_t st = boopie_xiaozhi_status(code, sizeof code, said, sizeof said);
    set_text(s_xz_code, st == BOOPIE_XZ_CODE ? code : st == BOOPIE_XZ_READY ? LV_SYMBOL_OK : "");
    set_text(s_xz_note, st == BOOPIE_XZ_OFF ? "现在用的是 Muse。\n在 AI 助手 里选小智\n就开始连接。" : said);
    if (s_xz_rebind_armed_us && esp_timer_get_time() - s_xz_rebind_armed_us >= 5000000) {
        s_xz_rebind_armed_us = 0;
        set_text(s_xz_rebind_lbl, "重新绑定（换新激活码）");
    }
}

static const page_t XIAOZHI = { &s_xiaozhi, build_xiaozhi_page };

static void build_brain_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_brain = page(tile, "AI 助手", true, &list);
    note(list, "说话时用哪个 AI 回答？");
    /* Muse first: it's the one recommended; 小智 is there when the network won't do. */
    static const int ORDER[BOOPIE_BRAIN_COUNT] = { BOOPIE_BRAIN_MUSE, BOOPIE_BRAIN_XIAOZHI };
    static const char *const NAMES[BOOPIE_BRAIN_COUNT] = { [BOOPIE_BRAIN_MUSE] = "Muse  推荐",
                                                           [BOOPIE_BRAIN_XIAOZHI] = "小智  备用" };
    for (int k = 0; k < BOOPIE_BRAIN_COUNT; k++) {
        int i = ORDER[k];
        row(list, NULL, NAMES[i], &s_brain_checks[i], on_brain_choice, (void *)(intptr_t)i);
        lv_obj_set_style_text_color(s_brain_checks[i], lv_color_hex(COLOR_ACCENT), 0);
    }
    note(list, "Muse：要海外网络（开 VPN）\n和 Muse App 配对。\n小智：国内网络，不用 VPN，\n在 xiaozhi.me 绑定一次。\n换一个会自动重启：只加载选中的那个。");
    nav_row(list, "Muse 接入与设置", on_nav, (void *)&HATCH);
    nav_row(list, "小智接入", on_nav, (void *)&XIAOZHI);
}

static void tick_brain(void)
{
    boopie_brain_t cur = boopie_avatar_brain();
    for (int i = 0; i < BOOPIE_BRAIN_COUNT; i++) {
        set_text(s_brain_checks[i], (int)cur == i ? LV_SYMBOL_OK : "");
    }
}

static const page_t BRAIN = { &s_brain, build_brain_page };

/* ---------- VPN (Boopie) ---------- */

static lv_obj_t *s_vpn, *s_home_vpn, *s_vpn_sw, *s_vpn_status, *s_vpn_msg, *s_vpn_list;
static uint32_t s_vpn_shown;   /* what the node list shows, to redraw only on change */

static void on_vpn_sw(lv_event_t *e)
{
    boopie_vpn_set_on(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void on_vpn_update(lv_event_t *e)
{
    (void)e;
    boopie_vpn_update();
}

/* Imported on the phone: the setup page turns VPN on and fetches the nodes. */
static void on_vpn_phone(lv_event_t *e)
{
    (void)e;
    boopie_setup_open(NULL);
}

static void on_vpn_test(lv_event_t *e)
{
    (void)e;
    boopie_vpn_test();
}

static void on_vpn_node(lv_event_t *e)
{
    boopie_vpn_select((int)(intptr_t)lv_event_get_user_data(e));
    s_vpn_shown = 0;   /* redraw the ticks */
}

static void build_vpn_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_vpn = page(tile, "VPN", true, &list);
    s_vpn_sw = switch_row(list, "VPN", boopie_vpn_on(), on_vpn_sw);
    s_vpn_status = note(list, "");
    button(list, LV_SYMBOL_IMAGE "  手机扫码导入订阅", COLOR_ACCENT, on_vpn_phone, NULL);
    button(list, LV_SYMBOL_REFRESH "  更新订阅", COLOR_ACCENT, on_vpn_update, NULL);
    button(list, LV_SYMBOL_LOOP "  测速", COLOR_ACCENT, on_vpn_test, NULL);
    s_vpn_msg = note(list, "");
    note(list, "节点");
    s_vpn_list = column(list);
    note(list, "支持 Shadowsocks（ss）节点：在机场后台复制订阅时，选「通用 / V2rayN」或「Clash」都可以；"
               "vmess、vless、trojan 节点用不了。存好就自动开 VPN、更新并测速，选一个能连上的节点。"
               "订阅开头那几条「剩余流量」「到期时间」只是信息，不是节点。只有 Muse 和系统更新走 VPN，小智、校时直连。");
    s_vpn_shown = 0;
}

static void tick_vpn(void)
{
    bool on = boopie_vpn_on();
    if (on != lv_obj_has_state(s_vpn_sw, LV_STATE_CHECKED)) {
        lv_obj_set_state(s_vpn_sw, LV_STATE_CHECKED, on);
    }
    int n = boopie_vpn_count(), cur = boopie_vpn_current();
    boopie_vpn_node_t node;
    char buf[96];
    if (!on) {
        strlcpy(buf, "已关", sizeof buf);
    } else if (cur >= 0 && boopie_vpn_node(cur, &node) && boopie_vpn_trouble()) {
        snprintf(buf, sizeof buf, "已开：%s\n%s，点「测速」换个能连上的", node.name, boopie_vpn_trouble());
    } else if (cur >= 0 && boopie_vpn_node(cur, &node)) {
        snprintf(buf, sizeof buf, "已开：%s%s", node.name, boopie_vpn_active() ? "\nMuse 正在走 VPN" : "");
    } else {
        strlcpy(buf, n ? "已开：先选一个节点" : "已开：还没有节点，先导入订阅", sizeof buf);
    }
    set_text(s_vpn_status, buf);
    lv_obj_set_style_text_color(s_vpn_status, lv_color_hex(on && boopie_vpn_trouble() ? COLOR_WARN
                                                           : on && cur >= 0 ? COLOR_OK : COLOR_DIM), 0);
    boopie_vpn_busy(buf, sizeof buf);
    set_text(s_vpn_msg, buf);

    /* The list, redrawn when the nodes, the choice or a speed changes. */
    uint32_t sig = 2166136261u;
    sig = (sig ^ (uint32_t)n) * 16777619u;
    sig = (sig ^ (uint32_t)(cur + 1)) * 16777619u;
    for (int i = 0; i < n; i++) {
        sig = (sig ^ (uint32_t)(boopie_vpn_latency(i) + 3)) * 16777619u;
    }
    if (sig == s_vpn_shown) {
        return;
    }
    s_vpn_shown = sig;
    lv_obj_clean(s_vpn_list);
    if (!n) {
        note(s_vpn_list, "还没有节点");
        return;
    }
    for (int i = 0; i < n; i++) {
        if (!boopie_vpn_node(i, &node)) {
            continue;
        }
        lv_obj_t *value;
        lv_obj_t *r = row(s_vpn_list, i == cur ? LV_SYMBOL_OK : NULL, node.name, &value, on_vpn_node, (void *)(intptr_t)i);
        (void)r;
        int ms = boopie_vpn_latency(i);
        if (boopie_vpn_is_info(&node)) {
            strlcpy(buf, "信息", sizeof buf);   /* a line of the subscription's, not a server */
        } else if (!node.supported) {
            strlcpy(buf, "不支持", sizeof buf);
        } else if (ms == -2) {
            strlcpy(buf, "连不上", sizeof buf);
        } else if (ms >= 0) {
            snprintf(buf, sizeof buf, "%d ms", ms);
        } else {
            buf[0] = '\0';
        }
        set_text(value, buf);
        lv_obj_set_style_text_color(value, lv_color_hex(boopie_vpn_is_info(&node) ? COLOR_DIM
                                                        : !node.supported || ms == -2 ? COLOR_WARN
                                                        : ms >= 0 && ms < 300 ? COLOR_OK : COLOR_DIM), 0);
    }
}

static const page_t VPN = { &s_vpn, build_vpn_page };

/* ---------- 系统更新 (Boopie) ---------- */

static lv_obj_t *s_update, *s_home_update, *s_upd_status, *s_upd_check, *s_upd_box, *s_upd_title, *s_upd_notes,
                *s_upd_bar, *s_upd_go, *s_upd_go_lbl, *s_upd_kind, *s_upd_switch, *s_upd_switch_lbl, *s_upd_switch_note;
static uint32_t s_upd_armed;   /* the first tap on 立即更新, waiting for the second */
static uint32_t s_upd_switch_armed;   /* the same for 换成…版 */

static void on_upd_check(lv_event_t *e)
{
    (void)e;
    boopie_ota_check();
}

/* Asked twice: the first tap says what will happen, the second starts it. */
static void on_upd_go(lv_event_t *e)
{
    (void)e;
    if (s_upd_armed && lv_tick_elaps(s_upd_armed) < 6000) {
        s_upd_armed = 0;
        boopie_ota_install();
    } else {
        s_upd_armed = lv_tick_get();
    }
}

static void on_upd_switch(lv_event_t *e)
{
    (void)e;
    if (s_upd_switch_armed && lv_tick_elaps(s_upd_switch_armed) < 6000) {
        s_upd_switch_armed = 0;
        boopie_ota_switch();
    } else {
        s_upd_switch_armed = lv_tick_get();
    }
}

static void build_update_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_update = page(tile, "系统更新", true, &list);
    s_upd_kind = note(list, "");
    s_upd_status = note(list, "");
    s_upd_check = button(list, LV_SYMBOL_REFRESH "  检查更新", COLOR_ACCENT, on_upd_check, NULL);
    s_upd_box = column(list);
    lv_obj_set_style_radius(s_upd_box, 18, 0);
    lv_obj_set_style_bg_opa(s_upd_box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_upd_box, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_pad_all(s_upd_box, 14, 0);
    s_upd_title = label(s_upd_box, &lv_font_montserrat_20, COLOR_OK, "");
    lv_obj_set_width(s_upd_title, lv_pct(100));
    lv_obj_set_style_text_align(s_upd_title, LV_TEXT_ALIGN_CENTER, 0);
    s_upd_notes = label(s_upd_box, &lv_font_montserrat_16, COLOR_TEXT, "");
    lv_obj_set_width(s_upd_notes, lv_pct(100));
    lv_label_set_long_mode(s_upd_notes, LV_LABEL_LONG_MODE_WRAP);
    s_upd_bar = lv_bar_create(s_upd_box);
    lv_obj_set_size(s_upd_bar, lv_pct(100), 14);
    lv_bar_set_range(s_upd_bar, 0, 100);
    lv_obj_set_style_bg_color(s_upd_bar, lv_color_hex(0x2a2345), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_upd_bar, lv_color_hex(COLOR_OK), LV_PART_INDICATOR);
    s_upd_go = button(s_upd_box, LV_SYMBOL_DOWNLOAD "  立即更新", COLOR_OK, on_upd_go, &s_upd_go_lbl);
    lv_obj_add_flag(s_upd_box, LV_OBJ_FLAG_HIDDEN);
    s_upd_switch = button(list, "", COLOR_ACCENT, on_upd_switch, &s_upd_switch_lbl);
    s_upd_switch_note = note(list, "");
    note(list, "每天自动检查一次，有新版本会让宠物告诉你，点了「立即更新」才会装。"
               "更新包带签名，只装 Boopie 自己发布的版本；VPN 开着时走 VPN 下载。"
               "更新时别断电，装好会自动重启；新版本跑不起来会自动回到旧版本。");
    s_upd_armed = s_upd_switch_armed = 0;
}

static void show_if(lv_obj_t *o, bool show)
{
    if (show == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        if (show) {
            lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void tick_update(void)
{
    static boopie_ota_info_t info;   /* big (the notes): not on the stack */
    boopie_ota_info(&info);
    bool busy = info.state == BOOPIE_OTA_DOWNLOADING || info.state == BOOPIE_OTA_RESTARTING;
    bool found = info.version[0] && (info.state == BOOPIE_OTA_FOUND || info.state == BOOPIE_OTA_FAILED || busy);
    set_text(s_upd_status, info.state == BOOPIE_OTA_IDLE ? "还没检查过" : info.msg);
    lv_obj_set_style_text_color(s_upd_status, lv_color_hex(info.state == BOOPIE_OTA_FAILED ? COLOR_WARN
                                                           : info.state == BOOPIE_OTA_LATEST ? COLOR_OK : COLOR_DIM), 0);
    show_if(s_upd_check, info.state != BOOPIE_OTA_OFF && !busy);
    char kind[64];
    snprintf(kind, sizeof kind, "当前版本  %s  %s", esp_app_get_description()->version,
             info.unlocked ? "解锁版" : "正常版");
    set_text(s_upd_kind, kind);
    /* 正常版 <-> 解锁版: the other build of the latest version. */
    bool can_switch = info.can_switch && !busy && info.state != BOOPIE_OTA_OFF;
    show_if(s_upd_switch, can_switch);
    show_if(s_upd_switch_note, can_switch);
    if (can_switch) {
        bool armed = s_upd_switch_armed && lv_tick_elaps(s_upd_switch_armed) < 6000;
        set_text(s_upd_switch_lbl, armed ? "再点一下开始（几分钟，别断电）"
                                   : info.unlocked ? LV_SYMBOL_LOOP "  换成正常版" : LV_SYMBOL_LOOP "  换成解锁版");
        set_text(s_upd_switch_note, info.unlocked
                 ? "正常版：皮肤、配饰、背景要用星星买或升级解锁。用星星买过的都还在；没买过、正穿着的会换下来。"
                 : "解锁版：所有皮肤、配饰、颜色和背景直接能用。宠物、星星、等级和聊天记录都不变。想换回来随时可以。");
    }
    show_if(s_upd_box, found);
    if (!found) {
        return;
    }
    char t[48];
    snprintf(t, sizeof t, "新版本 %s", info.version);
    set_text(s_upd_title, t);
    set_text(s_upd_notes, info.notes[0] ? info.notes : "（这次没写更新说明）");
    show_if(s_upd_bar, busy);
    lv_bar_set_value(s_upd_bar, info.percent, LV_ANIM_OFF);
    show_if(s_upd_go, !busy);
    bool armed = s_upd_armed && lv_tick_elaps(s_upd_armed) < 6000;
    set_text(s_upd_go_lbl, armed ? "再点一下开始（几分钟，别断电）"
                         : info.state == BOOPIE_OTA_FAILED ? LV_SYMBOL_REFRESH "  重试更新" : LV_SYMBOL_DOWNLOAD "  立即更新");
}

static const page_t UPDATE = { &s_update, build_update_page };

/* ---------- 关于 (Boopie) ---------- */

/* Who made it: shown on the 关于 page. */
static const struct {
    const char *what, *value;
} ABOUT_AUTHOR[] = {
    { "作者", "wenjinliuu" },
    { "项目", "github.com/wenjinliuu/boopie-firmware" },
};

static lv_obj_t *s_about_page;

static lv_obj_t *about_card(lv_obj_t *list, const char *heading, const char *body)
{
    lv_obj_t *c = column(list);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_set_style_pad_row(c, 6, 0);
    label(c, &lv_font_montserrat_20, COLOR_ACCENT, heading);
    lv_obj_t *l = label(c, &lv_font_montserrat_16, COLOR_TEXT, body);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return c;
}

static void build_about_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_about_page = page(tile, "关于", true, &list);
    lv_obj_t *head = lv_image_create(list);
    const lv_image_dsc_t *big = boopie_head(boopie_avatar_current(), 7);
    if (big) {
        lv_image_set_src(head, big);
    }
    const esp_app_desc_t *app = esp_app_get_description();
    char line[96];
    snprintf(line, sizeof line, "Boopie  %s", app->version);
    lv_obj_t *t = label(list, &lv_font_montserrat_20, COLOR_TEXT, line);
    (void)t;
    about_card(list, "Boopie 是什么",
               "一只住在圆形小屏里的电子宠物，也是会聊天的 AI 小伙伴。"
               "它会饿、会困、会长大，有自己的小世界、小游戏和一柜子皮肤；"
               "按住说话，就能找 Muse 或小智聊天。");
    about_card(list, "怎么做出来的",
               "跑在微雪 ESP32-S3 1.75 寸圆形 AMOLED 开发板上。"
               "从 Meta 开源的 Muse Gadget SDK 改起，界面是 LVGL 画的，中文用思源黑体。"
               "个人爱好项目，品牌形象仅供个人使用。");
    /* Each on its own lines, the value under its name: a web address is long. */
    for (size_t i = 0; i < sizeof ABOUT_AUTHOR / sizeof ABOUT_AUTHOR[0]; i++) {
        lv_obj_t *c = column(list);
        lv_obj_set_style_radius(c, 18, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD), 0);
        lv_obj_set_style_pad_hor(c, 16, 0);
        lv_obj_set_style_pad_ver(c, 10, 0);
        lv_obj_set_style_pad_row(c, 2, 0);
        label(c, &lv_font_montserrat_16, COLOR_DIM, ABOUT_AUTHOR[i].what);
        lv_obj_t *v = label(c, &lv_font_montserrat_16, COLOR_ACCENT, ABOUT_AUTHOR[i].value);
        lv_obj_set_width(v, lv_pct(100));
        lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_WRAP);
    }
    snprintf(line, sizeof line, "构建于 %s %s", app->date, app->time);
    note(list, line);
    note(list, "感谢 Muse Gadget SDK（Apache-2.0）、ESP-IDF、LVGL、思源黑体（SIL OFL）。");
}

static const page_t ABOUT = { &s_about_page, build_about_page };

/* ---------- Storage (Boopie) ---------- */

static const struct {
    const char *what, *name;
    boopie_store_kind_t kind;
} STORE_ROWS[] = {
    { "chat", "聊天记录", BOOPIE_STORE_CHAT },
    { "album", "相册", BOOPIE_STORE_ALBUM },
    { "notes", "断网留言", BOOPIE_STORE_NOTES },
};
#define STORE_ROWS_N (int)(sizeof STORE_ROWS / sizeof STORE_ROWS[0])

static lv_obj_t *s_storage, *s_home_storage, *s_store_total, *s_store_bar, *s_store_vals[STORE_ROWS_N];
static int64_t s_store_armed_us[STORE_ROWS_N];
static int64_t s_store_shown_us;

static void on_store_row(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    int64_t now = esp_timer_get_time();
    if (s_store_armed_us[i] && now - s_store_armed_us[i] < 5000000) {
        s_store_armed_us[i] = 0;
        boopie_viewer_clear(STORE_ROWS[i].what);
    } else {
        s_store_armed_us[i] = now;   /* the second tap clears */
    }
    s_store_shown_us = 0;
}

static void build_storage_page(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_storage = page(tile, "存储空间", true, &list);
    s_store_total = note(list, "");
    s_store_bar = lv_bar_create(list);
    lv_obj_set_size(s_store_bar, lv_pct(94), 12);
    lv_obj_set_style_bg_color(s_store_bar, lv_color_hex(0x2a2345), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_store_bar, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    for (int i = 0; i < STORE_ROWS_N; i++) {
        row(list, NULL, STORE_ROWS[i].name, &s_store_vals[i], on_store_row, (void *)(intptr_t)i);
        s_store_armed_us[i] = 0;
    }
    note(list, "点一行，再点一下就清空它。\n满了自动删最旧的：聊天记录 100 条，\n相册 10 张，"
               "留言发出或过 7 天就删。");
    s_store_shown_us = 0;
}

static void kb(char *buf, size_t cap, size_t bytes)
{
    if (bytes >= 1024 * 1024) {
        snprintf(buf, cap, "%u.%u MB", (unsigned)(bytes >> 20), (unsigned)((bytes & 0xFFFFF) * 10 >> 20));
    } else {
        snprintf(buf, cap, "%u KB", (unsigned)((bytes + 1023) / 1024));
    }
}

static void tick_storage(void)
{
    int64_t now = esp_timer_get_time();
    if (s_store_shown_us && now - s_store_shown_us < 2000000) {
        return;   /* sizes walk the folders: every couple of seconds is plenty */
    }
    s_store_shown_us = now;
    size_t used = boopie_store_used(), total = boopie_store_total();
    char a[16], b[16], buf[64];
    kb(a, sizeof a, used);
    kb(b, sizeof b, total);
    snprintf(buf, sizeof buf, total ? "已用 %s，共 %s" : "用户数据区不可用", a, b);
    set_text(s_store_total, buf);
    lv_bar_set_range(s_store_bar, 0, 1000);
    lv_bar_set_value(s_store_bar, total ? (int32_t)((uint64_t)used * 1000 / total) : 0, LV_ANIM_OFF);
    int counts[STORE_ROWS_N] = { boopie_chat_count(), -1, -1 };
    char paths[BOOPIE_ALBUM_MAX][BOOPIE_ALBUM_PATH_MAX];
    counts[1] = boopie_album_list(paths, BOOPIE_ALBUM_MAX);
    char names[8][BOOPIE_NOTE_NAME_MAX];
    counts[2] = boopie_notes_list(names, 8);
    static const char *const UNITS[STORE_ROWS_N] = { "条", "张", "条" };
    for (int i = 0; i < STORE_ROWS_N; i++) {
        if (s_store_armed_us[i] && now - s_store_armed_us[i] < 5000000) {
            set_text(s_store_vals[i], "再点一下清空");
            lv_obj_set_style_text_color(s_store_vals[i], lv_color_hex(COLOR_DANGER), 0);
            s_store_shown_us = 0;   /* back to the size once it's disarmed */
            continue;
        }
        s_store_armed_us[i] = 0;
        kb(a, sizeof a, boopie_store_kind_bytes(STORE_ROWS[i].kind));
        snprintf(buf, sizeof buf, "%d %s   %s", counts[i], UNITS[i], a);
        set_text(s_store_vals[i], buf);
        lv_obj_set_style_text_color(s_store_vals[i], lv_color_hex(COLOR_DIM), 0);
    }
}

static const page_t STORAGE = { &s_storage, build_storage_page };

/* Boopie: the setup guide, from the top. */
static void on_guide(lv_event_t *e)
{
    (void)e;
    boopie_guide_start();
}

/* Boopie: phone setup over the board's hotspot. */
static void on_phone_setup(lv_event_t *e)
{
    (void)e;
    boopie_setup_open(NULL);
}

static void build_home(lv_obj_t *tile)
{
    lv_obj_t *list;
    s_home = page(tile, "设置", false, &list);
    /* Boopie: pixel icons on coloured tiles, as the apps page's. */
    /* Boopie: what's set up most first: the pet, getting online, the phone, the guide. */
    icon_row(list, BOOPIE_ICON_PAW, "伙伴", &s_home_avatar, on_nav, (void *)&AVATAR);
    icon_row(list, BOOPIE_ICON_WIFI, "无线网络", &s_home_wifi, on_nav, (void *)&WIFI);
    icon_row(list, BOOPIE_ICON_BLUETOOTH, "蓝牙", &s_home_ble, on_nav, (void *)&BLE);
    icon_row(list, BOOPIE_ICON_VPN, "VPN", &s_home_vpn, on_nav, (void *)&VPN);
    icon_row(list, BOOPIE_ICON_PHONE, "手机扫码设置", NULL, on_phone_setup, NULL);
    icon_row(list, BOOPIE_ICON_GUIDE, "新手引导", NULL, on_guide, NULL);   /* Boopie: the setup guide again */
    icon_row(list, BOOPIE_ICON_BRAIN, "AI 助手", &s_home_brain, on_nav, (void *)&BRAIN);
    icon_row(list, BOOPIE_ICON_SOUND, "声音", &s_home_sound, on_nav, (void *)&SOUND);
    icon_row(list, BOOPIE_ICON_DISPLAY, "显示与熄屏", &s_home_sleep, on_nav, (void *)&SLEEP);
    icon_row(list, BOOPIE_ICON_BATTERY, "电池", &s_home_battery, on_nav, (void *)&BATTERY);
    icon_row(list, BOOPIE_ICON_STORAGE, "存储空间", &s_home_storage, on_nav, (void *)&STORAGE);
    icon_row(list, BOOPIE_ICON_UPDATE, "系统更新", &s_home_update, on_nav, (void *)&UPDATE);
    icon_row(list, BOOPIE_ICON_INFO, "关于", NULL, on_nav, (void *)&ABOUT);
    /* Boopie: no power off here; holding the bottom button opens the power menu. */
    s_about = note(list, "");
}

static void tick_home(void)
{
    muse_wifi_status_t w;
    muse_wifi_status(&w);
    static const char *const WIFI_VALUES[] = { "已关", "未设置", "连接中", "", "连不上", "不在附近" };
    set_text(s_home_wifi, w.state == MUSE_WIFI_CONNECTED ? w.ssid : WIFI_VALUES[w.state]);

    set_text(s_home_brain, boopie_avatar_brain() == BOOPIE_BRAIN_MUSE ? "Muse" : "小智");
    set_text(s_home_vpn, boopie_vpn_on() ? "已开" : "已关");
    size_t st_total = boopie_store_total();
    char st[16];
    snprintf(st, sizeof st, "已用 %u%%", st_total ? (unsigned)((uint64_t)boopie_store_used() * 100 / st_total) : 0u);
    set_text(s_home_storage, st);
    {
        static boopie_ota_info_t u;   /* big (the notes): not on the stack */
        boopie_ota_info(&u);
        set_text(s_home_update, u.state == BOOPIE_OTA_FOUND && u.version[0] ? "有新版本"
                                : u.state == BOOPIE_OTA_DOWNLOADING ? "更新中" : "");
        lv_obj_set_style_text_color(s_home_update, lv_color_hex(u.state == BOOPIE_OTA_FOUND ? COLOR_OK : COLOR_DIM), 0);
    }

    muse_ble_status_t b;
    muse_ble_status(&b);
    set_text(s_home_ble, b.state == MUSE_BLE_OFF ? "已关" : (b.state == MUSE_BLE_CONNECTED ? "已连接" : "已开"));

    if (muse_settings_speaker_on()) {
        set_val(s_home_sound, "音量 %d%%", muse_settings_volume());
    } else {
        set_text(s_home_sound, "静音");
    }
    set_text(s_home_sleep, sleep_name(muse_settings_sleep_s()));
    set_text(s_home_avatar, boopie_avatar_name(boopie_avatar_current()));   /* Boopie */

    muse_power_t p = muse_state_power();
    char buf[96];
    if (p.battery_pct < 0) {
        strlcpy(buf, "USB", sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%s%d%%", p.charging ? LV_SYMBOL_CHARGE " " : "", p.battery_pct);
    }
    set_text(s_home_battery, buf);

    snprintf(buf, sizeof(buf), "Boopie %s    %s", esp_app_get_description()->version,
             w.state == MUSE_WIFI_CONNECTED ? w.ip : "未联网");
    set_text(s_about, buf);
}

/* ---------- public ---------- */

void muse_settings_ui_build(lv_obj_t *tile)
{
    if (muse_board->round && muse_board->height < 466) {
        s_text_scale = muse_board->height;
    }
    s_tile = tile;
    build_home(tile);
    show(s_home);
}

void muse_settings_ui_tick(bool visible)
{
    static bool was_visible;
    if (visible != was_visible) {
        was_visible = visible;
        /* Only listen to the mic while the Sound page is actually on screen. */
        muse_voice_set_monitor(visible && s_current == s_sound);
    }
    if (!visible) {
        return;
    }
    if (s_current == s_home) {
        tick_home();
    } else if (s_current == s_wifi) {
        tick_wifi();
    } else if (s_current == s_hatch) {
        tick_hatch();
    } else if (s_current == s_ble) {
        tick_ble();
    } else if (s_current == s_sound) {
        tick_sound();
    } else if (s_current == s_sleep) {
        tick_sleep();
    } else if (s_current == s_battery) {
        tick_battery();
    } else if (s_current == s_avatar) {
        tick_avatar();   /* Boopie */
    } else if (s_current == s_brain) {
        tick_brain();    /* Boopie */
    } else if (s_current == s_vpn) {
        tick_vpn();      /* Boopie */
    } else if (s_current == s_xiaozhi) {
        tick_xiaozhi();  /* Boopie */
    } else if (s_current == s_storage) {
        tick_storage();  /* Boopie */
    } else if (s_current == s_update) {
        tick_update();   /* Boopie */
    }
}

bool muse_settings_ui_in_subpage(void)
{
    return s_current != s_home;
}

/* Boopie: the setup guide sends people to a page. */
bool muse_settings_ui_back(void)
{
    if (s_current == s_home) {
        return false;
    }
    go_back();
    return true;
}

void muse_settings_ui_open_from(const char *name, void (*leave)(void))
{
    muse_settings_ui_open(name);
    s_leave_to = s_current != s_home ? leave : NULL;
}

void muse_settings_ui_open(const char *name)
{
    s_leave_to = NULL;
    if (!name) {
        name = "home";   /* the list itself (it used to read a NULL) */
    }
    static const struct {
        const char *name;
        const page_t *page;
    } PAGES[] = { { "wifi", &WIFI }, { "muse", &HATCH }, { "avatar", &AVATAR }, { "bluetooth", &BLE },
                  { "sound", &SOUND }, { "sleep", &SLEEP }, { "battery", &BATTERY }, { "power", &POWER },
                  { "brain", &BRAIN }, { "xiaozhi", &XIAOZHI }, { "vpn", &VPN },
                  { "storage", &STORAGE }, { "update", &UPDATE }, { "about", &ABOUT } };
    if (strcmp(name, "home") == 0) {
        s_back_to = NULL;
        show(s_home);
        return;
    }
    for (size_t i = 0; i < sizeof PAGES / sizeof PAGES[0]; i++) {
        if (strcmp(PAGES[i].name, name) == 0) {
            if (!*PAGES[i].page->obj) {
                PAGES[i].page->build(s_tile);
            }
            s_back_to = NULL;
            show(*PAGES[i].page->obj);
            muse_settings_ui_tick(true);
        }
    }
}
