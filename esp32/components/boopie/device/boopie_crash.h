/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/* At boot: why the chip last reset and, if a crash was saved, where it was,
 * on the serial console; then the saved crash is cleared, so the next one is
 * news. The addresses decode with addr2line against the same build's ELF. */
void boopie_crash_report(void);
