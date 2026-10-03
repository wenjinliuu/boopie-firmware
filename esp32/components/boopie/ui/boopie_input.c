/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_input.h"

#include <stdio.h>
#include <string.h>

#include "boopie_font.h"
#include "boopie_pinyin.h"
#include "lvgl.h"

#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff

#define MAX_KEYS 6        /* the longest syllable */
#define SPELLINGS 12      /* spellings offered for the keys so far */
#define CANDIDATES 6      /* characters on a page */
#define KEY_W 96
#define KEY_H 44
#define KEY_GAP 6
#define KEYS_TOP 212

enum { K_BACK = 9, K_MODE, K_CANCEL, K_DONE };   /* past the eight letter keys */
static const char *const KEY_LABELS[] = { "2 abc", "3 def", "4 ghi", "5 jkl", "6 mno", "7 pqrs", "8 tuv", "9 wxyz",
                                          NULL, LV_SYMBOL_BACKSPACE, "", "取消", "完成" };
static const char *const KEY_LETTERS[] = { "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz" };

static lv_obj_t *s_root, *s_ta, *s_spell_row, *s_cand_row, *s_mode_label;
static boopie_input_done_t s_done;
static bool s_english;
static char s_keys[MAX_KEYS + 1];
static int s_spellings[SPELLINGS], s_nspellings, s_spelling, s_page;
static char s_letters[16];   /* English: the key pressed's letters, offered */

static lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t colour)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, boopie_font_with_cjk(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *button(lv_obj_t *parent, int w, int h, const char *text, uint32_t colour, lv_event_cb_t cb,
                        intptr_t user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_center(label(b, text, colour));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)user);
    return b;
}

/* The n-th UTF-8 character of s, as its own string; false past the end. */
static bool nth_char(const char *s, int n, char out[5])
{
    for (int i = 0; *s; i++) {
        int len = (*s & 0x80) == 0 ? 1 : (*s & 0xE0) == 0xC0 ? 2 : (*s & 0xF0) == 0xE0 ? 3 : 4;
        if (i == n) {
            memcpy(out, s, (size_t)len);
            out[len] = '\0';
            return true;
        }
        s += len;
    }
    return false;
}

static void refresh(void);

static void clear_keys(void)
{
    s_keys[0] = '\0';
    s_letters[0] = '\0';
    s_nspellings = s_spelling = s_page = 0;
}

static void on_spelling(lv_event_t *e)
{
    s_spelling = (int)(intptr_t)lv_event_get_user_data(e);
    s_page = 0;
    refresh();
}

static void on_candidate(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target(e);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    lv_textarea_add_text(s_ta, lv_label_get_text(l));
    clear_keys();
    refresh();
}

static void on_page(lv_event_t *e)
{
    s_page += (int)(intptr_t)lv_event_get_user_data(e);
    if (s_page < 0) {
        s_page = 0;
    }
    refresh();
}

/* The spellings for the keys, and the characters of the one chosen (or, in
 * English, the letters of the key pressed). */
static void refresh(void)
{
    lv_obj_clean(s_spell_row);
    lv_obj_clean(s_cand_row);
    lv_label_set_text(s_mode_label, s_english ? "英" : "中");
    if (s_english) {
        for (int i = 0; s_letters[i]; i++) {
            char one[2] = { s_letters[i], '\0' };
            button(s_cand_row, 40, 38, one, COLOR_TEXT, on_candidate, 0);
        }
        return;
    }
    s_nspellings = boopie_pinyin_match(s_keys, s_spellings, SPELLINGS);
    for (int i = 0; i < s_nspellings; i++) {
        lv_obj_t *b = button(s_spell_row, LV_SIZE_CONTENT, 34, BOOPIE_PINYIN_DICT[s_spellings[i]].py,
                             i == s_spelling ? COLOR_ACCENT : COLOR_DIM, on_spelling, i);
        lv_obj_set_style_pad_hor(b, 10, 0);
    }
    if (!s_nspellings) {
        if (s_keys[0]) {
            label(s_spell_row, "没有这个拼音", COLOR_DIM);
        }
        return;
    }
    const char *chars = BOOPIE_PINYIN_DICT[s_spellings[s_spelling]].hanzi;
    char one[5];
    if (!nth_char(chars, s_page * CANDIDATES, one)) {
        s_page = 0;
    }
    if (s_page > 0) {
        button(s_cand_row, 30, 38, LV_SYMBOL_LEFT, COLOR_DIM, on_page, -1);
    }
    for (int i = 0; i < CANDIDATES && nth_char(chars, s_page * CANDIDATES + i, one); i++) {
        button(s_cand_row, 40, 38, one, COLOR_TEXT, on_candidate, 0);
    }
    if (nth_char(chars, (s_page + 1) * CANDIDATES, one)) {
        button(s_cand_row, 30, 38, LV_SYMBOL_RIGHT, COLOR_DIM, on_page, 1);
    }
}

