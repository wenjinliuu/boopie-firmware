/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_font.h"

#include "boopie_pixel_font.h"

#include <string.h>

#ifdef BOOPIE_DATA_IN_ASSETS
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

/* The one thing that differs between the two sizes. */
typedef struct {
    uint8_t scale;
} pixel_font_dsc_t;

static bool get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *g, uint32_t letter,
                          uint32_t letter_next)
{
    (void)letter_next;
    bool tab = letter == '\t';
    int32_t index = boopie_pixel_find(tab ? ' ' : letter);
    if (index < 0) {
        return false;
    }
    int scale = ((const pixel_font_dsc_t *)font->dsc)->scale;
    int w = boopie_pixel_width(index) * scale;
    bool blank = boopie_pixel_blank(index);

    g->adv_w = tab ? 2 * w : w;
    g->box_w = blank ? 0 : w;
    g->box_h = blank ? 0 : BOOPIE_PIXEL_CELL * scale;
    g->ofs_x = 0;
    g->ofs_y = -(BOOPIE_PIXEL_CELL - BOOPIE_PIXEL_BASELINE) * scale;
    g->stride = 0;
    g->format = LV_FONT_GLYPH_FORMAT_A1;
    g->is_placeholder = false;
    g->gid.index = (uint32_t)index + 1;   /* 0 means none */
    return true;
}

/* The glyph as an A8 bitmap in draw_buf, each pixel a scale x scale block.
 * LVGL takes draw_buf itself back, as the built-in fonts return it. */
static const void *get_glyph_bitmap(lv_font_glyph_dsc_t *g, lv_draw_buf_t *draw_buf)
{
    if (!g->gid.index || !draw_buf || !g->box_w) {
        return NULL;
    }
    int32_t index = (int32_t)g->gid.index - 1;
    int scale = ((const pixel_font_dsc_t *)g->resolved_font->dsc)->scale;
    uint32_t stride = draw_buf->header.stride;
    uint8_t *out = draw_buf->data;
    uint8_t cell[BOOPIE_PIXEL_CELL_BYTES];
    boopie_pixel_cell(index, cell);   /* once: on the device it's a cached flash read */
    for (int y = 0; y < g->box_h; y++) {
        uint8_t *row = out + (uint32_t)y * stride;
        for (int x = 0; x < g->box_w; x++) {
            uint32_t bit = (uint32_t)(y / scale) * BOOPIE_PIXEL_CELL + (uint32_t)(x / scale);
            row[x] = cell[bit / 8] & (0x80 >> (bit % 8)) ? 0xFF : 0x00;
        }
    }
    return draw_buf;
}

static const pixel_font_dsc_t s_dsc_2x = { .scale = 2 };
static const pixel_font_dsc_t s_dsc_1x = { .scale = 1 };

const lv_font_t boopie_font_pixel_24 = {
    .get_glyph_dsc = get_glyph_dsc,
    .get_glyph_bitmap = get_glyph_bitmap,
    .line_height = BOOPIE_PIXEL_CELL * 2,
    .base_line = (BOOPIE_PIXEL_CELL - BOOPIE_PIXEL_BASELINE) * 2,
    .underline_position = -2,
    .underline_thickness = 2,
    .dsc = &s_dsc_2x,
};

const lv_font_t boopie_font_pixel_12 = {
    .get_glyph_dsc = get_glyph_dsc,
    .get_glyph_bitmap = get_glyph_bitmap,
    .line_height = BOOPIE_PIXEL_CELL,
    .base_line = BOOPIE_PIXEL_CELL - BOOPIE_PIXEL_BASELINE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &s_dsc_1x,
};

#define CJK_COPIES 8
#define SIZES 8
/* Rendered glyphs kept per size: a screen of Chinese rarely shows more than
 * this many different ones, and 256 at 28 px held some 200 KB of PSRAM each. */
#define GLYPH_CACHE 96

#ifdef BOOPIE_DATA_IN_ASSETS
/*
 * On the device ui.otf (1.5 MB) stays in the assets partition and is read as
 * a file: tiny_ttf's stream mode, over a small LVGL file system driver ("B:")
 * that reads the pack in 1 KB blocks and keeps the last 64 (64 KB of PSRAM,
 * not 1.5 MB). Only glyphs not already rendered read anything: on a PC model
 * of this cache a new Chinese glyph at 28 px reads about 4 KB of flash, and
 * renders the same as from memory. Everything here runs under the LVGL lock.
 */
#include "boopie_assets.h"

