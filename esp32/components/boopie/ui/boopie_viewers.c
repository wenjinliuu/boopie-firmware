/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_viewers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "boopie_font.h"
#include "boopie_history.h"
#include "boopie_store.h"
#include "lvgl.h"
#include "muse_board.h"
#include "muse_voice.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "rom/tjpgd.h"   /* the ROM's, as main/image_fetch.c decodes with */
#define BIG_ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
typedef UINT jd_size_t;
typedef UINT jd_out_t;
#else
#include "src/libs/tjpgd/tjpgd.h"   /* LVGL's copy: the simulator has no ROM */
#define BIG_ALLOC(n) malloc(n)
typedef size_t jd_size_t;
typedef int jd_out_t;
#endif

#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff
#define COLOR_DANGER 0xff6b6b

#define PICTURE_W 330
#define PICTURE_H 260
#define JPEG_POOL 4096

static lv_obj_t *s_root;

/* ---- pieces ---- */

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, const char *s)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, boopie_font_with_cjk(font), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_label_set_text(l, s);
    return l;
}

static void close_now(void)
{
    if (s_root) {
        lv_obj_delete_async(s_root);   /* maybe from one of its own buttons */
        s_root = NULL;
    }
}

static void on_close(lv_event_t *e)
{
    (void)e;
    close_now();
}

static lv_obj_t *button(lv_obj_t *parent, int w, const char *label, uint32_t colour, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, 48);
    lv_obj_set_style_radius(b, 18, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_center(text(b, &lv_font_montserrat_20, colour, label));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

/* A full-screen layer with a pixel title. */
static void open_root(const char *title)
{
    close_now();
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = text(s_root, &boopie_font_pixel_24, COLOR_ACCENT, title);
    lv_obj_set_style_text_letter_space(t, 2, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);
}

/* ---- chat history ---- */

void boopie_viewer_chat_locked(void)
{
    open_root("聊天记录");
    lv_obj_t *list = lv_obj_create(s_root);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 330, 300);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_style_pad_bottom(list, 20, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    boopie_chat_entry_t *e = BIG_ALLOC(sizeof *e * BOOPIE_CHAT_MAX);
    int n = e ? boopie_chat_load(e, BOOPIE_CHAT_MAX) : 0;
    if (!n) {
        lv_obj_t *l = text(list, &lv_font_montserrat_20, COLOR_DIM, "还没有聊天记录。\n和我说说话吧！");
        lv_obj_set_width(l, lv_pct(100));
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(l, 60, 0);
    }
    for (int i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_create(list);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_radius(c, 14, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD), 0);
        lv_obj_set_style_pad_all(c, 12, 0);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(c, 6, 0);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        char line[BOOPIE_CHAT_SAID_MAX + 16];
        if (e[i].when) {
            time_t t = (time_t)e[i].when;
            struct tm tm;
            localtime_r(&t, &tm);
            snprintf(line, sizeof line, "%d月%d日 %02d:%02d", tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
            text(c, &lv_font_montserrat_14, COLOR_DIM, line);
        }
        if (e[i].said[0]) {
            snprintf(line, sizeof line, "我：%s", e[i].said);
            lv_obj_t *s = text(c, &lv_font_montserrat_16, COLOR_ACCENT, line);
            lv_obj_set_width(s, lv_pct(100));
            lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_WRAP);
        }
        if (e[i].reply[0]) {
            lv_obj_t *r = text(c, &lv_font_montserrat_16, COLOR_TEXT, e[i].reply);
            lv_obj_set_width(r, lv_pct(100));
            lv_label_set_long_mode(r, LV_LABEL_LONG_MODE_WRAP);
        }
    }
    free(e);
    lv_obj_align(button(s_root, 120, "关闭", COLOR_TEXT, on_close, NULL), LV_ALIGN_BOTTOM_MID, 0, -28);
}

/* ---- album ---- */

static char (*s_paths)[BOOPIE_ALBUM_PATH_MAX];
static int s_count, s_index;
static lv_obj_t *s_picture, *s_counter;
static uint16_t *s_pixels;           /* the picture shown, RGB565 */
static lv_image_dsc_t s_picture_dsc;

/* ---- decoding: tjpgd, shrunk 1/2, 1/4 or 1/8 to fit ---- */

typedef struct {
    FILE *f;
    uint16_t *px;
    int w;
} jpeg_t;

