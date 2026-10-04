/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 小智's conversation (boopie_xz_voice.h). Three tasks meet here:
 *   - the voice task: begins, feeds and ends turns, and reads the reply;
 *   - the WebSocket's own task: what the server says, sorted as it comes;
 *   - "boopie_xz_codec" (its stack in PSRAM, as Opus wants a big one): Opus
 *     both ways, the connection, and everything sent.
 * Between them, buffers each written by one task and read by one other:
 *   speech in (PCM, voice → codec), the outbox (codec only, held until the
 *   server's hello), the reply (tagged messages, socket → codec: 'A' Opus,
 *   'S' a sentence, 'E' the end), the reply's PCM (codec → voice), events.
 * A turn's state says which of the server's words still count: a cancelled
 * turn's are dropped wherever they are.
 */

#include "boopie_xz_voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boopie_avatar.h"
#include "boopie_xiaozhi.h"
#include "boopie_xz_proto.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_opus_dec.h"
#include "esp_opus_enc.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/message_buffer.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "muse_wifi.h"

static const char *TAG = "boopie_xz_voice";

#define RATE 16000
#define FRAME 960                       /* 60 ms at 16 kHz: one Opus packet up */
#define DEC_MAX (RATE * 120 / 1000)     /* the longest packet down, 120 ms */
#define PCM_IN_BYTES (RATE * 2 * 3)     /* 3 s of speech not yet encoded */
#define OUTBOX_BYTES (64 * 1024)        /* ~10 s of packets while connecting */
#define DOWN_BYTES (96 * 1024)          /* the reply, still Opus */
#define PCM_OUT_BYTES (RATE * 2 * 4)    /* 4 s of the reply, decoded */
#define TEXT_MAX 8192                   /* the longest text message read */
#define PKT_MAX 4096                    /* the longest packet down */
#define SENTENCES 16
#define HELLO_US (10 * 1000000LL)       /* pressed and no hello: give up */
#define DEAF_US (10 * 1000000LL)        /* released and nothing heard */
#define MUTE_US (20 * 1000000LL)        /* heard, then silence */
#define IDLE_CLOSE_US (90 * 1000000LL)  /* nothing said: close */

typedef enum { T_IDLE = 0, T_LISTEN, T_WAIT } turn_t;
typedef enum { CMD_BEGIN, CMD_END, CMD_CANCEL } cmd_t;

typedef struct {
    muse_hatch_ev_t ev;
    char text[96];
} event_t;

typedef struct {
    size_t at;   /* reply frames decoded before it */
    char text[BOOPIE_XZ_TEXT_MAX];
} sentence_t;

static TaskHandle_t s_codec;
static QueueHandle_t s_cmds, s_events, s_mcp;
static StreamBufferHandle_t s_pcm_in, s_pcm_out;
static MessageBufferHandle_t s_outbox, s_down;
static SemaphoreHandle_t s_lock;   /* the session, the sentences, s_down's writes and resets */

static volatile turn_t s_turn;
static volatile bool s_heard, s_spoke;   /* since the release: it heard something, it began to answer */
static volatile int64_t s_begin_us, s_last_rx_us, s_last_use_us;

static esp_websocket_client_handle_t s_client;
static volatile bool s_connected, s_dead, s_say_hello;
static volatile bool s_closing;   /* our own close: not news for the turn */
static char s_session[BOOPIE_XZ_SESSION_MAX];
static volatile bool s_hello;
static volatile int s_frame_ms = 60;
static volatile boopie_xz_mood_t s_mood;

static sentence_t s_sent[SENTENCES];
static int s_sent_n;
static size_t s_out_total;   /* reply frames decoded this turn */

/* The socket task's: a message as it arrives in pieces. */
static char *s_txt, *s_bin;

static void push_event(muse_hatch_ev_t ev, const char *text)
{
    event_t e = { .ev = ev };
    snprintf(e.text, sizeof e.text, "%s", text ? text : "");
    if (xQueueSend(s_events, &e, 0) != pdTRUE) {
        ESP_LOGW(TAG, "events full");
    }
}

/* Into s_down, tagged; dropped if there's no room (the reply runs ahead of playing). */
static void down_put(char tag, const void *data, size_t len)
{
    static uint8_t buf[1 + PKT_MAX];
    if (len > PKT_MAX) {
        return;
    }
    buf[0] = (uint8_t)tag;
    memcpy(buf + 1, data, len);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (xMessageBufferSend(s_down, buf, len + 1, 0) == 0) {
        ESP_LOGW(TAG, "reply backlog full: dropped");
    }
    xSemaphoreGive(s_lock);
}