#define BLOCK 1024
#define BLOCKS 64

typedef struct {
    uint32_t base, size, pos;
} asset_file_t;

static uint8_t *s_blocks;
static uint32_t s_block_at[BLOCKS];
static uint32_t s_block_used[BLOCKS];
static uint32_t s_clock;
static int s_last;

/* The cached block holding pack offset `at` (BLOCK-aligned), read if need be. */
static const uint8_t *block(uint32_t at)
{
    if (s_block_at[s_last] == at) {   /* the parser reads a byte at a time, mostly in one block */
        return s_blocks + s_last * BLOCK;
    }
    int oldest = 0;
    for (int i = 0; i < BLOCKS; i++) {
        if (s_block_at[i] == at) {
            s_block_used[i] = ++s_clock;
            s_last = i;
            return s_blocks + i * BLOCK;
        }
        if (s_block_used[i] < s_block_used[oldest]) {
            oldest = i;
        }
    }
    uint32_t n = boopie_assets_size() - at < BLOCK ? boopie_assets_size() - at : BLOCK;
    s_block_at[oldest] = UINT32_MAX;
    if (!boopie_assets_read(at, s_blocks + oldest * BLOCK, n)) {
        return NULL;
    }
    s_block_at[oldest] = at;
    s_block_used[oldest] = ++s_clock;
    s_last = oldest;
    return s_blocks + oldest * BLOCK;
}

static void *fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    (void)drv;
    uint32_t base, size;
    if (mode != LV_FS_MODE_RD || !boopie_assets_find(path, &base, &size)) {
        return NULL;
    }
    asset_file_t *f = lv_malloc(sizeof(*f));
    if (f) {
        *f = (asset_file_t){ base, size, 0 };
    }
    return f;
}

static lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *file)
{
    (void)drv;
    lv_free(file);
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file, void *buf, uint32_t btr, uint32_t *br)
{
    (void)drv;
    asset_file_t *f = file;
    uint8_t *out = buf;
    uint32_t want = f->pos < f->size ? (btr < f->size - f->pos ? btr : f->size - f->pos) : 0;
    uint32_t done = 0;
    while (done < want) {
        uint32_t at = f->base + f->pos;
        const uint8_t *b = block(at & ~(uint32_t)(BLOCK - 1));
        if (!b) {
            break;
        }
        uint32_t in = at & (BLOCK - 1);
        uint32_t n = BLOCK - in < want - done ? BLOCK - in : want - done;
        memcpy(out + done, b + in, n);
        done += n;
        f->pos += n;
    }
    *br = done;
    return done == want ? LV_FS_RES_OK : LV_FS_RES_HW_ERR;
}

static lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *file, uint32_t pos, lv_fs_whence_t whence)
{
    (void)drv;
    asset_file_t *f = file;
    uint32_t from = whence == LV_FS_SEEK_CUR ? f->pos : whence == LV_FS_SEEK_END ? f->size : 0;
    f->pos = from + pos;
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_tell(lv_fs_drv_t *drv, void *file, uint32_t *pos)
{
    (void)drv;
    *pos = ((asset_file_t *)file)->pos;
    return LV_FS_RES_OK;
}

/* Registers "B:" once; false without the assets or the block cache. */
static bool assets_fs(void)
{
    static lv_fs_drv_t s_drv;
    static bool s_ok;
    if (s_ok) {
        return true;
    }
    if (!boopie_assets_ready()) {
        return false;
    }
    s_blocks = heap_caps_malloc(BLOCK * BLOCKS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_blocks) {
        return false;
    }
    for (int i = 0; i < BLOCKS; i++) {
        s_block_at[i] = UINT32_MAX;
    }
    lv_fs_drv_init(&s_drv);
    s_drv.letter = 'B';
    s_drv.open_cb = fs_open;
    s_drv.close_cb = fs_close;
    s_drv.read_cb = fs_read;
    s_drv.seek_cb = fs_seek;
    s_drv.tell_cb = fs_tell;
    lv_fs_drv_register(&s_drv);
    s_ok = true;
    return true;
}

/*
 * One at a time: LVGL draws with two threads and lays out text on a third,
 * and tiny_ttf reading a file keeps a position (a seek, then a read) that
 * two of them at once would move under each other, as they would the block
 * cache: glyphs came out scrambled ("聊" like a mosaic). In memory there was
 * no position to share. Every glyph goes through these two, under one lock.
 */
static SemaphoreHandle_t s_ttf_lock;
static bool (*s_ttf_dsc)(const lv_font_t *, lv_font_glyph_dsc_t *, uint32_t, uint32_t);
static const void *(*s_ttf_bitmap)(lv_font_glyph_dsc_t *, lv_draw_buf_t *);
static void (*s_ttf_release)(const lv_font_t *, lv_font_glyph_dsc_t *);

static bool locked_dsc(const lv_font_t *f, lv_font_glyph_dsc_t *g, uint32_t letter, uint32_t next)
{
    xSemaphoreTakeRecursive(s_ttf_lock, portMAX_DELAY);
    bool ok = s_ttf_dsc(f, g, letter, next);
    xSemaphoreGiveRecursive(s_ttf_lock);
    return ok;
}

static const void *locked_bitmap(lv_font_glyph_dsc_t *g, lv_draw_buf_t *buf)
{
    xSemaphoreTakeRecursive(s_ttf_lock, portMAX_DELAY);
    const void *r = s_ttf_bitmap(g, buf);
    xSemaphoreGiveRecursive(s_ttf_lock);
    return r;
}

static void locked_release(const lv_font_t *f, lv_font_glyph_dsc_t *g)
{
    xSemaphoreTakeRecursive(s_ttf_lock, portMAX_DELAY);
    s_ttf_release(f, g);
    xSemaphoreGiveRecursive(s_ttf_lock);
}

static lv_font_t *make_ui_font(int size)
{
    if (!assets_fs()) {
        return NULL;   /* no assets flashed: the pixel font stands in */
    }
    if (!s_ttf_lock && !(s_ttf_lock = xSemaphoreCreateRecursiveMutex())) {
        return NULL;
    }
    lv_font_t *f = lv_tiny_ttf_create_file_ex("B:fonts/ui.otf", size, LV_FONT_KERNING_NONE, GLYPH_CACHE);
    if (f) {
        s_ttf_dsc = f->get_glyph_dsc;
        s_ttf_bitmap = f->get_glyph_bitmap;
        s_ttf_release = f->release_glyph;
        f->get_glyph_dsc = locked_dsc;
        f->get_glyph_bitmap = locked_bitmap;
        if (s_ttf_release) {
            f->release_glyph = locked_release;
        }
    }
    return f;
}
#else
/* ui.otf, linked in by the build (EMBED_FILES; the simulator's incbin). */
extern const uint8_t ui_otf_start[] __asm__("_binary_ui_otf_start");
extern const uint8_t ui_otf_end[] __asm__("_binary_ui_otf_end");

static lv_font_t *make_ui_font(int size)
{
    return lv_tiny_ttf_create_data_ex(ui_otf_start, (size_t)(ui_otf_end - ui_otf_start), size,
                                      LV_FONT_KERNING_NONE, GLYPH_CACHE);
}
#endif

const lv_font_t *boopie_font_ui(int size)
{
    static struct {
        int size;
        lv_font_t *font;
    } s_made[SIZES];
    for (int i = 0; i < SIZES; i++) {
        if (s_made[i].font && s_made[i].size == size) {
            return s_made[i].font;
        }
    }
    for (int i = 0; i < SIZES; i++) {
        if (!s_made[i].font) {
            lv_font_t *f = make_ui_font(size);
            if (!f) {
                return NULL;
            }
            /* Past GB2312, the pixel font has the rest of GBK. */
            f->fallback = size >= 20 ? &boopie_font_pixel_24 : &boopie_font_pixel_12;
            s_made[i].size = size;
            s_made[i].font = f;
            return f;
        }
    }
    return NULL;
}

const lv_font_t *boopie_font_with_cjk(const lv_font_t *base)
{
    static const lv_font_t *s_base[CJK_COPIES];
    static lv_font_t s_copy[CJK_COPIES];
    if (!base || base->fallback || base->dsc == &s_dsc_2x || base->dsc == &s_dsc_1x) {
        return base;
    }
    for (int i = 0; i < CJK_COPIES; i++) {
        if (s_base[i] == base) {
            return &s_copy[i];
        }
        if (!s_base[i]) {
            /* Montserrat's line is about 1.1 of its size; Chinese reads right a touch larger. */
            const lv_font_t *cjk = boopie_font_ui((base->line_height * 100 + 55) / 110 + 1);
            s_copy[i] = *base;
            s_copy[i].fallback = cjk ? cjk : base->line_height >= 20 ? &boopie_font_pixel_24 : &boopie_font_pixel_12;
            s_base[i] = base;
            return &s_copy[i];
        }
    }
    return base;
}
