/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * 小智 on its official server (docs/boopie-xiaozhi.md). While it's the brain
 * chosen and Wi-Fi is up, the board checks in: unbound, the server gives a
 * six-digit code to show, which the user enters at xiaozhi.me (控制台 › 添加
 * 设备), and the board waits till it's bound; bound, where to talk (kept in
 * NVS for the voice). The firmware the server may offer is never taken.
 * Built into the muse component; any task.
 */

typedef enum {
    BOOPIE_XZ_OFF = 0,        /* Muse is the brain */
    BOOPIE_XZ_NO_NET,         /* waiting on Wi-Fi */
    BOOPIE_XZ_CHECKING,       /* checking in */
    BOOPIE_XZ_CODE,           /* showing a code, waiting to be bound */
    BOOPIE_XZ_READY,          /* bound: where to talk is known */
    BOOPIE_XZ_ERROR,          /* the server couldn't be reached: trying again */
} boopie_xz_state_t;

void boopie_xiaozhi_start(void);

/* How it's going; the code while there is one, and a word for the screen (Chinese). */
boopie_xz_state_t boopie_xiaozhi_status(char *code, size_t code_cap, char *note, size_t note_cap);

/* Check in again now (the settings page's button). */
void boopie_xiaozhi_recheck(void);

/* Where to talk once bound: false before. */
bool boopie_xiaozhi_endpoint(char *url, size_t url_cap, char *token, size_t token_cap);

/* Who the board is to the server: its MAC (Device-Id) and its UUID (Client-Id). */
void boopie_xiaozhi_ids(char *mac, size_t mac_cap, char *uuid, size_t uuid_cap);
