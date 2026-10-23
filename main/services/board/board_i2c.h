/**
    board_i2c.h : Shared RFScout board I2C bus
    Description: Owns the shared board I2C controller used by onboard and expansion devices.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BOARD_I2C_H_
#define _RFSCOUT_BOARD_I2C_H_

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Initialize the shared board I2C bus. */
esp_err_t board_i2c_init( void );

/** @brief Return the initialized shared I2C bus handle, or NULL when initialization fails. */
i2c_master_bus_handle_t board_i2c_handle( void );

/** @brief Whether shared I2C clients may transact after a hardware timeout. */
bool board_i2c_ready( void );

/** @brief Probe one seven-bit address on the shared board I2C bus. */
esp_err_t board_i2c_probe( uint16_t a_address );

/** @brief Add one device to the shared board I2C bus. */
esp_err_t board_i2c_add_device( uint16_t a_address, i2c_master_dev_handle_t *a_device );

/**
 * @brief Add a shared-bus device with a device-specific I2C clock frequency.
 * @note Other devices retain their own configured clock rates on the same bus.
 */
esp_err_t board_i2c_add_device_with_speed( uint16_t a_address,
                                           uint32_t a_scl_speed_hz,
                                           i2c_master_dev_handle_t *a_device );

/**
 * @brief Write bytes and immediately read a response from a shared-bus device.
 * @param a_device Device handle returned by board_i2c_add_device().
 * @param a_write Bytes sent before the repeated-start read.
 * @param a_write_length Number of bytes to send.
 * @param a_read Destination buffer.
 * @param a_read_length Number of bytes to receive.
 * @return ESP_OK on success or an ESP-IDF I2C error.
 */
esp_err_t board_i2c_transmit_receive( i2c_master_dev_handle_t a_device,
                                      const uint8_t *a_write,
                                      size_t a_write_length,
                                      uint8_t *a_read,
                                      size_t a_read_length );

/** @brief Write bytes to a shared-bus device. */
esp_err_t
board_i2c_transmit( i2c_master_dev_handle_t a_device, const uint8_t *a_data, size_t a_length );

#endif // _RFSCOUT_BOARD_I2C_H_
