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
    int16_t pixels[ MLX90640_PIXEL_COUNT ];
    int16_t minimum;
    int16_t maximum;
    int16_t center;
} mlx90640_frame_t;

/**
 * @brief Probe for the optional MLX90640 at its standard address.
 * @return ESP_OK when the device acknowledges or the underlying I2C error.
 * @details The device handle is attached lazily on first successful detection.
 */
esp_err_t mlx90640_probe( void );

/** @brief Initialize the optional MLX90640 by probing its standard address. */
esp_err_t mlx90640_init( void );

/** @brief Return whether the sensor is currently known to be present. */
bool mlx90640_is_detected( void );

/**
 * @brief Acquire and merge the current MLX90640 subpage into the raw frame.
 * @param a_frame Destination for the 32x24 frame and range metadata.
 * @return ESP_OK when a complete two-subpage frame is available,
 *         ESP_ERR_NOT_FINISHED while waiting for the complementary subpage,
 *         or an I2C/device-state error.
 * @note Values are signed raw IR data, not calibrated degrees Celsius. The
 *       MLX90640 updates alternating pixel subpages; this routine merges both
 *       subpages before reporting a valid image to avoid checkerboard/striped
 *       artifacts caused by treating inactive pixels as a complete frame.
 */
esp_err_t mlx90640_read_frame( mlx90640_frame_t *a_frame );

#endif // _RFSCOUT_MLX90640_H_
