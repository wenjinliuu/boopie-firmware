/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_noise_ui.h"

#include <stdio.h>
#include <string.h>

#include "boopie_font.h"
#include "boopie_icons.h"
#include "boopie_noise.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "muse_board.h"
#include "muse_settings.h"

#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff
#define COLOR_GOLD 0xffd246

static const struct {
    const char *id;
    boopie_icon_t icon;
} KINDS[BOOPIE_NOISE_COUNT] = {
    [BOOPIE_NOISE_WHITE] = { "white", BOOPIE_ICON_NOISE_WHITE },
    [BOOPIE_NOISE_PINK] = { "pink", BOOPIE_ICON_NOISE_PINK },
    [BOOPIE_NOISE_RAIN] = { "rain", BOOPIE_ICON_NOISE_RAIN },
    [BOOPIE_NOISE_WAVES] = { "waves", BOOPIE_ICON_NOISE_WAVES },
};
static const int MINUTES[] = { 15, 30, 60, 0 };
#define MINUTE_COUNT (int)(sizeof MINUTES / sizeof MINUTES[0])

static int s_minutes = 30;   /* how long the next one plays */
static lv_obj_t *s_root, *s_kinds[BOOPIE_NOISE_COUNT], *s_chips[MINUTE_COUNT], *s_status, *s_stop;
static lv_timer_t *s_timer;
static char s_shown[96];

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
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

