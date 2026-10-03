/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The user data partition (docs/boopie-storage.md): what use leaves behind,
 * one folder each, every one with a cap so the partition never fills. LittleFS
 * on the device (power-loss safe, wear-levelled), mounted at /data; the
 * simulator uses the folder BOOPIE_DATA names (or /tmp/boopie-data).
 */

typedef enum {
    BOOPIE_STORE_CHAT = 0,    /* chat history: the last 500 */
    BOOPIE_STORE_ALBUM,       /* pictures kept: 30 */
    BOOPIE_STORE_NOTES,       /* notes waiting to be sent: gone once sent, or after 7 days */
    BOOPIE_STORE_LOGS,        /* written round in a fixed size */
    BOOPIE_STORE_GAMES,       /* game saves */
    BOOPIE_STORE_COUNT,
} boopie_store_kind_t;

/* Mounts it (formatting it if it's blank or damaged) and makes the folders. */
bool boopie_store_mount(void);
bool boopie_store_ready(void);

/* A kind's folder, "/data/chat"; NULL out of range. */
const char *boopie_store_dir(boopie_store_kind_t kind);
const char *boopie_store_name(boopie_store_kind_t kind);   /* "聊天记录" */

/* Bytes in use (the whole partition, or one kind's files) and the partition's size. */
size_t boopie_store_used(void);
size_t boopie_store_total(void);
size_t boopie_store_kind_bytes(boopie_store_kind_t kind);

/* Deletes every file of a kind. */
bool boopie_store_clear(boopie_store_kind_t kind);

/* A factory reset: everything gone, formatted afresh. */
bool boopie_store_wipe(void);
