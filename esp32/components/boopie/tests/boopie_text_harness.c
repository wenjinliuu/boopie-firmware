/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Drives Boopie's text layout for test_boopie_text.py:
 *   page COLS LINES [PX]  stdin is a reply: prints it wrapped and paged the
 *                     way the screen shows it (muse_hatch_caption_at), from
 *                     the top; with PX, measured in the UI font at PX px,
 *                     a column PX / 2 wide, as the reply caption does
 *   cols              stdin is text: prints its width in columns
 *   width PX          stdin is text: prints its width in the UI font at PX px
 *   font              checks the UI font's tables and prints
 *                     {"glyphs":N,"bad":M,"wide":W,"em":E} (W wide characters,
 *                     E of them exactly an em)
 *   advances          prints "cp advance" for every character, in font units
 *   has CHARS         prints the characters of CHARS the font lacks */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_text.h"
#include "boopie_ui_metrics.h"
#include "muse_chat_priv.h"
#include "muse_state.h"

static int s_cols = 16, s_lines = 2;

void muse_state_page(int *cols, int *lines)
{
    *cols = s_cols;
    *lines = s_lines;
}

static int s_px;

static int measure(uint32_t cp)
{
    return boopie_ui_advance(cp, s_px);
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
        if (argc > 4) {
            s_px = atoi(argv[4]);
            boopie_text_set_measure(measure, s_px / 2);
        }
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
    if (argc > 2 && !strcmp(argv[1], "width")) {
        s_px = atoi(argv[2]);
        int w = 0;
        for (const char *p = read_all(); *p && *p != '\n';) {
            size_t len;
            w += boopie_ui_advance(boopie_text_decode(p, &len), s_px);
            p += len;
        }
        printf("%d", w);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "font")) {
        uint32_t bad = 0, wide = 0, em = 0;
        for (uint32_t i = 0; i < boopie_ui_count; i++) {
            bad += i && boopie_ui_cps[i - 1] >= boopie_ui_cps[i];
            bad += !boopie_ui_has(boopie_ui_cps[i]);
            if (boopie_text_cols(boopie_ui_cps[i]) == 2) {
                wide++;
                em += boopie_ui_advance_units[i] == boopie_ui_units_per_em;
            }
        }
        printf("{\"glyphs\":%u,\"bad\":%u,\"wide\":%u,\"em\":%u}", (unsigned)boopie_ui_count,
               (unsigned)bad, (unsigned)wide, (unsigned)em);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "advances")) {
        for (uint32_t i = 0; i < boopie_ui_count; i++) {
            printf("%u %u\n", boopie_ui_cps[i], boopie_ui_advance_units[i]);
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "has")) {
        for (const char *p = argv[2]; *p;) {
            size_t len;
            uint32_t cp = boopie_text_decode(p, &len);
            if (!boopie_ui_has(cp)) {
                fwrite(p, 1, len, stdout);
            }
            p += len;
        }
        return 0;
    }
    return 2;
}
