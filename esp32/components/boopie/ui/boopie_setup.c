/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_setup.h"

#include <stdio.h>
#include <string.h>

#include "boopie_font.h"
#include "boopie_setup_web.h"
#include "lvgl.h"

#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff
#define COLOR_OK 0x7ee0a0

#define QR_SIZE 184
#define IDLE_CLOSE_S 600

typedef enum { V_NONE, V_FAILED, V_JOIN, V_OPEN, V_SAVED } view_t;

static lv_obj_t *s_root;
static view_t s_view;
static boopie_setup_ap_t s_ap;
static void (*s_done)(uint32_t saved);
static uint32_t s_saves_seen;

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

static void on_close(lv_event_t *e)
{
    (void)e;
    boopie_setup_close();
}

static void close_button(const char *label, bool main)
{
    lv_obj_t *b = lv_button_create(s_root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 150, 48);
    lv_obj_set_style_radius(b, 18, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(main ? COLOR_ACCENT : COLOR_CARD), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(main ? 0x8c63e6 : COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_center(text(b, &lv_font_montserrat_20, main ? 0x14102a : COLOR_TEXT, label));
    lv_obj_add_event_cb(b, on_close, LV_EVENT_CLICKED, NULL);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -34);
}

/* A QR code on a white card (phones want the quiet zone round it). */
static void qr(const char *data, int y)
{
    lv_obj_t *q = lv_qrcode_create(s_root);
    lv_qrcode_set_size(q, QR_SIZE);
    lv_qrcode_set_dark_color(q, lv_color_black());
    lv_qrcode_set_light_color(q, lv_color_white());
    lv_qrcode_update(q, data, strlen(data));
    lv_obj_set_style_border_color(q, lv_color_white(), 0);
    lv_obj_set_style_border_width(q, 10, 0);
    lv_obj_set_style_radius(q, 8, 0);
    lv_obj_align(q, LV_ALIGN_TOP_MID, 0, y);
}

static void view(view_t v)
{
    s_view = v;
    lv_obj_clean(s_root);
    char line[160];
    switch (v) {
    case V_FAILED:
        lv_obj_align(text(s_root, &lv_font_montserrat_28, COLOR_TEXT, "热点没开起来"), LV_ALIGN_TOP_MID, 0, 120);
        lv_obj_align(text(s_root, &lv_font_montserrat_20, COLOR_DIM, "关掉再试一次，\n或者在屏幕上选 Wi-Fi。"),
                     LV_ALIGN_TOP_MID, 0, 180);
        close_button("好的", true);
        break;
    case V_JOIN: {
        lv_obj_align(text(s_root, &lv_font_montserrat_28, COLOR_TEXT, "用手机设置"), LV_ALIGN_TOP_MID, 0, 34);
        lv_obj_align(text(s_root, &lv_font_montserrat_16, COLOR_DIM, "① 相机扫码，连上我的热点"), LV_ALIGN_TOP_MID, 0, 72);
        snprintf(line, sizeof line, "WIFI:T:WPA;S:%s;P:%s;;", s_ap.ssid, s_ap.pass);
        qr(line, 100);
        snprintf(line, sizeof line, "%s\n密码 %s", s_ap.ssid, s_ap.pass);
        lv_obj_align(text(s_root, &lv_font_montserrat_20, COLOR_TEXT, line), LV_ALIGN_TOP_MID, 0, 312);
        close_button("关闭", false);
        break;
    }
    case V_OPEN:
        lv_obj_align(text(s_root, &lv_font_montserrat_28, COLOR_TEXT, "连上了！"), LV_ALIGN_TOP_MID, 0, 34);
        lv_obj_align(text(s_root, &lv_font_montserrat_16, COLOR_DIM, "② 设置页没弹出来？扫这个码"), LV_ALIGN_TOP_MID, 0, 72);
        qr(BOOPIE_SETUP_URL, 100);
        lv_obj_align(text(s_root, &lv_font_montserrat_20, COLOR_TEXT, "或在浏览器打开\n192.168.4.1"), LV_ALIGN_TOP_MID, 0, 312);
        close_button("关闭", false);
        break;
    case V_SAVED: {
        uint32_t saved = 0;
        boopie_setup_web_saves(&saved);
        static const char *const WHAT[] = { "Wi-Fi", "AI 助手", "Muse", "代理订阅", "名字" };
        size_t n = 0;
        line[0] = '\0';
        for (int i = 0; i < 5; i++) {
            if (saved & (1u << i)) {
                n += snprintf(line + n, sizeof line - n, "%s%s", n ? (i == 3 ? "\n" : "、") : "", WHAT[i]);
            }
        }
        lv_obj_align(text(s_root, &lv_font_montserrat_28, COLOR_OK, LV_SYMBOL_OK " 已保存"), LV_ALIGN_TOP_MID, 0, 110);
        lv_obj_align(text(s_root, &lv_font_montserrat_20, COLOR_TEXT, line), LV_ALIGN_TOP_MID, 0, 170);
        lv_obj_align(text(s_root, &lv_font_montserrat_16, COLOR_DIM, "还要改别的，就在手机上接着填。"),
                     LV_ALIGN_TOP_MID, 0, 270);
        close_button("完成", true);
        break;
    }
    default:
        break;
    }
}

void boopie_setup_open(void (*done)(uint32_t saved))
{
    s_done = done;
    if (s_root) {
        return;
    }
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    s_saves_seen = 0;
    view(boopie_setup_web_start(&s_ap) ? V_JOIN : V_FAILED);
}

bool boopie_setup_active(void)
{
    return s_root != NULL;
}

void boopie_setup_close(void)
{
    if (!s_root) {
        return;
    }
    uint32_t saved = 0;
    boopie_setup_web_saves(&saved);
    boopie_setup_web_stop();
    lv_obj_delete_async(s_root);   /* maybe from its own button */
    s_root = NULL;
    s_view = V_NONE;
    memset(&s_ap, 0, sizeof s_ap);
    if (s_done) {
        void (*done)(uint32_t) = s_done;
        s_done = NULL;
        done(saved);
    }
}

void boopie_setup_tick(void)
{
    if (!s_root || s_view == V_FAILED) {
        return;
    }
    uint32_t saves = boopie_setup_web_saves(NULL);
    if (saves != s_saves_seen) {
        s_saves_seen = saves;
        view(V_SAVED);
    } else if (s_view == V_JOIN && boopie_setup_web_clients() > 0) {
        view(V_OPEN);
    } else if (s_view == V_OPEN && boopie_setup_web_clients() == 0) {
        view(V_JOIN);
    }
    if (boopie_setup_web_idle_s() > IDLE_CLOSE_S) {
        boopie_setup_close();
    }
}
