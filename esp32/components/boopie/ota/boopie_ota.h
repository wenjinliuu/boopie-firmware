/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Boopie's own updates over the air (cloud/ota-worker, .github/workflows/
 * boopie-release.yml). Once a day, and from Settings > 系统更新, the board
 * fetches the release manifest, checks its signature against the public key
 * built in (CONFIG_BOOPIE_OTA_PUBKEY), and offers what's newer; it installs
 * only when asked. The app goes to the other OTA slot, each file checked
 * against its SHA-256 in the manifest; the assets pack (fonts, the pixel
 * font, the small world) waits in the user data partition and is written over
 * the assets partition at the next boot, before anything reads it. A broken
 * assets pack is put right without asking. Downloads go through the VPN when
 * it's on. Muse's own update commands are ignored (CONFIG_HOMEHUB_OTA_ENABLED).
 */

typedef enum {
    BOOPIE_OTA_OFF,           /* no server or key in this build */
    BOOPIE_OTA_IDLE,          /* not checked yet */
    BOOPIE_OTA_CHECKING,
    BOOPIE_OTA_LATEST,        /* nothing newer */
    BOOPIE_OTA_FOUND,         /* a newer version, waiting for a yes */
    BOOPIE_OTA_DOWNLOADING,
    BOOPIE_OTA_RESTARTING,    /* installed: restarting into it */
    BOOPIE_OTA_FAILED,        /* msg says why; checking again works */
} boopie_ota_state_t;

typedef struct {
    boopie_ota_state_t state;
    int percent;              /* while downloading */
    char version[16];         /* the newer version, when found */
    char notes[640];          /* what's new in it */
    char msg[96];             /* a line for the page: what's happening, or why it failed */
} boopie_ota_info_t;

/* At boot, with the user data partition mounted and before the assets are
 * read: writes a downloaded assets pack over the assets partition. */
void boopie_ota_boot(void);

/* Starts the daily check (it waits for Wi-Fi). */
void boopie_ota_start(void);

bool boopie_ota_enabled(void);
void boopie_ota_check(void);
void boopie_ota_install(void);   /* after a yes, with FOUND */
void boopie_ota_info(boopie_ota_info_t *out);

/* The newer version found, once per version, for the pet to mention; NULL
 * otherwise. */
const char *boopie_ota_news(void);

/* The UI calls this every frame; a newly installed app is kept (not rolled
 * back) once the UI has been running a while and Wi-Fi, if set up, is up. */
void boopie_ota_ui_alive(void);
bool boopie_ota_healthy(void);
