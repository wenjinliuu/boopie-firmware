/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/* Boopie: what the real settings pages (muse_settings_ui.c) call, stood in
 * for so they can be shown and screenshotted in the simulator. */

#include "esp_app_desc.h"
#include "esp_mac.h"

const esp_app_desc_t *esp_app_get_description(void)
{
    static const esp_app_desc_t DESC = { "sim" };
    return &DESC;
}

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
    (void)type;
    static const uint8_t MAC[6] = { 0x24, 0x6f, 0x28, 0x5b, 0x1d, 0x75 };
    for (int i = 0; i < 6; i++) {
        mac[i] = MAC[i];
    }
    return ESP_OK;
}

#include <stdio.h>
#include <string.h>

#include "muse_audio.h"
#include "muse_battery.h"
#include "muse_ble.h"
#include "muse_chat.h"
#include "muse_input.h"
#include "muse_link.h"
#include "muse_settings.h"
#include "muse_voice.h"
#include "muse_wifi.h"

/* Settings, kept in memory for the run. */
static int s_volume = 70, s_mic_gain = 24, s_sleep_s = 60;
static bool s_wifi_on = true, s_ble_on = true;

int muse_settings_volume(void) { return s_volume; }
void muse_settings_set_volume(int pct) { s_volume = pct; }
int muse_settings_mic_gain(void) { return s_mic_gain; }
void muse_settings_set_mic_gain(int db) { s_mic_gain = db; }
int muse_settings_sleep_s(void) { return s_sleep_s; }
void muse_settings_set_sleep_s(int secs) { s_sleep_s = secs; }
bool muse_settings_wifi_on(void) { return s_wifi_on; }
void muse_settings_set_wifi_on(bool on) { s_wifi_on = on; }
bool muse_settings_ble_on(void) { return s_ble_on; }
void muse_settings_set_ble_on(bool on) { s_ble_on = on; }
void muse_settings_set_wifi(const char *ssid, const char *pass) { (void)ssid; (void)pass; }
void muse_settings_hatch_host(char out[MUSE_HOST_MAX + 1]) { snprintf(out, MUSE_HOST_MAX + 1, "%s", ""); }
void muse_settings_set_hatch_host(const char *host) { (void)host; }
void muse_settings_hatch_vm(char out[MUSE_VM_MAX + 1]) { snprintf(out, MUSE_VM_MAX + 1, "%s", ""); }
void muse_settings_set_hatch_vm(const char *vm) { (void)vm; }
size_t muse_settings_hatch_token_len(void) { return 0; }
esp_err_t muse_settings_set_hatch_token(const char *token, bool append)
{
    (void)token;
    (void)append;
    return ESP_OK;
}

void muse_audio_set_volume(int volume) { (void)volume; }
void muse_audio_set_mic_gain(int db) { (void)db; }
float muse_voice_monitor_db(void) { return -42.0f; }
void muse_voice_request_chirp(void) {}
void muse_voice_set_monitor(bool on) { (void)on; }

void muse_battery_read(muse_battery_t *out) { memset(out, 0, sizeof *out); }
void muse_battery_reset(void) {}
bool muse_battery_drain(const muse_battery_t *b, int *rate10, int *full_h)
{
    (void)b;
    (void)rate10;
    (void)full_h;
    return false;
}

void muse_ble_forget_all(void) {}
void muse_input_request_power_off(void) {}
const char *muse_hatch_state_name(muse_hatch_state_t state) { (void)state; return "Not set"; }
void muse_hatch_test(void) {}
bool muse_link_hatch_linked(void) { return false; }
void muse_link_reset_setup(void) {}
const char *muse_link_state_name(muse_link_state_t state) { (void)state; return "Online"; }

/* A few networks nearby, one of them with a Chinese name. */
static const muse_wifi_ap_t APS[] = {
    { "Home-5G", -48, true }, { "我家的WiFi", -55, true }, { "Office", -67, true }, { "Cafe Free", -74, false },
};

void muse_wifi_apply(void) {}
void muse_wifi_forget(const char *ssid) { (void)ssid; }
int muse_wifi_saved(muse_wifi_saved_t *out, int max)
{
    if (max < 1) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    snprintf(out->ssid, sizeof out->ssid, "%s", "Home-5G");
    return 1;
}
esp_err_t muse_wifi_scan(void) { return ESP_OK; }
bool muse_wifi_scanning(void) { return false; }
int muse_wifi_scan_results(muse_wifi_ap_t *out, int max, uint32_t *gen)
{
    int n = (int)(sizeof APS / sizeof APS[0]);
    n = n < max ? n : max;
    memcpy(out, APS, (size_t)n * sizeof *out);
    if (gen) {
        *gen = 1;
    }
    return n;
}

/* Boopie: phone setup's hotspot. BOOPIE_SETUP_CLIENTS (phones joined) and
 * BOOPIE_SETUP_SAVED (what the page saved, as bits) stand in for a phone. */
#include <stdlib.h>
#include "boopie_setup_web.h"

static bool s_setup_on;