static jd_size_t jpeg_in(JDEC *jd, uint8_t *buf, jd_size_t len)
{
    jpeg_t *j = jd->device;
    if (buf) {
        return (jd_size_t)fread(buf, 1, len, j->f);
    }
    return fseek(j->f, (long)len, SEEK_CUR) == 0 ? len : 0;
}

static jd_out_t jpeg_out(JDEC *jd, void *bitmap, JRECT *r)
{
    jpeg_t *j = jd->device;
    const uint8_t *rgb = bitmap;
    for (int y = r->top; y <= r->bottom; y++) {
        for (int x = r->left; x <= r->right; x++, rgb += 3) {
#ifdef ESP_PLATFORM
            uint8_t r = rgb[0], b = rgb[2];   /* the ROM's: red first */
#else
            uint8_t r = rgb[2], b = rgb[0];   /* LVGL's copy: blue first */
#endif
            j->px[y * j->w + x] = (uint16_t)((r & 0xF8) << 8 | (rgb[1] & 0xFC) << 3 | b >> 3);
        }
    }
    return 1;
}

/* Nearest-neighbour down to fit the frame, keeping the shape. */
static uint16_t *shrink(uint16_t *px, int *w, int *h)
{
    if (*w <= PICTURE_W && *h <= PICTURE_H) {
        return px;
    }
    int nw = PICTURE_W, nh = *h * PICTURE_W / *w;
    if (nh > PICTURE_H) {
        nh = PICTURE_H;
        nw = *w * PICTURE_H / *h;
    }
    uint16_t *out = nw > 0 && nh > 0 ? BIG_ALLOC((size_t)nw * nh * sizeof(uint16_t)) : NULL;
    if (out) {
        for (int y = 0; y < nh; y++) {
            const uint16_t *row = px + (size_t)(y * *h / nh) * *w;
            for (int x = 0; x < nw; x++) {
                out[y * nw + x] = row[x * *w / nw];
            }
        }
        *w = nw;
        *h = nh;
    }
    free(px);
    return out;
}

/* The picture at path, decoded to fit the frame; NULL if it can't be. */
static uint16_t *decode(const char *path, int *w, int *h)
{
    jpeg_t j = { .f = fopen(path, "rb") };
    void *pool = malloc(JPEG_POOL);
    uint16_t *px = NULL;
    JDEC jd;
    if (j.f && pool && jd_prepare(&jd, jpeg_in, pool, JPEG_POOL, &j) == JDR_OK) {
        uint8_t scale = 0;
#ifdef ESP_PLATFORM
        /* The ROM's decoder shrinks by 1/2, 1/4 or 1/8 as it goes: the
         * smallest of those that's still at least the frame. */
        while (scale < 3 && (int)(jd.width >> (scale + 1)) >= PICTURE_W && (int)(jd.height >> (scale + 1)) >= PICTURE_H) {
            scale++;
        }
#endif
        *w = (int)(jd.width >> scale);
        *h = (int)(jd.height >> scale);
        px = *w > 0 && *h > 0 ? BIG_ALLOC((size_t)*w * *h * sizeof(uint16_t)) : NULL;
        j.px = px;
        j.w = *w;
        if (px && jd_decomp(&jd, jpeg_out, scale) != JDR_OK) {
            free(px);   /* progressive, or broken: tjpgd takes baseline only */
            px = NULL;
        }
    }
    free(pool);
    if (j.f) {
        fclose(j.f);
    }
    return px ? shrink(px, w, h) : NULL;
}

static void show_picture(void)
{
    free(s_pixels);
    int w = 0, h = 0;
    s_pixels = decode(s_paths[s_index], &w, &h);
    if (s_pixels) {
        s_picture_dsc = (lv_image_dsc_t){
            .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = w, .h = h,
                        .stride = w * sizeof(uint16_t) },
            .data_size = (uint32_t)(w * h * sizeof(uint16_t)),
            .data = (const uint8_t *)s_pixels,
        };
        lv_image_set_src(s_picture, &s_picture_dsc);
    } else {
        lv_image_set_src(s_picture, LV_SYMBOL_WARNING);   /* progressive or broken */
    }
    lv_obj_center(s_picture);
    char c[24];
    snprintf(c, sizeof c, "%d / %d", s_index + 1, s_count);
    lv_label_set_text(s_counter, c);
}

