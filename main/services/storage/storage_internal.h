/**
    storage_internal.h : RFScout storage-service internal interface
    Description: Private arbitration shared only by storage service implementation units.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_STORAGE_INTERNAL_H_
#define _RFSCOUT_STORAGE_INTERNAL_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_STORAGE_WAIT_FOREVER UINT32_MAX
/** Private SD VFS mount point; never exposed outside the storage service. */
#define RFSCOUT_STORAGE_MOUNT_POINT "/sdcard"
/** FAT open-file limit configured at mount; also the media-store handle pool size. */
#define RFSCOUT_STORAGE_MAX_OPEN_FILES 5

/** @brief Acquire storage-service arbitration for one bounded filesystem transaction. */
bool storage_io_acquire( uint32_t a_timeout_ms );

/** @brief Release storage-service arbitration after storage_io_acquire() succeeds. */
void storage_io_release( void );

/** @brief True until three seconds have elapsed since the latest SD media failure. */
bool storage_retry_delayed( void );

/** @brief Latch failed SD media and begin the shared three-second cooldown. */
void storage_note_media_failure( void );

/** @brief Whether the SD volume is mounted (including a latched-lost volume); caller holds arbitration. */
bool storage_is_mounted( void );

/** @brief Unmount a latched-lost card and attempt a fresh mount. Caller must close media handles. */
esp_err_t storage_recover_lost_media( void );

#endif // _RFSCOUT_STORAGE_INTERNAL_H_
