/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "boopie_imu.h"

#include <string.h>

#include "boopie_shake.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "boopie_imu";

/* QMI8658 registers (datasheet; as Waveshare's SensorLib). */
#define ADDR 0x6B
#define REG_WHO_AM_I 0x00
#define WHO_AM_I 0x05
#define REG_CTRL1 0x02      /* bit 6: address auto-increment */
#define REG_CTRL2 0x03      /* accel: range 6:4, rate 3:0 */
#define REG_CTRL3 0x04      /* gyro: range 6:4, rate 3:0 */
#define REG_CTRL7 0x08      /* bit 0 accel on, bit 1 gyro on */
#define REG_AX_L 0x35       /* AX AY AZ GX GY GZ, little endian */
#define REG_RST_RESULT 0x4D
#define REG_RESET 0x60
#define RESET_CMD 0xB0

#define ACC_4G (1 << 4)
#define ACC_LP_21HZ 13      /* low power: only with the gyro off */
#define ACC_125HZ 6
#define GYR_512DPS (5 << 4)
#define GYR_112HZ 6
#define ACC_LSB_PER_G (32768.0f / 4)
#define GYR_LSB_PER_DPS (32768.0f / 512)

static i2c_master_dev_handle_t s_dev;
static boopie_imu_cb_t s_on_shake;
static volatile int64_t s_awake_until_us;
static volatile bool s_gyro;
static float s_accel[3], s_gyro_v[3];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 50);
}

static esp_err_t read_regs(uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, n, 50);
}

static esp_err_t configure(bool gyro)
{
    ESP_RETURN_ON_ERROR(write_reg(REG_CTRL7, 0), TAG, "stop");
    ESP_RETURN_ON_ERROR(write_reg(REG_CTRL2, ACC_4G | (gyro ? ACC_125HZ : ACC_LP_21HZ)), TAG, "accel");
    ESP_RETURN_ON_ERROR(write_reg(REG_CTRL3, GYR_512DPS | GYR_112HZ), TAG, "gyro");
    return write_reg(REG_CTRL7, gyro ? 0x03 : 0x01);
}

static void imu_task(void *arg)
{
    (void)arg;
    boopie_shake_t shake = { 0 };
    int64_t last = esp_timer_get_time();
    for (;;) {
        int64_t now = esp_timer_get_time();
        bool awake = now < s_awake_until_us;
        uint8_t raw[12];
        if (read_regs(REG_AX_L, raw, 12) == ESP_OK) {
            int16_t v[6];
            for (int i = 0; i < 6; i++) {
                v[i] = (int16_t)(raw[2 * i] | (raw[2 * i + 1] << 8));
            }
            float a[3] = { v[0] / ACC_LSB_PER_G, v[1] / ACC_LSB_PER_G, v[2] / ACC_LSB_PER_G };
            portENTER_CRITICAL(&s_lock);
            memcpy(s_accel, a, sizeof(a));
            for (int i = 0; i < 3; i++) {
                s_gyro_v[i] = s_gyro ? v[3 + i] / GYR_LSB_PER_DPS : 0;
            }
            portEXIT_CRITICAL(&s_lock);
            if (boopie_shake_feed(&shake, a[0], a[1], a[2], (now - last) / 1e6f) && s_on_shake) {
                ESP_LOGI(TAG, "shaken");
                s_on_shake();
            }
        }
        last = now;
        vTaskDelay(pdMS_TO_TICKS(awake || s_gyro ? 50 : 1000));
    }
}

esp_err_t boopie_imu_init(i2c_master_bus_handle_t bus)
{
    if (s_dev) {
        return ESP_OK;
    }
    if (i2c_master_probe(bus, ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG, "no QMI8658 at 0x%02x", ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add device");
    uint8_t id = 0;
    if (read_regs(REG_WHO_AM_I, &id, 1) != ESP_OK || id != WHO_AM_I) {
        ESP_LOGW(TAG, "unexpected WHO_AM_I 0x%02x", id);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    write_reg(REG_RESET, RESET_CMD);
    for (int i = 0; i < 20; i++) {   /* up to 15 ms */
        vTaskDelay(pdMS_TO_TICKS(5));
        uint8_t r = 0;
        if (read_regs(REG_RST_RESULT, &r, 1) == ESP_OK && r == 0x80) {
            break;
        }
    }
    ESP_RETURN_ON_ERROR(write_reg(REG_CTRL1, 0x40), TAG, "auto-increment");
    ESP_RETURN_ON_ERROR(configure(false), TAG, "configure");
    if (xTaskCreate(imu_task, "boopie_imu", 3072, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "QMI8658 up");
    return ESP_OK;
}

bool boopie_imu_present(void)
{
    return s_dev != NULL;
}

void boopie_imu_on_shake(boopie_imu_cb_t cb)
{
    s_on_shake = cb;
}

void boopie_imu_keepalive(void)
{
    s_awake_until_us = esp_timer_get_time() + 1000000;
}

bool boopie_imu_read(float accel[3], float gyro[3])
{
    if (!s_dev) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    memcpy(accel, s_accel, sizeof(s_accel));
    memcpy(gyro, s_gyro_v, sizeof(s_gyro_v));
    portEXIT_CRITICAL(&s_lock);
    return true;
}

esp_err_t boopie_imu_gyro(bool on)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    s_gyro = on;
    return configure(on);
}