static void on_step(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    s_index = (s_index + d + s_count) % s_count;
    show_picture();
}

static void on_album_deleted(lv_event_t *e)
{
    (void)e;
    free(s_paths);
    s_paths = NULL;
    free(s_pixels);   /* its image went with it */
    s_pixels = NULL;
}

void boopie_viewer_album_locked(void)
{
    open_root("相册");
    s_paths = malloc(sizeof *s_paths * BOOPIE_ALBUM_MAX);
    s_count = s_paths ? boopie_album_list(s_paths, BOOPIE_ALBUM_MAX) : 0;
    s_index = 0;
    lv_obj_add_event_cb(s_root, on_album_deleted, LV_EVENT_DELETE, NULL);
    if (!s_count) {
        lv_obj_t *l = text(s_root, &lv_font_montserrat_20, COLOR_DIM, "还没有图片。\n让 Muse 给你看张图，\n就会存在这里。");
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
    } else {
        lv_obj_t *box = lv_obj_create(s_root);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, PICTURE_W, PICTURE_H);
        lv_obj_align(box, LV_ALIGN_CENTER, 0, -6);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(box, on_step, LV_EVENT_CLICKED, (void *)(intptr_t)1);   /* tap: the next */
        s_picture = lv_image_create(box);
        s_counter = text(s_root, &lv_font_montserrat_16, COLOR_DIM, "");
        lv_obj_align(s_counter, LV_ALIGN_BOTTOM_MID, 0, -86);
        lv_obj_align(button(s_root, 64, LV_SYMBOL_LEFT, COLOR_TEXT, on_step, (void *)(intptr_t)-1), LV_ALIGN_BOTTOM_MID,
                     -96, -28);
        lv_obj_align(button(s_root, 64, LV_SYMBOL_RIGHT, COLOR_TEXT, on_step, (void *)(intptr_t)1), LV_ALIGN_BOTTOM_MID,
                     96, -28);
        show_picture();
    }
    lv_obj_align(button(s_root, 100, "关闭", COLOR_TEXT, on_close, NULL), LV_ALIGN_BOTTOM_MID, 0, -28);
}

/* ---- clearing ---- */

void boopie_viewer_clear(const char *what)
{
    bool all = strcmp(what, "all") == 0;
    if (all || strcmp(what, "chat") == 0) {
        boopie_chat_clear();
    }
    if (all || strcmp(what, "album") == 0) {
        boopie_store_clear(BOOPIE_STORE_ALBUM);
    }
    if (all || strcmp(what, "notes") == 0) {
        muse_voice_clear_notes();   /* the voice task holds them too */
    }
}

static char s_ask[8];

static void on_clear_yes(lv_event_t *e)
{
    (void)e;
    boopie_viewer_clear(s_ask);
    close_now();
}

bool boopie_viewer_ask_clear(const char *what)
{
    static const struct {
        const char *key, *name;
    } KINDS[] = { { "chat", "所有聊天记录" }, { "album", "相册里的图片" }, { "notes", "没发出去的留言" },
                  { "all", "聊天记录、相册和留言" } };
    const char *name = NULL;
    for (size_t i = 0; what && i < sizeof KINDS / sizeof KINDS[0]; i++) {
        if (strcmp(what, KINDS[i].key) == 0) {
            name = KINDS[i].name;
        }
    }
    if (!name) {
        return false;
    }
    muse_board->display_lock(-1);
    strlcpy(s_ask, what, sizeof s_ask);
    open_root("确认");
    char q[96];
    snprintf(q, sizeof q, "要删掉%s吗？\n删掉就找不回来了。", name);
    lv_obj_t *l = text(s_root, &lv_font_montserrat_20, COLOR_TEXT, q);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_align(button(s_root, 260, "删除", COLOR_DANGER, on_clear_yes, NULL), LV_ALIGN_TOP_MID, 0, 240);
    lv_obj_align(button(s_root, 260, "不删", COLOR_TEXT, on_close, NULL), LV_ALIGN_TOP_MID, 0, 300);
    muse_board->display_unlock();
    return true;
}

bool boopie_viewer_active(void)
{
    return s_root != NULL;
}

void boopie_viewer_close(void)
{
    muse_board->display_lock(-1);
    close_now();
    muse_board->display_unlock();
}