/* ---- what the server says (the WebSocket's task) ---- */

static void on_text(const char *json)
{
    boopie_xz_msg_t *m = heap_caps_malloc(sizeof *m, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!m || !boopie_xz_parse_msg(json, m)) {
        free(m);
        return;
    }
    s_last_rx_us = esp_timer_get_time();
    switch (m->type) {
    case BOOPIE_XZ_MSG_HELLO:
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_session, sizeof s_session, "%s", m->session);
        xSemaphoreGive(s_lock);
        if (m->frame_ms) {
            s_frame_ms = m->frame_ms;
        }
        s_hello = true;
        ESP_LOGI(TAG, "hello: reply audio %d Hz, %d ms", m->sample_rate, m->frame_ms);
        break;
    case BOOPIE_XZ_MSG_STT:
        if (s_turn != T_IDLE && m->text[0]) {
            s_heard = true;
            push_event(MUSE_HATCH_EV_HEARD, m->text);
        }
        break;
    case BOOPIE_XZ_MSG_LLM:
        if (s_turn == T_WAIT) {
            s_mood = boopie_xz_mood(m->text);
        }
        break;
    case BOOPIE_XZ_MSG_TTS_START:
        if (s_turn == T_WAIT) {
            s_spoke = true;
        }
        break;
    case BOOPIE_XZ_MSG_TTS_SENTENCE:
        if (s_turn == T_WAIT && m->text[0]) {
            s_spoke = true;
            down_put('S', m->text, strlen(m->text) + 1);
        }
        break;
    case BOOPIE_XZ_MSG_TTS_STOP:
        /* A stop before anything of this turn is the cancelled one's. */
        if (s_turn == T_WAIT && (s_spoke || s_heard)) {
            down_put('E', "", 0);
        }
        break;
    case BOOPIE_XZ_MSG_MCP: {
        char *copy = strdup(json);
        if (copy && xQueueSend(s_mcp, &copy, 0) != pdTRUE) {
            free(copy);
        }
        break;
    }
    default:
        break;   /* "system" and the rest: never obeyed */
    }
    free(m);
}

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_websocket_event_data_t *d = data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        s_say_hello = true;
        xTaskNotifyGive(s_codec);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
        if (s_closing) {
            break;
        }
        if (!s_dead) {
            ESP_LOGW(TAG, "connection %s", id == WEBSOCKET_EVENT_ERROR ? "failed" : "closed");
        }
        s_connected = s_hello = false;
        s_dead = true;
        if (s_turn != T_IDLE) {
            push_event(MUSE_HATCH_EV_ERROR, "和小智断开了，再说一次");
            s_turn = T_IDLE;
        }
        break;
    case WEBSOCKET_EVENT_DATA: {
        bool text = d->op_code == 0x1, bin = d->op_code == 0x2;
        if (d->op_code == 0x0 || (!text && !bin) || d->payload_len <= 0) {
            break;   /* control frames; continuations come with the first's op code */
        }
        char *buf = text ? s_txt : s_bin;
        size_t cap = text ? TEXT_MAX - 1 : PKT_MAX;
        if ((size_t)d->payload_len > cap || d->payload_offset + d->data_len > d->payload_len) {
            ESP_LOGW(TAG, "a %d-byte message: too long, dropped", d->payload_len);
            break;
        }
        memcpy(buf + d->payload_offset, d->data_ptr, d->data_len);
        if (d->payload_offset + d->data_len < d->payload_len) {
            break;   /* more to come */
        }
        if (text) {
            buf[d->payload_len] = '\0';
            on_text(buf);
        } else if (s_turn == T_WAIT) {
            s_last_rx_us = esp_timer_get_time();
            down_put('A', buf, d->payload_len);
        }
        break;
    }
    default:
        break;
    }
}

/* ---- the codec task ---- */

static void close_ws(void)
{
    if (s_client) {
        s_closing = true;
        esp_websocket_client_destroy(s_client);   /* stops it first */
        s_client = NULL;
        s_closing = false;
    }
    s_connected = s_hello = s_say_hello = false;
    s_dead = false;
}

