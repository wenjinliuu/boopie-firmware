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
