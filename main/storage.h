/**
    storage.h : RFScout SD-card storage interface
    Description: Owns SD-card mounting, unmounting, and capacity queries.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_STORAGE_H_
#define _RFSCOUT_STORAGE_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_STORAGE_MOUNT_POINT "/sdcard"
#define RFSCOUT_STORAGE_LOG_DIRECTORY "/sdcard/logs"

/**
 * @brief Mount the SD card on the board's already initialized shared SPI bus.
 * @return ESP_OK on success or the underlying ESP-IDF storage error.
 * @note Failure is nonfatal to the rest of RFScout.
 */
esp_err_t storage_init( void );

/**
 * @brief Unmount the SD card and release storage resources.
 */
void storage_shutdown( void );

/**
 * @brief Determine whether the SD card is currently mounted.
 * @return True when storage is available.
 */
bool storage_is_available( void );

/**
 * @brief Verify that the currently mounted physical SD card still responds.
 * @return ESP_OK when the mounted card responds, ESP_ERR_INVALID_STATE when no card is mounted,
 *         or the underlying SD/MMC status error when the card disappeared or became unusable.
 * @note This does not unmount or remount storage; callers that own open files must quiesce them first.
 */
esp_err_t storage_probe_card( void );

/**
 * @brief Query the filesystem capacity.
 * @param a_total_bytes Receives total filesystem bytes when non-NULL.
 * @param a_free_bytes Receives currently free filesystem bytes when non-NULL.
 * @return ESP_OK on success or an ESP-IDF/VFS error.
 */
esp_err_t storage_get_capacity( uint64_t *a_total_bytes, uint64_t *a_free_bytes );

#endif // _RFSCOUT_STORAGE_H_
