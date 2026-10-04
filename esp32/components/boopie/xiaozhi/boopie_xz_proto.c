/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * After 78/xiaozhi-esp32 (main/ota.cc, main/boards/common/board.cc),
 * Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd. and
 * contributors, MIT License.
 */

#include "boopie_xz_proto.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

int boopie_xz_info_json(const boopie_xz_info_t *in, char *out, size_t cap)
{
    /* As the official firmware sends it, but as itself: its own application and
     * board, so the server never offers the official firmware for it. */
    int n = snprintf(out, cap,
                     "{\"version\":2,\"language\":\"zh-CN\",\"flash_size\":%lu,\"minimum_free_heap_size\":\"%lu\","
                     "\"mac_address\":\"%s\",\"uuid\":\"%s\",\"chip_model_name\":\"esp32s3\","
                     "\"application\":{\"name\":\"boopie\",\"version\":\"%s\"},"
                     "\"board\":{\"type\":\"boopie\",\"name\":\"boopie-amoled-1.75c\",\"mac\":\"%s\"}}",
                     (unsigned long)in->flash_size, (unsigned long)in->free_heap, in->mac, in->uuid, in->version,
                     in->mac);
    return n > 0 && (size_t)n < cap ? n : -1;
}

void boopie_xz_uuid(const uint8_t rnd[16], char out[37])
{
    uint8_t b[16];
    memcpy(b, rnd, 16);
    b[6] = (uint8_t)((b[6] & 0x0f) | 0x40);   /* version 4 */
    b[8] = (uint8_t)((b[8] & 0x3f) | 0x80);   /* variant 10 */
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3],
             b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

static void copy_str(const cJSON *obj, const char *key, char *out, size_t cap, bool *has)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v) && v->valuestring) {
        snprintf(out, cap, "%s", v->valuestring);
        if (has) {
            *has = true;
        }
    }
}

bool boopie_xz_parse_ota(const char *json, boopie_xz_ota_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *act = cJSON_GetObjectItemCaseSensitive(root, "activation");
    if (cJSON_IsObject(act)) {
        copy_str(act, "message", out->message, sizeof out->message, NULL);
        copy_str(act, "code", out->code, sizeof out->code, &out->has_code);
        copy_str(act, "challenge", out->challenge, sizeof out->challenge, &out->has_challenge);
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(act, "timeout_ms");
        if (cJSON_IsNumber(t)) {
            out->timeout_ms = t->valueint;
        }
    }
    const cJSON *ws = cJSON_GetObjectItemCaseSensitive(root, "websocket");
    if (cJSON_IsObject(ws)) {
        bool url = false;
        copy_str(ws, "url", out->ws_url, sizeof out->ws_url, &url);
        copy_str(ws, "token", out->ws_token, sizeof out->ws_token, NULL);
        out->has_websocket = url;
    }
    const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "server_time");
    if (cJSON_IsObject(st)) {
        const cJSON *ts = cJSON_GetObjectItemCaseSensitive(st, "timestamp");
        if (cJSON_IsNumber(ts) && ts->valuedouble > 1.6e12) {   /* milliseconds, and sane */
            out->time_ms = (int64_t)ts->valuedouble;
            out->has_time = true;
        }
    }
    out->offered_firmware = cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root, "firmware"));
    cJSON_Delete(root);
    return true;
}

/* ---- the conversation ---- */

int boopie_xz_hello_json(char *out, size_t cap)
{
    int n = snprintf(out, cap,
                     "{\"type\":\"hello\",\"version\":1,\"features\":{\"mcp\":true},\"transport\":\"websocket\","
                     "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60}}");
    return n > 0 && (size_t)n < cap ? n : -1;
}

/* A string as JSON, quoted and escaped, into out; its length or -1. */
static int json_str(const char *s, char *out, size_t cap)
{
    cJSON *v = cJSON_CreateString(s ? s : "");
    char *p = v ? cJSON_PrintUnformatted(v) : NULL;
    int n = p ? snprintf(out, cap, "%s", p) : -1;
    cJSON_free(p);
    cJSON_Delete(v);
    return n > 0 && (size_t)n < cap ? n : -1;
}

