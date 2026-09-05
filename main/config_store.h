/**
    config_store.h : RFScout internal configuration persistence
    Description: Owns the internal SPIFFS configuration mount and SD import/example exchange.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_CONFIG_STORE_H_
#define _RFSCOUT_CONFIG_STORE_H_

#include "esp_err.h"

#include <stdbool.h>

#define RFSCOUT_INTERNAL_CONFIG_ROOT "/internal"
#define RFSCOUT_INTERNAL_CONFIG_PATH RFSCOUT_INTERNAL_CONFIG_ROOT "/rfscout.json"
#define RFSCOUT_SD_CONFIG_IMPORT_PATH "/sdcard/rfscout.json"
#define RFSCOUT_SD_CONFIG_BAD_PATH RFSCOUT_SD_CONFIG_IMPORT_PATH ".bad"
#define RFSCOUT_SD_CONFIG_EXAMPLE_PATH "/sdcard/rfscout-example.json"

/** @brief Mount internal persistence and consume a valid SD configuration import when present. */
esp_err_t config_store_init( void );

/** @brief Return whether the internal persistence filesystem is mounted and usable. */
bool config_store_is_available( void );

/**
 * @brief Synchronize the SD example with the authoritative internal configuration.
 * @details This is a no-op when SD is unavailable or the files already match byte-for-byte.
 */
esp_err_t config_store_sync_example( void );

#endif // _RFSCOUT_CONFIG_STORE_H_
