/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_history.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "boopie_store.h"

#define CLOCK_SET 1700000000LL   /* earlier than this, the clock isn't set yet */
#define CHAT_FILE "log.txt"
#define CHAT_TRIM_AT (BOOPIE_CHAT_MAX + 20)   /* rewrite once it's this long, back to the cap */

static int64_t now_s(void)
{
    int64_t t = (int64_t)time(NULL);
    return t >= CLOCK_SET ? t : 0;
}

static bool path_in(boopie_store_kind_t kind, const char *name, char *out, size_t cap)
{
    const char *dir = boopie_store_dir(kind);
    if (!dir) {
        return false;
    }
    return snprintf(out, cap, "%s/%s", dir, name) < (int)cap;
}

/* ---- chat: one line a turn, "when\tsaid\treply", tabs and newlines escaped ---- */

static int s_chat_lines = -1;   /* lines in the file, counted once */

static void put_escaped(FILE *f, const char *s, size_t max)
{
    for (size_t i = 0; s[i] && i < max; i++) {
        switch (s[i]) {
        case '\n': fputs("\\n", f); break;
        case '\t': fputs("\\t", f); break;
        case '\r': break;
        case '\\': fputs("\\\\", f); break;
        default: fputc(s[i], f);
        }
    }
}

static void unescape(const char *s, size_t n, char *out, size_t cap)
{
    size_t o = 0, i = 0;
    for (; i < n && o + 1 < cap; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < n) {
            c = s[++i];
            c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
        }
        out[o++] = c;
    }
    if (i < n) {
        /* Cut short: don't end inside a UTF-8 character. */
        size_t k = o;
        while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80) {
            k--;
        }
        if (k > 0 && ((unsigned char)out[k - 1] & 0x80)) {
            o = k - 1;
        }
    }
    out[o] = '\0';
}

static int count_lines(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    int n = 0, c;
    while ((c = fgetc(f)) != EOF) {
        n += c == '\n';
    }
    fclose(f);
    return n;
}

/* Keeps the last `keep` lines. */
static void trim(const char *path, int lines, int keep)
{
    char tmp[BOOPIE_ALBUM_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.new", path);
    FILE *in = fopen(path, "r"), *out = fopen(tmp, "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return;
    }
    int skip = lines - keep, c;
    while (skip > 0 && (c = fgetc(in)) != EOF) {
        skip -= c == '\n';
    }
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    rename(tmp, path);
}

bool boopie_chat_add(int64_t when, const char *said, const char *reply)
{
    char path[BOOPIE_ALBUM_PATH_MAX];
    if ((!said || !*said) && (!reply || !*reply)) {
        return false;
    }
    if (!path_in(BOOPIE_STORE_CHAT, CHAT_FILE, path, sizeof path)) {
        return false;
    }
    if (s_chat_lines < 0) {
        s_chat_lines = count_lines(path);
    }
    FILE *f = fopen(path, "a");
    if (!f) {
        return false;
    }
    fprintf(f, "%lld\t", (long long)(when ? when : now_s()));
    put_escaped(f, said ? said : "", BOOPIE_CHAT_SAID_MAX - 1);
    fputc('\t', f);
    put_escaped(f, reply ? reply : "", BOOPIE_CHAT_REPLY_MAX - 1);
    fputc('\n', f);
    bool ok = fclose(f) == 0;
    if (++s_chat_lines >= CHAT_TRIM_AT) {
        trim(path, s_chat_lines, BOOPIE_CHAT_MAX);
        s_chat_lines = BOOPIE_CHAT_MAX;
    }
    return ok;
}

int boopie_chat_count(void)
{
    char path[BOOPIE_ALBUM_PATH_MAX];
    if (!path_in(BOOPIE_STORE_CHAT, CHAT_FILE, path, sizeof path)) {
        return 0;
    }
    int n = count_lines(path);
    return n < BOOPIE_CHAT_MAX ? n : BOOPIE_CHAT_MAX;
}

void boopie_chat_clear(void)
{
    boopie_store_clear(BOOPIE_STORE_CHAT);
    s_chat_lines = 0;
}

int boopie_chat_load(boopie_chat_entry_t *out, int max)
{
    char path[BOOPIE_ALBUM_PATH_MAX];
    if (!path_in(BOOPIE_STORE_CHAT, CHAT_FILE, path, sizeof path)) {
        return 0;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    /* Read oldest first into a ring of the last `max`, then turn it round. */
    size_t cap = 4 * (BOOPIE_CHAT_SAID_MAX + BOOPIE_CHAT_REPLY_MAX) + 32;
    char *line = malloc(cap);
    int n = 0;
    while (line && fgets(line, (int)cap, f)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') {
            line[--len] = '\0';
        }
        char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
        if (!t2) {
            continue;
        }
        boopie_chat_entry_t *e = &out[n % max];
        e->when = strtoll(line, NULL, 10);
        unescape(t1 + 1, t2 - t1 - 1, e->said, sizeof e->said);
        unescape(t2 + 1, line + len - t2 - 1, e->reply, sizeof e->reply);
        n++;
    }
    free(line);
    fclose(f);
    int count = n < max ? n : max;
    /* out holds the last `count` as a ring ending at (n - 1) % max: newest first. */
    boopie_chat_entry_t *tmp = malloc(sizeof *tmp * (size_t)count);
    if (!tmp) {
        return 0;
    }
    for (int i = 0; i < count; i++) {
        tmp[i] = out[(n - 1 - i) % max];
    }
    memcpy(out, tmp, sizeof *tmp * (size_t)count);
    free(tmp);
    return count;
}

/* ---- names in a folder, sorted ---- */

static int cmp_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/* Up to max names ending in `ext` in the kind's folder, sorted (oldest first). */
static int names(boopie_store_kind_t kind, const char *ext, char (*out)[BOOPIE_NOTE_NAME_MAX], int max)
{
    const char *dir = boopie_store_dir(kind);
    DIR *d = dir ? opendir(dir) : NULL;
    if (!d) {
        return 0;
    }
    int n = 0;
    size_t el = strlen(ext);
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        size_t len = strlen(e->d_name);
        if (e->d_name[0] != '.' && len > el && len < BOOPIE_NOTE_NAME_MAX && strcmp(e->d_name + len - el, ext) == 0) {
            memcpy(out[n++], e->d_name, len + 1);
        }
    }
    closedir(d);
    qsort(out, n, BOOPIE_NOTE_NAME_MAX, cmp_names);
    return n;
}

