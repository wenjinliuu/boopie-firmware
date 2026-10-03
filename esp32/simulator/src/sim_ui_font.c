/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* The UI font, linked in as the firmware's EMBED_FILES links it
 * (font/boopie_font.c): _binary_ui_otf_start .. _end. */
__asm__(".section .rodata\n"
        ".global _binary_ui_otf_start\n"
        "_binary_ui_otf_start:\n"
        ".incbin \"" BOOPIE_UI_FONT "\"\n"
        ".global _binary_ui_otf_end\n"
        "_binary_ui_otf_end:\n"
        ".byte 0\n"
        ".previous\n");
