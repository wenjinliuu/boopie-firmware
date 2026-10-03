/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What use leaves behind in the user data partition (boopie_store.h), each
 * kept to a cap so it never fills (docs/boopie-storage.md):
 *   chat history  the last 100 turns: what was said and the reply
 *   album         the last 10 pictures Muse showed
 *   notes         voice notes waiting for the network: gone once sent, or
 *                 after 7 days
 * Plain C over stdio: any task, and the host tests run it.
 */

/* ---- chat history ---- */

#define BOOPIE_CHAT_MAX 100
#define BOOPIE_CHAT_SAID_MAX 256
#define BOOPIE_CHAT_REPLY_MAX 1024

typedef struct {
    int64_t when;   /* Unix seconds, 0 if the clock wasn't set */
    char said[BOOPIE_CHAT_SAID_MAX];
    char reply[BOOPIE_CHAT_REPLY_MAX];
} boopie_chat_entry_t;

/* A turn: what was said, and the reply (either may be empty, not both). */
bool boopie_chat_add(int64_t when, const char *said, const char *reply);
/* The turns, newest first, up to max; returns how many. */
int boopie_chat_load(boopie_chat_entry_t *out, int max);
int boopie_chat_count(void);
void boopie_chat_clear(void);

/* ---- album ---- */

#define BOOPIE_ALBUM_MAX 10
#define BOOPIE_ALBUM_PATH_MAX 192

/* A picture as it downloads: write it to the file, then end it, kept or not
 * (a broken download isn't). Keeping one past the cap drops the oldest. */
FILE *boopie_album_begin(void);
void boopie_album_end(FILE *f, bool keep);
/* The pictures' paths, newest first; returns how many. */
int boopie_album_list(char paths[][BOOPIE_ALBUM_PATH_MAX], int max);

/* ---- notes waiting to be sent ---- */

#define BOOPIE_NOTES_KEEP_S (7 * 24 * 3600)
#define BOOPIE_NOTE_NAME_MAX 32

/* Saves a note's 16 kHz mono PCM; its name into name. */
bool boopie_notes_save(const int16_t *pcm, size_t frames, char name[BOOPIE_NOTE_NAME_MAX]);
void boopie_notes_drop(const char *name);
/* The notes, oldest first, up to max; those past 7 days are deleted first
 * (once the clock is set). Returns how many. */
int boopie_notes_list(char names[][BOOPIE_NOTE_NAME_MAX], int max);
/* A note's length, and reading it into pcm (room for that many frames);
 * its age in seconds, -1 if unknown. */
size_t boopie_notes_frames(const char *name);
bool boopie_notes_read(const char *name, int16_t *pcm, size_t frames);
int64_t boopie_notes_age(const char *name);

#ifdef __cplusplus
}
#endif