static void finish(bool done)
{
    boopie_input_done_t cb = s_done;
    char text[64] = "";
    if (done) {
        snprintf(text, sizeof text, "%s", lv_textarea_get_text(s_ta));
    }
    lv_obj_delete_async(s_root);
    s_root = NULL;
    s_done = NULL;
    if (cb) {
        cb(done ? text : NULL, done);
    }
}

static void on_key(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (k < 8) {
        if (s_english) {
            snprintf(s_letters, sizeof s_letters, "%s%c", KEY_LETTERS[k], '2' + k);
        } else if (strlen(s_keys) < MAX_KEYS) {
            size_t n = strlen(s_keys);
            s_keys[n] = (char)('2' + k);
            s_keys[n + 1] = '\0';
            s_spelling = s_page = 0;
        }
    } else if (k == K_BACK) {
        size_t n = strlen(s_keys);
        if (!s_english && n) {
            s_keys[n - 1] = '\0';
            s_spelling = s_page = 0;
        } else {
            s_letters[0] = '\0';
            lv_textarea_delete_char(s_ta);
        }
    } else if (k == K_MODE) {
        s_english = !s_english;
        clear_keys();
    } else {
        finish(k == K_DONE);
        return;
    }
    refresh();
}

static lv_obj_t *row(int y, int w)
{
    lv_obj_t *r = lv_obj_create(s_root);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, 40);
    lv_obj_align(r, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 4, 0);
    lv_obj_set_scroll_dir(r, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
    return r;
}

void boopie_input_open(const char *title, const char *text, const char *hint, int max_chars,
                       boopie_input_done_t done)
{
    if (s_root) {
        finish(false);
    }
    s_done = done;
    s_english = false;
    clear_keys();
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);   /* nothing under it takes a tap */

    lv_obj_t *t = label(s_root, title, COLOR_ACCENT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 30);

    s_ta = lv_textarea_create(s_root);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, (uint32_t)max_chars);
    lv_textarea_set_placeholder_text(s_ta, hint ? hint : "");
    lv_textarea_set_text(s_ta, text ? text : "");
    lv_obj_set_size(s_ta, 260, 48);
    lv_obj_align(s_ta, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_set_style_text_font(s_ta, boopie_font_with_cjk(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_font(s_ta, boopie_font_with_cjk(&lv_font_montserrat_20), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(s_ta, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_text_color(s_ta, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_color(s_ta, lv_color_hex(COLOR_DIM), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(s_ta, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(s_ta, 2, 0);
    lv_obj_set_style_radius(s_ta, 14, 0);
    lv_obj_set_style_pad_hor(s_ta, 14, 0);
    lv_obj_add_state(s_ta, LV_STATE_FOCUSED);

    s_spell_row = row(122, 330);
    /* Spellings run on past the edge: the likeliest stay in view, the rest a swipe away. */
    lv_obj_set_flex_align(s_spell_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_cand_row = row(164, 340);

    for (int k = 0; k < 13; k++) {
        if (k == 8) {
            continue;   /* no key there: the 3 x 4 grid's ninth is backspace */
        }
        int slot = k < 8 ? k : k - 1;   /* 0..11 */
        int col = slot % 3, r = slot / 3;
        lv_obj_t *b = button(s_root, KEY_W, KEY_H, KEY_LABELS[k], k == K_DONE ? COLOR_ACCENT : COLOR_TEXT, on_key, k);
        lv_obj_align(b, LV_ALIGN_TOP_MID, (col - 1) * (KEY_W + KEY_GAP), KEYS_TOP + r * (KEY_H + KEY_GAP));
        if (k == K_MODE) {
            s_mode_label = lv_obj_get_child(b, 0);
        }
    }
    refresh();
}

bool boopie_input_active(void)
{
    return s_root != NULL;
}

void boopie_input_cancel(void)
{
    if (s_root) {
        finish(false);
    }
}