/* Opens the connection if it isn't open or opening; false if there's nowhere to talk. */
static bool ensure_ws(void)
{
    if (s_client && !s_dead) {
        return true;
    }
    close_ws();
    static char url[BOOPIE_XZ_URL_MAX], token[BOOPIE_XZ_TOKEN_MAX];
    static char headers[BOOPIE_XZ_TOKEN_MAX + 160];
    if (!boopie_xiaozhi_endpoint(url, sizeof url, token, sizeof token)) {
        return false;
    }
    char mac[18], uuid[37];
    boopie_xiaozhi_ids(mac, sizeof mac, uuid, sizeof uuid);
    /* As upstream: a token without a scheme is a bearer token. */
    snprintf(headers, sizeof headers, "Authorization: %s%s\r\nProtocol-Version: 1\r\nDevice-Id: %s\r\nClient-Id: %s\r\n",
             strchr(token, ' ') ? "" : "Bearer ", token, mac, uuid);
    memset(token, 0, sizeof token);
    esp_websocket_client_config_t cfg = {
        .uri = url,
        .headers = headers,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_reconnect = true,
        .buffer_size = 2048,
        .task_stack = 6144,
        .task_prio = 5,
        .task_name = "boopie_xz_ws",
        .network_timeout_ms = 10000,
    };
    s_client = esp_websocket_client_init(&cfg);
    memset(headers, 0, sizeof headers);
    if (!s_client) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_session[0] = '\0';
    xSemaphoreGive(s_lock);
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    if (esp_websocket_client_start(s_client) != ESP_OK) {
        close_ws();
        return false;
    }
    ESP_LOGI(TAG, "connecting");
    return true;
}

static void send_text(const char *json, int len)
{
    if (s_client && s_connected && esp_websocket_client_send_text(s_client, json, len, pdMS_TO_TICKS(1000)) < 0) {
        ESP_LOGW(TAG, "send failed");
    }
}

static void session(char *out, size_t cap)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(out, cap, "%s", s_session);
    xSemaphoreGive(s_lock);
}

/* Sends what's waiting once the server has said hello: listen start, speech, listen stop. */
static void flush_outbox(void)
{
    static uint8_t item[1 + PKT_MAX];
    char sid[BOOPIE_XZ_SESSION_MAX], json[160];
    while (s_hello && s_connected) {
        size_t n = xMessageBufferReceive(s_outbox, item, sizeof item, 0);
        if (!n) {
            return;
        }
        if (item[0] == 'A') {
            if (esp_websocket_client_send_bin(s_client, (const char *)item + 1, n - 1, pdMS_TO_TICKS(1000)) < 0) {
                ESP_LOGW(TAG, "speech not sent");
            }
            continue;
        }
        session(sid, sizeof sid);
        int len = boopie_xz_listen_json(sid, item[0] == 'L' ? "start" : "stop", json, sizeof json);
        if (len > 0) {
            send_text(json, len);
        }
    }
}

static void outbox_put(char tag, const void *data, size_t len)
{
    static uint8_t buf[1 + PKT_MAX];
    buf[0] = (uint8_t)tag;
    memcpy(buf + 1, data, len);
    if (xMessageBufferSend(s_outbox, buf, len + 1, 0) == 0) {
        ESP_LOGW(TAG, "outbox full: speech dropped");
    }
}

static void *s_enc, *s_dec;
static int16_t *s_frame;   /* speech gathering into a packet */
static size_t s_frame_n;
static uint8_t *s_packet;
static int s_packet_cap;
static volatile bool s_end_pending;