bool boopie_setup_web_start(boopie_setup_ap_t *ap)
{
    snprintf(ap->ssid, sizeof ap->ssid, "%s", "Boopie-1A2B");
    snprintf(ap->pass, sizeof ap->pass, "%s", "k7m2p9qa");
    s_setup_on = true;
    return true;
}
bool boopie_setup_web_stop(void) { s_setup_on = false; return false; }
int boopie_setup_web_clients(void)
{
    const char *n = getenv("BOOPIE_SETUP_CLIENTS");
    return s_setup_on && n ? atoi(n) : 0;
}
uint32_t boopie_setup_web_saves(uint32_t *saved)
{
    const char *bits = getenv("BOOPIE_SETUP_SAVED");
    uint32_t b = s_setup_on && bits ? (uint32_t)strtoul(bits, NULL, 0) : 0;
    if (saved) {
        *saved = b;
    }
    return b ? 1 : 0;
}
int boopie_setup_web_idle_s(void) { return 0; }
size_t boopie_setup_subscription(char *out, size_t cap)
{
    if (cap) {
        out[0] = '\0';
    }
    return 0;
}

/* Boopie: the developer token and the VPN. BOOPIE_SDK set: a token's in;
 * BOOPIE_VPN set: on, with a few nodes to show. */
#include "boopie_sdk_token.h"
#include "boopie_vpn.h"

bool boopie_sdk_token(char *out)
{
    out[0] = '\0';
    if (!getenv("BOOPIE_SDK")) {
        return false;
    }
    snprintf(out, BOOPIE_SDK_TOKEN_LEN + 1, "%s", "mgst_simulatorsimulatorsimulatorsimulatorsimA");
    return true;
}
bool boopie_sdk_token_valid(const char *t) { return t && strlen(t) == BOOPIE_SDK_TOKEN_LEN; }
bool boopie_sdk_token_set(const char *t) { (void)t; return true; }

static const boopie_vpn_node_t SIM_NODES[] = {
    { .name = "节点一", .host = "hk.example", .cipher = "aes-256-gcm", .port = 443, .supported = true },
    { .name = "节点二", .host = "jp.example", .cipher = "chacha20-ietf-poly1305", .port = 443, .supported = true },
    { .name = "节点三", .host = "us.example", .cipher = "aes-128-gcm", .port = 8388, .supported = true },
    { .name = "节点四", .host = "sg.example", .cipher = "2022-blake3-aes-128-gcm", .port = 443, .supported = false },
};
static const int SIM_LATENCY[] = { 86, 142, -2, -1 };
static int s_sim_node;

void boopie_vpn_init(const char *extra_host) { (void)extra_host; }
bool boopie_vpn_on(void) { return getenv("BOOPIE_VPN") != NULL; }

/* 小智: BOOPIE_XZ_CODE shows that code waiting to be bound; BOOPIE_XZ_READY, bound. */
#include "boopie_xiaozhi.h"
void boopie_xiaozhi_start(void) {}
void boopie_xiaozhi_recheck(void) {}
void boopie_xiaozhi_rebind(void) {}
bool boopie_xiaozhi_endpoint(char *url, size_t url_cap, char *token, size_t token_cap)
{
    snprintf(url, url_cap, "%s", "");
    snprintf(token, token_cap, "%s", "");
    return false;
}
boopie_xz_state_t boopie_xiaozhi_status(char *code, size_t code_cap, char *note, size_t note_cap)
{
    const char *c = getenv("BOOPIE_XZ_CODE");
    boopie_xz_state_t st = c ? BOOPIE_XZ_CODE : getenv("BOOPIE_XZ_READY") ? BOOPIE_XZ_READY : BOOPIE_XZ_NO_NET;
    if (code_cap) {
        snprintf(code, code_cap, "%s", c ? c : "");
    }
    if (note_cap) {
        snprintf(note, note_cap, "%s", st == BOOPIE_XZ_CODE ? "到 xiaozhi.me 添加设备，输入它"
                                       : st == BOOPIE_XZ_READY ? "已连接小智" : "等 Wi-Fi 连上");
    }
    return st;
}
void boopie_vpn_set_on(bool on) { (void)on; }
int boopie_vpn_count(void) { return getenv("BOOPIE_VPN") ? (int)(sizeof SIM_NODES / sizeof SIM_NODES[0]) : 0; }
bool boopie_vpn_node(int i, boopie_vpn_node_t *out)
{
    if (i < 0 || i >= boopie_vpn_count()) {
        return false;
    }
    *out = SIM_NODES[i];
    return true;
}
int boopie_vpn_current(void) { return boopie_vpn_count() ? s_sim_node : -1; }
void boopie_vpn_select(int i) { s_sim_node = i; }
int boopie_vpn_latency(int i) { return i >= 0 && i < boopie_vpn_count() ? SIM_LATENCY[i] : -1; }
boopie_vpn_busy_t boopie_vpn_busy(char *msg, size_t cap)
{
    snprintf(msg, cap, "%s", getenv("BOOPIE_VPN") ? "已更新：4 个节点，3 个能用" : "");
    return BOOPIE_VPN_IDLE;
}
void boopie_vpn_update(void) {}
void boopie_vpn_test(void) {}
bool boopie_vpn_active(void) { return getenv("BOOPIE_VPN") != NULL; }

/* Boopie: the voice task's notes (the simulator has none). */
void muse_voice_clear_notes(void) {}
