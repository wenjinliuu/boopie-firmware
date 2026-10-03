/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/*
 * The QMI8658 six-axis IMU on the Waveshare 1.75C (I2C 0x6B, shared with the
 * touch, codecs and PMU), which the official firmware leaves unused. The
 * accelerometer runs in its low-power mode and is read 20 times a second
 * while the screen is drawing, once a second otherwise; the gyroscope is off
 * until a game asks for it.
 */

typedef void (*boopie_imu_cb_t)(void);

/* Find and start it on the board's I2C bus; ESP_ERR_NOT_FOUND if it isn't there. */
esp_err_t boopie_imu_init(i2c_master_bus_handle_t bus);

bool boopie_imu_present(void);

/* Called (from the IMU's task) when the board is shaken. */
void boopie_imu_on_shake(boopie_imu_cb_t cb);

/* Say the screen is drawing, so it reads fast; call each frame. */
void boopie_imu_keepalive(void);

/* The latest reading: acceleration in g, rotation in degrees a second (zero
 * while the gyroscope is off). False if there's no IMU. */
bool boopie_imu_read(float accel[3], float gyro[3]);

/* Turn the gyroscope on (games) or off. */
esp_err_t boopie_imu_gyro(bool on);
