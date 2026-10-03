/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Phone setup over the board's own hotspot (docs/boopie-interaction.md): the
 * radio goes AP+STA, an AP "Boopie-XXXX" with a fresh random password comes
 * up, a DNS server answers every name with the board so the phone pops the
 * page up, and a small web page sets Wi-Fi, the brain, the Muse developer
 * token, the VPN subscription and the pet's name. Long secrets are pasted on the phone and
 * never shown back: the page only says whether they're set.
 *
 * Start and stop from any one task (the UI's); the server runs in its own.
 */

#define BOOPIE_SETUP_URL "http://192.168.4.1/"

typedef struct {
    char ssid[33];
    char pass[16];
} boopie_setup_ap_t;

/* What the page has saved this session, as BOOPIE_SETUP_SAVED_* bits. */
enum {
    BOOPIE_SETUP_SAVED_WIFI = 1 << 0,
    BOOPIE_SETUP_SAVED_BRAIN = 1 << 1,
    BOOPIE_SETUP_SAVED_MUSE = 1 << 2,   /* the developer token: it restarts */
    BOOPIE_SETUP_SAVED_PROXY = 1 << 3,
    BOOPIE_SETUP_SAVED_NAME = 1 << 4,
};

bool boopie_setup_web_start(boopie_setup_ap_t *ap);
void boopie_setup_web_stop(void);
/* Phones joined to the hotspot now. */
int boopie_setup_web_clients(void);
/* Bumped by each save; with the bits saved so far. */
uint32_t boopie_setup_web_saves(uint32_t *saved);
/* Seconds since the last request from a phone (or since the start). */
int boopie_setup_web_idle_s(void);

/* The proxy subscription the page saved (empty if none), for the proxy. */
size_t boopie_setup_subscription(char *out, size_t cap);