/* A new file's name: the time, or after the newest there if the clock isn't set. */
static void new_name(boopie_store_kind_t kind, const char *ext, char *out)
{
    char have[64][BOOPIE_NOTE_NAME_MAX];
    int n = names(kind, ext, have, 64);
    long long last = n ? strtoll(have[n - 1], NULL, 10) : 0, t = now_s();
    snprintf(out, BOOPIE_NOTE_NAME_MAX, "%010lld%s", t > last ? t : last + 1, ext);
}

/* ---- album ---- */

static char s_album_tmp[BOOPIE_ALBUM_PATH_MAX];

FILE *boopie_album_begin(void)
{
    if (!path_in(BOOPIE_STORE_ALBUM, ".incoming", s_album_tmp, sizeof s_album_tmp)) {
        return NULL;
    }
    return fopen(s_album_tmp, "wb");
}

void boopie_album_end(FILE *f, bool keep)
{
    if (!f) {
        return;
    }
    bool ok = fclose(f) == 0 && keep;
    if (!ok) {
        unlink(s_album_tmp);
        return;
    }
    char name[BOOPIE_NOTE_NAME_MAX], path[BOOPIE_ALBUM_PATH_MAX];
    new_name(BOOPIE_STORE_ALBUM, ".jpg", name);
    if (!path_in(BOOPIE_STORE_ALBUM, name, path, sizeof path) || rename(s_album_tmp, path) != 0) {
        unlink(s_album_tmp);
        return;
    }
    char have[BOOPIE_ALBUM_MAX + 16][BOOPIE_NOTE_NAME_MAX];
    int n = names(BOOPIE_STORE_ALBUM, ".jpg", have, BOOPIE_ALBUM_MAX + 16);
    for (int i = 0; i + BOOPIE_ALBUM_MAX < n; i++) {
        if (path_in(BOOPIE_STORE_ALBUM, have[i], path, sizeof path)) {
            unlink(path);
        }
    }
}

int boopie_album_list(char paths[][BOOPIE_ALBUM_PATH_MAX], int max)
{
    char have[BOOPIE_ALBUM_MAX + 16][BOOPIE_NOTE_NAME_MAX];
    int n = names(BOOPIE_STORE_ALBUM, ".jpg", have, BOOPIE_ALBUM_MAX + 16), count = 0;
    for (int i = n - 1; i >= 0 && count < max; i--) {
        if (path_in(BOOPIE_STORE_ALBUM, have[i], paths[count], BOOPIE_ALBUM_PATH_MAX)) {
            count++;
        }
    }
    return count;
}

/* ---- notes ---- */

bool boopie_notes_save(const int16_t *pcm, size_t frames, char name[BOOPIE_NOTE_NAME_MAX])
{
    char path[BOOPIE_ALBUM_PATH_MAX];
    new_name(BOOPIE_STORE_NOTES, ".pcm", name);
    if (!path_in(BOOPIE_STORE_NOTES, name, path, sizeof path)) {
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(pcm, sizeof *pcm, frames, f) == frames;
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        unlink(path);
    }
    return ok;
}

void boopie_notes_drop(const char *name)
{
    char path[BOOPIE_ALBUM_PATH_MAX];
    if (name && *name && path_in(BOOPIE_STORE_NOTES, name, path, sizeof path)) {
        unlink(path);
    }
}

int64_t boopie_notes_age(const char *name)
{
    long long t = strtoll(name, NULL, 10), now = now_s();
    return t >= CLOCK_SET && now ? now - t : -1;
}

int boopie_notes_list(char names_out[][BOOPIE_NOTE_NAME_MAX], int max)
{
    char have[64][BOOPIE_NOTE_NAME_MAX];
    int n = names(BOOPIE_STORE_NOTES, ".pcm", have, 64), count = 0;
    for (int i = 0; i < n; i++) {
        if (boopie_notes_age(have[i]) > BOOPIE_NOTES_KEEP_S) {
            boopie_notes_drop(have[i]);   /* too old to send now */
        } else if (count < max) {
            memcpy(names_out[count++], have[i], BOOPIE_NOTE_NAME_MAX);
        }
    }
    return count;
}

size_t boopie_notes_frames(const char *name)
{
    char path[BOOPIE_ALBUM_PATH_MAX];
    struct stat st;
    if (!path_in(BOOPIE_STORE_NOTES, name, path, sizeof path) || stat(path, &st) != 0) {
        return 0;
    }
    return (size_t)st.st_size / sizeof(int16_t);
}

bool boopie_notes_read(const char *name, int16_t *pcm, size_t frames)
{
    char path[BOOPIE_ALBUM_PATH_MAX];
    if (!path_in(BOOPIE_STORE_NOTES, name, path, sizeof path)) {
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    bool ok = fread(pcm, sizeof *pcm, frames, f) == frames;
    fclose(f);
    return ok;
}
