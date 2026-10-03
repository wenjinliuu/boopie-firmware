# Copyright (c) 2026 Boopie contributors
# SPDX-License-Identifier: Apache-2.0

# Boopie's own code, kept apart from the upstream Muse firmware so upstream
# merges stay clean. Plain C with no IDF dependencies, so tests/ builds it on
# the host too.
idf_component_register(
    SRCS "boopie_expr.c"
    INCLUDE_DIRS "."
)