static void encode(void)
{
    if (!s_enc) {
        esp_opus_enc_config_t cfg = {
            .sample_rate = RATE,
            .channel = ESP_AUDIO_MONO,
            .bits_per_sample = 16,
            .bitrate = ESP_OPUS_BITRATE_AUTO,
            .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
            .application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO,
            .complexity = 0,
            .enable_fec = false,
            .enable_dtx = true,
            .enable_vbr = true,
        };   /* as upstream's */
        int in_size = 0;
        if (esp_opus_enc_open(&cfg, sizeof cfg, &s_enc) != ESP_AUDIO_ERR_OK
            || esp_opus_enc_get_frame_size(s_enc, &in_size, &s_packet_cap) != ESP_AUDIO_ERR_OK
            || in_size != FRAME * 2) {
            ESP_LOGE(TAG, "Opus encoder: no");
            return;
        }
        s_packet = heap_caps_malloc(s_packet_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    for (;;) {
        s_frame_n += xStreamBufferReceive(s_pcm_in, s_frame + s_frame_n, (FRAME - s_frame_n) * 2, 0) / 2;
        bool last = s_end_pending && xStreamBufferIsEmpty(s_pcm_in);
        if (s_frame_n < FRAME && !(last && s_frame_n)) {
            break;
        }
        memset(s_frame + s_frame_n, 0, (FRAME - s_frame_n) * 2);   /* the last one, padded */
        esp_audio_enc_in_frame_t in = { .buffer = (uint8_t *)s_frame, .len = FRAME * 2 };
        esp_audio_enc_out_frame_t out = { .buffer = s_packet, .len = s_packet_cap };
        if (s_packet && esp_opus_enc_process(s_enc, &in, &out) == ESP_AUDIO_ERR_OK && out.encoded_bytes) {
            outbox_put('A', s_packet, out.encoded_bytes);
        }
        s_frame_n = 0;
    }
    if (s_end_pending && xStreamBufferIsEmpty(s_pcm_in)) {
        s_end_pending = false;
        outbox_put('P', NULL, 0);
    }
}

static int16_t *s_pcm;   /* one decoded packet, waiting for room */
static size_t s_pcm_n, s_pcm_at;
static bool s_stuck;   /* s_pcm still to go */

static void decode(void)
{
    static uint8_t item[1 + PKT_MAX];
    if (!s_dec) {
        int ms = s_frame_ms;
        esp_opus_dec_cfg_t cfg = {
            .sample_rate = RATE,   /* Opus decodes to any of its rates: no resampling */
            .channel = ESP_AUDIO_MONO,
            .frame_duration = ms == 20   ? ESP_OPUS_DEC_FRAME_DURATION_20_MS
                              : ms == 40  ? ESP_OPUS_DEC_FRAME_DURATION_40_MS
                              : ms == 120 ? ESP_OPUS_DEC_FRAME_DURATION_120_MS
                                          : ESP_OPUS_DEC_FRAME_DURATION_60_MS,
            .self_delimited = false,
        };
        if (esp_opus_dec_open(&cfg, sizeof cfg, &s_dec) != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "Opus decoder: no");
            s_dec = NULL;
            return;
        }
    }
    for (;;) {
        if (s_stuck) {
            size_t left = (s_pcm_n - s_pcm_at) * 2;
            size_t put = s_turn == T_WAIT ? xStreamBufferSend(s_pcm_out, s_pcm + s_pcm_at, left, 0) : left;
            s_pcm_at += put / 2;
            if (s_pcm_at < s_pcm_n) {
                return;   /* the voice task is behind: the rest next time */
            }
            s_stuck = false;
        }
        size_t n = xMessageBufferReceive(s_down, item, sizeof item, 0);
        if (!n || s_turn != T_WAIT) {
            return;
        }
        if (item[0] == 'A') {
            esp_audio_dec_in_raw_t raw = { .buffer = item + 1, .len = n - 1 };
            esp_audio_dec_out_frame_t frame = { .buffer = (uint8_t *)s_pcm, .len = DEC_MAX * 2 };
            esp_audio_dec_info_t info;
            if (esp_opus_dec_decode(s_dec, &raw, &frame, &info) == ESP_AUDIO_ERR_OK && frame.decoded_size) {
                s_pcm_n = frame.decoded_size / 2;
                s_pcm_at = 0;
                s_stuck = true;
                s_out_total += s_pcm_n;
            }
        } else if (item[0] == 'S') {
            item[n - 1] = '\0';
            xSemaphoreTake(s_lock, portMAX_DELAY);
            sentence_t *st = &s_sent[s_sent_n++ % SENTENCES];
            st->at = s_out_total;
            strlcpy(st->text, (const char *)item + 1, sizeof st->text);   /* already kept to this by the parse */
            xSemaphoreGive(s_lock);
            push_event(MUSE_HATCH_EV_REPLY, (const char *)item + 1);
        } else if (item[0] == 'E') {
            push_event(MUSE_HATCH_EV_DONE, NULL);
            static const boopie_expr_t faces[] = {
                [BOOPIE_XZ_MOOD_HAPPY] = BOOPIE_EXPR_HAPPY,
                [BOOPIE_XZ_MOOD_SAD] = BOOPIE_EXPR_SAD,
                [BOOPIE_XZ_MOOD_SLEEPY] = BOOPIE_EXPR_SLEEPY,
                [BOOPIE_XZ_MOOD_SURPRISED] = BOOPIE_EXPR_DIZZY,
            };
            if (s_mood != BOOPIE_XZ_MOOD_NONE) {
                boopie_avatar_react(faces[s_mood], 3.0f);   /* shown once it's idle again */
            }
        }
    }
}

static void answer_mcp(const char *json)
{
    static char out[1024];
    char sid[BOOPIE_XZ_SESSION_MAX], name[64], args[256];
    int id = 0;
    session(sid, sizeof sid);
    int n = boopie_xz_mcp_reply(json, sid, "[]", out, sizeof out, name, sizeof name, args, sizeof args, &id);
    if (n == -2) {
        /* No tools offered yet (step 3), so none should be called. */
        n = boopie_xz_mcp_result(sid, id, "没有这个功能", true, out, sizeof out);
    }
    if (n > 0) {
        send_text(out, n);
    }
}

static void reset_reply(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    xMessageBufferReset(s_down);
    s_sent_n = 0;
    xSemaphoreGive(s_lock);
    xStreamBufferReset(s_pcm_out);
    s_stuck = false;
    s_out_total = 0;
    s_mood = BOOPIE_XZ_MOOD_NONE;
}

static void fail(const char *why)
{
    if (s_turn != T_IDLE) {
        s_turn = T_IDLE;
        push_event(MUSE_HATCH_EV_ERROR, why);
    }
}

static void codec_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        int64_t now = esp_timer_get_time();
        cmd_t c;
        while (xQueueReceive(s_cmds, &c, 0) == pdTRUE) {
            if (c == CMD_BEGIN) {
                reset_reply();
                xMessageBufferReset(s_outbox);
                s_frame_n = 0;
                s_end_pending = false;
                if (!ensure_ws()) {
                    fail("连不上小智");
                    continue;
                }
                outbox_put('L', NULL, 0);
            } else if (c == CMD_END) {
                s_end_pending = true;
            } else {
                bool talking = s_hello && s_connected;
                xMessageBufferReset(s_outbox);
                reset_reply();
                s_frame_n = 0;
                s_end_pending = false;
                if (talking) {
                    char sid[BOOPIE_XZ_SESSION_MAX], json[128];
                    session(sid, sizeof sid);
                    int n = boopie_xz_abort_json(sid, json, sizeof json);
                    if (n > 0) {
                        send_text(json, n);
                    }
                }
            }
        }
        if (s_say_hello && s_connected) {
            s_say_hello = false;
            char hello[256];
            int n = boopie_xz_hello_json(hello, sizeof hello);
            send_text(hello, n);
        }
        char *m;
        while (xQueueReceive(s_mcp, &m, 0) == pdTRUE) {
            answer_mcp(m);
            free(m);
        }
        encode();
        flush_outbox();
        decode();
        /* Watching the turn. */
        if (s_turn != T_IDLE && !s_hello && now - s_begin_us > HELLO_US) {
            fail("连不上小智，稍后再试");
        } else if (s_turn == T_WAIT && !s_heard && !s_spoke && now - s_last_rx_us > DEAF_US) {
            fail("小智没听清，再说一次");
        } else if (s_turn == T_WAIT && now - s_last_rx_us > MUTE_US) {
            fail("小智没有回应");
        }
        /* Nothing said for a while, or 小智 not wanted: close. */
        if (s_client && s_turn == T_IDLE
            && (now - s_last_use_us > IDLE_CLOSE_US || boopie_avatar_brain() != BOOPIE_BRAIN_XIAOZHI)) {
            ESP_LOGI(TAG, "closing: quiet");
            close_ws();
        }
    }
}