int boopie_xz_listen_json(const char *session, const char *state, char *out, size_t cap)
{
    char sid[BOOPIE_XZ_SESSION_MAX + 8];
    if (json_str(session, sid, sizeof sid) < 0) {
        return -1;
    }
    int n = snprintf(out, cap, "{\"session_id\":%s,\"type\":\"listen\",\"state\":\"%s\",\"mode\":\"manual\"}", sid,
                     state);
    return n > 0 && (size_t)n < cap ? n : -1;
}

int boopie_xz_abort_json(const char *session, char *out, size_t cap)
{
    char sid[BOOPIE_XZ_SESSION_MAX + 8];
    if (json_str(session, sid, sizeof sid) < 0) {
        return -1;
    }
    int n = snprintf(out, cap, "{\"session_id\":%s,\"type\":\"abort\"}", sid);
    return n > 0 && (size_t)n < cap ? n : -1;
}

bool boopie_xz_parse_msg(const char *json, boopie_xz_msg_t *out)
{
    memset(out, 0, sizeof *out);
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    copy_str(root, "session_id", out->session, sizeof out->session, NULL);
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const char *t = cJSON_IsString(type) ? type->valuestring : "";
    if (!strcmp(t, "hello")) {
        out->type = BOOPIE_XZ_MSG_HELLO;
        const cJSON *ap = cJSON_GetObjectItemCaseSensitive(root, "audio_params");
        const cJSON *sr = cJSON_IsObject(ap) ? cJSON_GetObjectItemCaseSensitive(ap, "sample_rate") : NULL;
        const cJSON *fd = cJSON_IsObject(ap) ? cJSON_GetObjectItemCaseSensitive(ap, "frame_duration") : NULL;
        out->sample_rate = cJSON_IsNumber(sr) ? sr->valueint : 0;
        out->frame_ms = cJSON_IsNumber(fd) ? fd->valueint : 0;
    } else if (!strcmp(t, "stt")) {
        out->type = BOOPIE_XZ_MSG_STT;
        copy_str(root, "text", out->text, sizeof out->text, NULL);
    } else if (!strcmp(t, "llm")) {
        out->type = BOOPIE_XZ_MSG_LLM;
        copy_str(root, "emotion", out->text, sizeof out->text, NULL);
    } else if (!strcmp(t, "tts")) {
        const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "state");
        const char *s = cJSON_IsString(st) ? st->valuestring : "";
        out->type = !strcmp(s, "start")            ? BOOPIE_XZ_MSG_TTS_START
                    : !strcmp(s, "stop")           ? BOOPIE_XZ_MSG_TTS_STOP
                    : !strcmp(s, "sentence_start") ? BOOPIE_XZ_MSG_TTS_SENTENCE
                                                   : BOOPIE_XZ_MSG_OTHER;
        copy_str(root, "text", out->text, sizeof out->text, NULL);
    } else if (!strcmp(t, "mcp")) {
        out->type = BOOPIE_XZ_MSG_MCP;
    }
    cJSON_Delete(root);
    return true;
}

/* {"session_id":...,"type":"mcp","payload":<payload>} into out, taking payload. */
static int mcp_wrap(const char *session, cJSON *payload, char *out, size_t cap)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "session_id", session ? session : "");
    cJSON_AddStringToObject(msg, "type", "mcp");
    cJSON_AddItemToObject(msg, "payload", payload);
    char *p = cJSON_PrintUnformatted(msg);
    int n = p ? snprintf(out, cap, "%s", p) : -1;
    cJSON_free(p);
    cJSON_Delete(msg);
    return n > 0 && (size_t)n < cap ? n : -1;
}

static cJSON *rpc(const cJSON *id)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    cJSON_AddItemToObject(r, "id", cJSON_Duplicate(id, true));
    return r;
}

