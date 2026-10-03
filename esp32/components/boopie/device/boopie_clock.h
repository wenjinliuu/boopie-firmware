/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <time.h>

/*
 * Wall-clock time. The board has no clock chip and the official firmware
 * never sets the time, so this does: from NTP once Wi-Fi is up, and between
 * power-ups from the last time saved in NVS (every 10 minutes and on power
 * off), so the clock carries on, late by however long the board was off,
 * until NTP corrects it. Time zone China (UTC+8) unless set otherwise.
 */

/* Restore the saved time and start NTP when the network is up. Call once,
 * after nvs_flash_init(). */
void boopie_clock_start(void);

/* Keep the current time for the next power-up. */
void boopie_clock_save(void);

/* The time is known: set by NTP this power-up, or restored. */
bool boopie_clock_known(void);

/* NTP has set it this power-up. */
bool boopie_clock_synced(void);

/* Local time now; false (and *out zeroed) while unknown. */
bool boopie_clock_local(struct tm *out);