static bool init(void)
{
    if (s_codec) {
        return true;
    }
    const uint32_t ps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    s_lock = xSemaphoreCreateMutex();
    s_cmds = xQueueCreate(8, sizeof(cmd_t));
    s_events = xQueueCreateWithCaps(16, sizeof(event_t), ps);
    s_mcp = xQueueCreate(4, sizeof(char *));
    s_pcm_in = xStreamBufferCreateWithCaps(PCM_IN_BYTES, 1, ps);
    s_pcm_out = xStreamBufferCreateWithCaps(PCM_OUT_BYTES, 1, ps);
    s_outbox = xMessageBufferCreateWithCaps(OUTBOX_BYTES, ps);
    s_down = xMessageBufferCreateWithCaps(DOWN_BYTES, ps);
    s_txt = heap_caps_malloc(TEXT_MAX, ps);
    s_bin = heap_caps_malloc(PKT_MAX, ps);
    s_frame = heap_caps_malloc(FRAME * 2, ps);
    s_pcm = heap_caps_malloc(DEC_MAX * 2, ps);
    if (!s_lock || !s_cmds || !s_events || !s_mcp || !s_pcm_in || !s_pcm_out || !s_outbox || !s_down || !s_txt
        || !s_bin || !s_frame || !s_pcm) {
        ESP_LOGE(TAG, "no memory");
        return false;   /* leaked: it isn't tried again */
    }
    /* Opus wants a deep stack (upstream gives it 24 KB); it only computes and sends, so PSRAM will do. */
    if (xTaskCreateWithCaps(codec_task, "boopie_xz_codec", 24 * 1024, NULL, 5, &s_codec, ps) != pdPASS) {
        ESP_LOGE(TAG, "codec task not started");
        return false;
    }
    return true;
}