int boopie_xz_mcp_reply(const char *json, const char *session, const char *tools_json, char *out, size_t cap,
                        char *call_name, size_t name_cap, char *call_args, size_t args_cap, int *call_id)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *pl = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "payload") : NULL;
    const cJSON *method = cJSON_IsObject(pl) ? cJSON_GetObjectItemCaseSensitive(pl, "method") : NULL;
    const cJSON *id = cJSON_IsObject(pl) ? cJSON_GetObjectItemCaseSensitive(pl, "id") : NULL;
    if (!cJSON_IsString(method) || !cJSON_IsNumber(id)) {
        cJSON_Delete(root);
        return -1;   /* a notification, or not a request */
    }
    const char *m = method->valuestring;
    cJSON *r = rpc(id);
    int n;
    if (!strcmp(m, "initialize")) {
        cJSON *res = cJSON_AddObjectToObject(r, "result");
        cJSON_AddStringToObject(res, "protocolVersion", "2024-11-05");
        cJSON_AddObjectToObject(cJSON_AddObjectToObject(res, "capabilities"), "tools");
        cJSON *info = cJSON_AddObjectToObject(res, "serverInfo");
        cJSON_AddStringToObject(info, "name", "boopie");
        cJSON_AddStringToObject(info, "version", "1");
        n = mcp_wrap(session, r, out, cap);
    } else if (!strcmp(m, "tools/list")) {
        cJSON *res = cJSON_AddObjectToObject(r, "result");
        cJSON *tools = cJSON_Parse(tools_json ? tools_json : "[]");
        cJSON_AddItemToObject(res, "tools", cJSON_IsArray(tools) ? tools : cJSON_CreateArray());
        if (tools && !cJSON_IsArray(tools)) {
            cJSON_Delete(tools);
        }
        n = mcp_wrap(session, r, out, cap);
    } else if (!strcmp(m, "tools/call")) {
        const cJSON *params = cJSON_GetObjectItemCaseSensitive(pl, "params");
        const cJSON *name = cJSON_IsObject(params) ? cJSON_GetObjectItemCaseSensitive(params, "name") : NULL;
        const cJSON *args = cJSON_IsObject(params) ? cJSON_GetObjectItemCaseSensitive(params, "arguments") : NULL;
        cJSON_Delete(r);
        if (!cJSON_IsString(name)) {
            cJSON_Delete(root);
            return -1;
        }
        snprintf(call_name, name_cap, "%s", name->valuestring);
        char *a = args ? cJSON_PrintUnformatted(args) : NULL;
        snprintf(call_args, args_cap, "%s", a ? a : "{}");
        cJSON_free(a);
        *call_id = id->valueint;
        cJSON_Delete(root);
        return -2;
    } else {
        cJSON *err = cJSON_AddObjectToObject(r, "error");
        cJSON_AddNumberToObject(err, "code", -32601);
        cJSON_AddStringToObject(err, "message", "Method not found");
        n = mcp_wrap(session, r, out, cap);
    }
    cJSON_Delete(root);
    return n;
}

int boopie_xz_mcp_result(const char *session, int id, const char *text, bool is_error, char *out, size_t cap)
{
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(r, "id", id);
    cJSON *res = cJSON_AddObjectToObject(r, "result");
    cJSON *content = cJSON_AddArrayToObject(res, "content");
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text ? text : "");
    cJSON_AddItemToArray(content, item);
    cJSON_AddBoolToObject(res, "isError", is_error);
    return mcp_wrap(session, r, out, cap);
}

boopie_xz_mood_t boopie_xz_mood(const char *emotion)
{
    /* The server's emotions (as upstream main/boards/espressif/esp-hi/emoji_display.cc names them). */
    static const char *const happy[] = { "happy", "laughing", "funny", "loving", "winking", "cool", "delicious",
                                         "kissy", "confident", "silly", "relaxed" };
    static const char *const sad[] = { "sad", "crying", "angry", "embarrassed" };
    static const char *const surprised[] = { "surprised", "shocked", "confused", "thinking" };
    if (!emotion) {
        return BOOPIE_XZ_MOOD_NONE;
    }
    for (size_t i = 0; i < sizeof happy / sizeof *happy; i++) {
        if (!strcmp(emotion, happy[i])) {
            return BOOPIE_XZ_MOOD_HAPPY;
        }
    }
    for (size_t i = 0; i < sizeof sad / sizeof *sad; i++) {
        if (!strcmp(emotion, sad[i])) {
            return BOOPIE_XZ_MOOD_SAD;
        }
    }
    for (size_t i = 0; i < sizeof surprised / sizeof *surprised; i++) {
        if (!strcmp(emotion, surprised[i])) {
            return BOOPIE_XZ_MOOD_SURPRISED;
        }
    }
    return !strcmp(emotion, "sleepy") ? BOOPIE_XZ_MOOD_SLEEPY : BOOPIE_XZ_MOOD_NONE;
}
