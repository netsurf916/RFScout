/**
    board_i2c.h : Shared RFScout board I2C bus
    Description: Owns the T-Embed I2C controller used by power and NFC devices.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BOARD_I2C_H_
#define _RFSCOUT_BOARD_I2C_H_

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <stdint.h>

/**
 * @brief Initialize the shared board I2C bus.
 * @return ESP_OK on success or an ESP-IDF driver error.
 * @note The function is idempotent and may be called by multiple feature modules.
 */
esp_err_t board_i2c_init( void );

/**
 * @brief Probe one seven-bit address on the shared board I2C bus.
 * @param a_address Seven-bit I2C address without the read/write bit.
 * @return ESP_OK when the device acknowledges or an ESP-IDF driver error.
 */
esp_err_t board_i2c_probe( uint16_t a_address );

/**
 * @brief Add one device to the shared board I2C bus.
 * @param a_address Seven-bit I2C address without the read/write bit.
 * @param a_device Receives the ESP-IDF device handle.
 * @return ESP_OK on success or an ESP-IDF driver error.
 */
esp_err_t board_i2c_add_device( uint16_t a_address,
                                i2c_master_dev_handle_t *a_device );

#endif // _RFSCOUT_BOARD_I2C_H_
