/**
    storage.h : RFScout SD-card storage interface
    Description: Owns SD-card mounting, health probing, and capacity queries.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_STORAGE_H_
#define _RFSCOUT_STORAGE_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Mount the SD card on the board's already initialized shared SPI bus.
 * @details The mount point is private to the storage service. Other modules access files only
 *          through media_store.h.
 * @return ESP_OK on success or the underlying ESP-IDF storage error.
 * @note Failure is nonfatal to the rest of RFScout. A successful mount remains owned by the
 *       platform for the lifetime of the running firmware.
 */
esp_err_t storage_init( void );

/**
 * @brief Determine whether mounted SD media is currently considered healthy and usable.
 * @return True only while storage is mounted and no physical-media loss has been latched.
 */
bool storage_is_available( void );

/**
 * @brief Verify that the currently mounted physical SD card still responds.
 * @return ESP_OK when the mounted card responds or when a filesystem transaction causes this probe
 *         attempt to be deferred, ESP_ERR_INVALID_STATE when no healthy card is mounted, or the
 *         underlying SD/MMC status error when the card disappears or fails.
 * @note A failed physical probe latches the media unavailable for the remainder of the running
 *       platform session and emits RFSCOUT_EVENT_STORAGE_MOUNT_FAILED once to report that storage
 *       became unavailable. Probe arbitration is storage-owned and never unmounts/remounts the
 *       volume or invalidates open files.
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