/* ---- the voice task's side ---- */

bool boopie_xz_ready(void)
{
    return boopie_avatar_brain() == BOOPIE_BRAIN_XIAOZHI && muse_wifi_connected()
           && boopie_xiaozhi_status(NULL, 0, NULL, 0) == BOOPIE_XZ_READY;
}

void boopie_xz_turn_begin(void)
{
    static bool failed;
    if (failed || !init()) {
        failed = true;
        return;
    }
    xQueueReset(s_events);
    xStreamBufferReset(s_pcm_in);
    s_heard = s_spoke = false;
    s_begin_us = s_last_use_us = esp_timer_get_time();
    s_turn = T_LISTEN;
    cmd_t c = CMD_BEGIN;
    xQueueSend(s_cmds, &c, portMAX_DELAY);
}

size_t boopie_xz_turn_audio_wait(const int16_t *pcm, size_t frames, int wait_ms)
{
    if (s_turn != T_LISTEN) {
        return frames;   /* failed: taken, and nowhere to go */
    }
    return xStreamBufferSend(s_pcm_in, pcm, frames * 2, pdMS_TO_TICKS(wait_ms)) / 2;
}

void boopie_xz_turn_audio(const int16_t *pcm, size_t frames)
{
    boopie_xz_turn_audio_wait(pcm, frames, 0);
}

void boopie_xz_turn_end(void)
{
    if (s_turn != T_LISTEN) {
        return;
    }
    s_last_rx_us = s_last_use_us = esp_timer_get_time();
    s_turn = T_WAIT;
    cmd_t c = CMD_END;
    xQueueSend(s_cmds, &c, portMAX_DELAY);
}

void boopie_xz_turn_cancel(void)
{
    if (!s_codec) {
        return;
    }
    bool was = s_turn != T_IDLE;
    s_turn = T_IDLE;
    xQueueReset(s_events);
    xStreamBufferReset(s_pcm_in);
    s_last_use_us = esp_timer_get_time();
    if (was) {
        cmd_t c = CMD_CANCEL;
        xQueueSend(s_cmds, &c, portMAX_DELAY);
    }
}

muse_hatch_ev_t boopie_xz_turn_event(char *text, size_t cap)
{
    event_t e;
    if (!s_events || xQueueReceive(s_events, &e, 0) != pdTRUE) {
        if (!s_codec && cap) {
            /* The tasks never started: say so, once a turn is asked of it. */
            static bool said;
            if (!said) {
                said = true;
                snprintf(text, cap, "小智对话没能启动");
                return MUSE_HATCH_EV_ERROR;
            }
        }
        return MUSE_HATCH_EV_NONE;
    }
    if (cap) {
        snprintf(text, cap, "%s", e.text);
    }
    return e.ev;
}

bool boopie_xz_turn_caption(size_t played, char *out, size_t cap)
{
    if (!s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_sent_n, first = n > SENTENCES ? n - SENTENCES : 0;
    int pick = -1;
    for (int i = first; i < n; i++) {
        if (s_sent[i % SENTENCES].at <= played || pick < 0) {
            pick = i;
        }
    }
    if (pick >= 0) {
        snprintf(out, cap, "%s", s_sent[pick % SENTENCES].text);
    }
    xSemaphoreGive(s_lock);
    return pick >= 0;
}

size_t boopie_xz_turn_read(int16_t *pcm, size_t frames, int wait_ms)
{
    if (!s_pcm_out) {
        return 0;
    }
    return xStreamBufferReceive(s_pcm_out, pcm, frames * 2, pdMS_TO_TICKS(wait_ms)) / 2;
}
