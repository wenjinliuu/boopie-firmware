# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

# Boopie's own code, kept apart from the upstream Muse firmware so upstream
# merges stay clean. Everything but font/boopie_font.c is plain C with no IDF
# dependencies, so tests/ builds it on the host too. avatar/boopie_avatar.c is
# built into the muse component, whose renderer it calls.
set(srcs
    "boopie_expr.c"
    "avatar/boopie_pixel.c"
    "boopie_text.c"
    "font/boopie_pixel_font.c"
    "font/boopie_pixel_glyphs.c")
if(CONFIG_MUSE_ENABLED)
    # LVGL comes from idf_component.yml, on the same boards as Muse's UI.
    list(APPEND srcs "font/boopie_font.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "." "font" "avatar"
)
