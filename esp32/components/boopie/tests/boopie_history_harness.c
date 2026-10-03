/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Drives store/boopie_history.c for test_boopie_history.py, in BOOPIE_DATA:
 *   chat-add N            adds N turns, "said i" / "reply i\nline two\t!"
 *   chat-add-text SAID REPLY
 *   chat                  prints the turns, newest first, as JSON lines
 *   album-add N           keeps N pictures (each "jpeg i")
 *   album-broken          a download that fails
 *   album                 prints the pictures' contents, newest first
 *   notes-add FRAMES      saves a note of FRAMES frames (sample i = i)
 *   notes                 prints each note: name frames ok */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_history.h"
#include "boopie_store.h"

static void json(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') {
            printf("\\%c", *s);
        } else if (*s == '\n') {
            printf("\\n");
        } else if (*s == '\t') {
            printf("\\t");
        } else {
            putchar(*s);
        }
    }
    putchar('"');
}

int main(int argc, char **argv)
{
    if (!boopie_store_mount() || argc < 2) {
        return 2;
    }
    const char *cmd = argv[1];
    if (!strcmp(cmd, "chat-add") && argc > 2) {
        for (int i = 0; i < atoi(argv[2]); i++) {
            char said[32], reply[48];
            snprintf(said, sizeof said, "said %d", i);
            snprintf(reply, sizeof reply, "reply %d\nline two\t!", i);
            if (!boopie_chat_add(1700000000 + i, said, reply)) {
                return 1;
            }
        }
        return 0;
    }
    if (!strcmp(cmd, "chat-add-text") && argc > 3) {
        return boopie_chat_add(0, argv[2], argv[3]) ? 0 : 1;
    }
    if (!strcmp(cmd, "chat")) {
        static boopie_chat_entry_t e[BOOPIE_CHAT_MAX];
        int n = boopie_chat_load(e, BOOPIE_CHAT_MAX);
        for (int i = 0; i < n; i++) {
            printf("{\"when\":%lld,\"said\":", (long long)e[i].when);
            json(e[i].said);
            printf(",\"reply\":");
            json(e[i].reply);
            printf("}\n");
        }
        return boopie_chat_count() == n ? 0 : 3;
    }
    if (!strcmp(cmd, "album-add") && argc > 2) {
        for (int i = 0; i < atoi(argv[2]); i++) {
            FILE *f = boopie_album_begin();
            if (!f) {
                return 1;
            }
            fprintf(f, "jpeg %d", i);
            boopie_album_end(f, true);
        }
        return 0;
    }
    if (!strcmp(cmd, "album-broken")) {
        FILE *f = boopie_album_begin();
        fputs("half", f);
        boopie_album_end(f, false);
        return 0;
    }
    if (!strcmp(cmd, "album")) {
        char paths[BOOPIE_ALBUM_MAX][BOOPIE_ALBUM_PATH_MAX];
        int n = boopie_album_list(paths, BOOPIE_ALBUM_MAX);
        for (int i = 0; i < n; i++) {
            char buf[64] = "";
            FILE *f = fopen(paths[i], "rb");
            if (f) {
                if (!fgets(buf, sizeof buf, f)) {
                    buf[0] = 0;
                }
                fclose(f);
            }
            printf("%s\n", buf);
        }
        return 0;
    }
    if (!strcmp(cmd, "notes-add") && argc > 2) {
        size_t n = (size_t)atoi(argv[2]);
        int16_t *pcm = malloc(n * sizeof *pcm);
        for (size_t i = 0; i < n; i++) {
            pcm[i] = (int16_t)i;
        }
        char name[BOOPIE_NOTE_NAME_MAX];
        bool ok = boopie_notes_save(pcm, n, name);
        free(pcm);
        printf("%s\n", name);
        return ok ? 0 : 1;
    }
    if (!strcmp(cmd, "notes-drop") && argc > 2) {
        boopie_notes_drop(argv[2]);
        return 0;
    }
    if (!strcmp(cmd, "notes")) {
        char names[16][BOOPIE_NOTE_NAME_MAX];
        int n = boopie_notes_list(names, 16);
        for (int i = 0; i < n; i++) {
            size_t frames = boopie_notes_frames(names[i]);
            int16_t *pcm = malloc(frames * sizeof *pcm + 1);
            bool ok = boopie_notes_read(names[i], pcm, frames);
            for (size_t k = 0; ok && k < frames; k++) {
                ok = pcm[k] == (int16_t)k;
            }
            free(pcm);
            printf("%s %zu %s\n", names[i], frames, ok ? "ok" : "bad");
        }
        return 0;
    }
    return 2;
}
