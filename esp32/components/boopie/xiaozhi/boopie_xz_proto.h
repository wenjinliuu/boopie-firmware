/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 小智's official server, the protocol's plain parts (docs/boopie-xiaozhi.md),
 * after 78/xiaozhi-esp32 (MIT): what the device says about itself when it
 * checks in, and what the check-in's answer holds. Plain C (cJSON).
 */

#define BOOPIE_XZ_OTA_URL "https://api.tenclass.net/xiaozhi/ota/"
#define BOOPIE_XZ_CODE_MAX 16
#define BOOPIE_XZ_URL_MAX 160
#define BOOPIE_XZ_TOKEN_MAX 192
#define BOOPIE_XZ_MSG_MAX 160

typedef struct {
    const char *mac;          /* "aa:bb:cc:dd:ee:ff" */
    const char *uuid;         /* the device's own, kept in NVS */
    const char *version;      /* the firmware's */
    uint32_t flash_size;
    uint32_t free_heap;
} boopie_xz_info_t;

/* The check-in's body, JSON, into out; its length, or -1 if it didn't fit. */
int boopie_xz_info_json(const boopie_xz_info_t *info, char *out, size_t cap);

/* A UUID v4 from 16 random bytes: "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx". */
void boopie_xz_uuid(const uint8_t rnd[16], char out[37]);

typedef struct {
    bool has_code;            /* not bound yet: show this code */
    char code[BOOPIE_XZ_CODE_MAX];
    char message[BOOPIE_XZ_MSG_MAX];
    bool has_challenge;       /* ... and wait on activate with it */
    char challenge[64];
    int timeout_ms;           /* how long the code holds (0: not said) */
    bool has_websocket;       /* where to talk, once bound */
    char ws_url[BOOPIE_XZ_URL_MAX];
    char ws_token[BOOPIE_XZ_TOKEN_MAX];
    bool has_time;
    int64_t time_ms;          /* the server's clock, epoch milliseconds (UTC) */
    bool offered_firmware;    /* it offered its own firmware: never taken */
} boopie_xz_ota_t;

/* The check-in's answer; false if it isn't JSON. */
bool boopie_xz_parse_ota(const char *json, boopie_xz_ota_t *out);

/* ---- the conversation, over the WebSocket (docs/websocket_zh.md upstream) ---- */

#define BOOPIE_XZ_SESSION_MAX 64
#define BOOPIE_XZ_TEXT_MAX 192

/* The device's hello: Opus, 16 kHz mono, 60 ms frames, MCP on. */
int boopie_xz_hello_json(char *out, size_t cap);
/* Listening starts or stops ("start" / "stop"), manual: the button says when. */
int boopie_xz_listen_json(const char *session, const char *state, char *out, size_t cap);
/* Stop speaking. */
int boopie_xz_abort_json(const char *session, char *out, size_t cap);

typedef enum {
    BOOPIE_XZ_MSG_OTHER = 0,
    BOOPIE_XZ_MSG_HELLO,        /* the server's: session, its audio */
    BOOPIE_XZ_MSG_STT,          /* what it heard */
    BOOPIE_XZ_MSG_LLM,          /* an emotion for the face */
    BOOPIE_XZ_MSG_TTS_START,
    BOOPIE_XZ_MSG_TTS_SENTENCE, /* the sentence being said */
    BOOPIE_XZ_MSG_TTS_STOP,
    BOOPIE_XZ_MSG_MCP,          /* JSON-RPC for the device's tools */
} boopie_xz_msg_type_t;

typedef struct {
    boopie_xz_msg_type_t type;
    char session[BOOPIE_XZ_SESSION_MAX];
    char text[BOOPIE_XZ_TEXT_MAX];   /* STT, TTS_SENTENCE; LLM: the emotion */
    int sample_rate;                 /* HELLO: the server's audio (0: not said) */
    int frame_ms;
} boopie_xz_msg_t;

/* A text message from the server; false if it isn't JSON. */
bool boopie_xz_parse_msg(const char *json, boopie_xz_msg_t *out);

/*
 * The device's answer to an MCP message (the whole {"type":"mcp",...} the
 * server sent), into out: initialize, tools/list (the tools in tools_json, a
 * JSON array, "[]" for none), anything else an error. A tools/call gets back
 * -2 with its name and arguments (JSON) for the caller to run, which then
 * answers with boopie_xz_mcp_result. -1 for a notification (nothing to say)
 * or what can't be read.
 */
int boopie_xz_mcp_reply(const char *json, const char *session, const char *tools_json, char *out, size_t cap,
                        char *call_name, size_t name_cap, char *call_args, size_t args_cap, int *call_id);
int boopie_xz_mcp_result(const char *session, int id, const char *text, bool is_error, char *out, size_t cap);

/* What the reply's emotion (llm's "emotion") means for the pet's face. */
typedef enum {
    BOOPIE_XZ_MOOD_NONE = 0,
    BOOPIE_XZ_MOOD_HAPPY,
    BOOPIE_XZ_MOOD_SAD,
    BOOPIE_XZ_MOOD_SLEEPY,
    BOOPIE_XZ_MOOD_SURPRISED,
} boopie_xz_mood_t;

boopie_xz_mood_t boopie_xz_mood(const char *emotion);
