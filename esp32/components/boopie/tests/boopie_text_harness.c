/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Drives Boopie's text layout for test_boopie_text.py:
 *   page COLS LINES   stdin is a reply: prints it wrapped and paged the way
 *                     the screen shows it (muse_hatch_caption_at), from the top
 *   cols              stdin is text: prints its width in columns
 *   font              checks every glyph of the pixel font against the column
 *                     rule and prints {"glyphs":N,"bad":M,"wide":W}
 *   has CHARS         prints the characters of CHARS the font lacks */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_pixel_font.h"
#include "boopie_text.h"
#include "muse_chat_priv.h"
#include "muse_state.h"

static int s_cols = 16, s_lines = 2;

void muse_state_page(int *cols, int *lines)
{
    *cols = s_cols;
    *lines = s_lines;
}

static char *read_all(void)
{
    static char buf[1 << 16];
    size_t n = fread(buf, 1, sizeof(buf) - 1, stdin);
    buf[n] = '\0';
    return buf;
}

int main(int argc, char **argv)
{
    if (argc > 3 && !strcmp(argv[1], "page")) {
        s_cols = atoi(argv[2]);
        s_lines = atoi(argv[3]);
        char out[2048];
        if (!muse_hatch_caption_at(read_all(), 0, out, sizeof(out))) {
            return 1;
        }
        fputs(out, stdout);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "cols")) {
        printf("%d", boopie_text_line_cols(read_all()));
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "font")) {
        uint32_t bad = 0, wide = 0;
        for (uint32_t i = 0; i < boopie_pixel_glyph_count; i++) {
            int w = boopie_pixel_width((int32_t)i);
            wide += w == BOOPIE_PIXEL_CELL;
            bad += w != boopie_text_cols(boopie_pixel_cps[i]) * BOOPIE_PIXEL_HALF;
            bad += boopie_pixel_find(boopie_pixel_cps[i]) != (int32_t)i;
            bad += i && boopie_pixel_cps[i - 1] >= boopie_pixel_cps[i];
            /* A narrow glyph never reaches the right half of its cell. */
            for (int y = 0; w == BOOPIE_PIXEL_HALF && y < BOOPIE_PIXEL_CELL; y++) {
                for (int x = BOOPIE_PIXEL_HALF; x < BOOPIE_PIXEL_CELL; x++) {
                    bad += boopie_pixel_dot((int32_t)i, x, y);
                }
            }
        }
        printf("{\"glyphs\":%u,\"bad\":%u,\"wide\":%u}", (unsigned)boopie_pixel_glyph_count,
               (unsigned)bad, (unsigned)wide);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "has")) {
        for (const char *p = argv[2]; *p;) {
            size_t len;
            uint32_t cp = boopie_text_decode(p, &len);
            if (boopie_pixel_find(cp) < 0) {
                fwrite(p, 1, len, stdout);
            }
            p += len;
        }
        return 0;
    }
    return 2;
}
