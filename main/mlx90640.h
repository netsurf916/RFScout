/**
    mlx90640.h : RFScout MLX90640 thermal-array interface
    Description: Detects and acquires 32x24 thermal-array sensor frames.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_MLX90640_H_
#define _RFSCOUT_MLX90640_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define MLX90640_WIDTH 32
#define MLX90640_HEIGHT 24
#define MLX90640_PIXEL_COUNT ( MLX90640_WIDTH * MLX90640_HEIGHT )

typedef struct
{
    bool detected;
    bool frame_valid;
    uint16_t pixels[ MLX90640_PIXEL_COUNT ];
    uint16_t minimum;
    uint16_t maximum;
    uint16_t center;
} mlx90640_frame_t;

/** @brief Probe and attach the optional MLX90640 at its standard address. */
esp_err_t mlx90640_init( void );

/** @brief Return whether the sensor acknowledged during initialization. */
bool mlx90640_is_detected( void );

/**
 * @brief Acquire the current raw IR frame.
 * @param a_frame Destination for the 32x24 frame and range metadata.
 * @return ESP_OK on success or an I2C/device-state error.
 * @note Values are sensor IR data, not calibrated degrees Celsius. The module
 *       intentionally keeps acquisition separate from calibration/conversion.
 */
esp_err_t mlx90640_read_frame( mlx90640_frame_t *a_frame );

#endif // _RFSCOUT_MLX90640_H_