static lv_obj_t *card(lv_obj_t *parent, int w, int h, int radius, lv_event_cb_t cb, void *user)
{
    lv_obj_t *c = lv_button_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_radius(c, radius, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(c, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, user);
    return c;
}

static void refresh(void)
{
    boopie_noise_t kind;
    int left;
    bool on = boopie_noise_playing(now_ms(), &kind, &left);
    bool speaker = muse_settings_speaker_on();
    char status[96];
    if (!speaker) {
        snprintf(status, sizeof status, "扬声器关着：去 设置 › 声音 打开");
    } else if (!on) {
        snprintf(status, sizeof status, "点一种声音开始");
    } else if (left < 0) {
        snprintf(status, sizeof status, "%s  一直放", boopie_noise_name(kind));
    } else {
        snprintf(status, sizeof status, "%s  还有 %d:%02d", boopie_noise_name(kind), left / 60, left % 60);
    }
    char shown[sizeof s_shown];
    snprintf(shown, sizeof shown, "%.80s|%d|%d|%d", status, on ? (int)kind : -1, s_minutes, on);
    if (strcmp(shown, s_shown) == 0) {
        return;
    }
    strcpy(s_shown, shown);
    lv_label_set_text(s_status, status);
    for (int i = 0; i < BOOPIE_NOISE_COUNT; i++) {
        lv_obj_set_style_border_width(s_kinds[i], on && (int)kind == i ? 3 : 0, 0);
    }
    for (int i = 0; i < MINUTE_COUNT; i++) {
        bool sel = MINUTES[i] == s_minutes;
        lv_obj_set_style_bg_color(s_chips[i], lv_color_hex(sel ? COLOR_ACCENT : COLOR_CARD), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_chips[i], 0), lv_color_hex(sel ? 0x140f26 : COLOR_TEXT), 0);
    }
    lv_obj_set_style_opa(s_stop, on ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void on_tick(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static void on_kind(lv_event_t *e)
{
    boopie_noise_play((boopie_noise_t)(intptr_t)lv_event_get_user_data(e), s_minutes, now_ms());
    refresh();
}

static void on_minutes(lv_event_t *e)
{
    s_minutes = (int)(intptr_t)lv_event_get_user_data(e);
    boopie_noise_t kind;
    if (boopie_noise_playing(now_ms(), &kind, NULL)) {
        boopie_noise_play(kind, s_minutes, now_ms());   /* from now */
    }
    refresh();
}

static void on_stop(lv_event_t *e)
{
    (void)e;
    boopie_noise_stop(now_ms());
    refresh();
}

void boopie_noise_ui_open_locked(void)
{
    if (s_root) {
        lv_obj_move_foreground(s_root);
        return;
    }
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = text(s_root, &boopie_font_pixel_24, COLOR_ACCENT, "白噪音");
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);

    /* The four sounds, two by two: an icon and a name each. */
    const int w = 136, h = 96, gap = 12, top = 84;
    const lv_font_t *name_font = boopie_font_ui(22) ? boopie_font_ui(22) : &lv_font_montserrat_20;
    for (int i = 0; i < BOOPIE_NOISE_COUNT; i++) {
        lv_obj_t *c = card(s_root, w, h, 22, on_kind, (void *)(intptr_t)i);
        lv_obj_align(c, LV_ALIGN_TOP_MID, (i % 2 ? 1 : -1) * (w + gap) / 2, top + (i / 2) * (h + gap));
        lv_obj_t *tile = boopie_icon_tile(c, KINDS[i].icon, 48, 3);
        lv_obj_align(tile, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_t *n = text(c, name_font, COLOR_TEXT, boopie_noise_name((boopie_noise_t)i));
        lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, -8);
        s_kinds[i] = c;
    }

    /* How long. */
    const int cw = 70, ch = 40, cgap = 8;
    int x0 = -((MINUTE_COUNT * cw + (MINUTE_COUNT - 1) * cgap) / 2) + cw / 2;
    for (int i = 0; i < MINUTE_COUNT; i++) {
        char label[16];
        if (MINUTES[i]) {
            snprintf(label, sizeof label, "%d分", MINUTES[i]);
        } else {
            snprintf(label, sizeof label, "不关");
        }
        lv_obj_t *c = card(s_root, cw, ch, ch / 2, on_minutes, (void *)(intptr_t)MINUTES[i]);
        lv_obj_align(c, LV_ALIGN_TOP_MID, x0 + i * (cw + cgap), top + 2 * (h + gap) + 4);
        lv_obj_center(text(c, &lv_font_montserrat_16, COLOR_TEXT, label));
        s_chips[i] = c;
    }

    s_status = text(s_root, &lv_font_montserrat_16, COLOR_DIM, "");
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, top + 2 * (h + gap) + 56);

    s_stop = card(s_root, 128, 48, 24, on_stop, NULL);
    lv_obj_align(s_stop, LV_ALIGN_TOP_MID, 0, top + 2 * (h + gap) + 86);
    lv_obj_center(text(s_stop, &lv_font_montserrat_20, COLOR_GOLD, LV_SYMBOL_STOP "  停止"));

    s_shown[0] = '\0';
    refresh();
    s_timer = lv_timer_create(on_tick, 500, NULL);
}

bool boopie_noise_ui_active(void)
{
    return s_root != NULL;
}

void boopie_noise_ui_close(void)
{
    muse_board->display_lock(-1);
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_root) {
        lv_obj_delete_async(s_root);
        s_root = NULL;
    }
    muse_board->display_unlock();
}

bool boopie_noise_ui_play(const char *kind, int minutes, char *out, size_t cap)
{
    int k = BOOPIE_NOISE_RAIN;
    if (kind && *kind) {
        for (k = 0; k < BOOPIE_NOISE_COUNT && strcmp(kind, KINDS[k].id) != 0; k++) {
        }
        if (k == BOOPIE_NOISE_COUNT) {
            return false;
        }
    }
    if (minutes < 0) {
        minutes = 30;
    }
    boopie_noise_play((boopie_noise_t)k, minutes, now_ms());
    if (!muse_settings_speaker_on()) {
        snprintf(out, cap, "the speaker is off in settings, so nothing will be heard until it's turned on");
    } else if (minutes) {
        snprintf(out, cap, "playing %s for %d minutes, then fading out", KINDS[k].id, minutes);
    } else {
        snprintf(out, cap, "playing %s until stopped", KINDS[k].id);
    }
    return true;
}

void boopie_noise_ui_stop(char *out, size_t cap)
{
    bool was = boopie_noise_playing(now_ms(), NULL, NULL);
    boopie_noise_stop(now_ms());
    snprintf(out, cap, "%s", was ? "stopped" : "nothing was playing");
}
