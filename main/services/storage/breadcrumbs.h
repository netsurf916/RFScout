/**
    breadcrumbs.h : RFScout GPS breadcrumb recorder
    Description: Records durable GPS-UTC location breadcrumbs to bounded CSV files on SD.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BREADCRUMBS_H_
#define _RFSCOUT_BREADCRUMBS_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_BREADCRUMB_FILE_LIMIT_DEFAULT 144U
#define RFSCOUT_BREADCRUMB_FILE_LIMIT_MAX 1008U

/** @brief Start the breadcrumb worker when GPS and SD storage providers are available. */
esp_err_t breadcrumbs_init( void );

/** @brief Snapshot a fresh GPS fix for asynchronous breadcrumb recording when due. */
void breadcrumbs_poll( void );

/** @brief Return whether the active runtime has usable GPS and SD providers for breadcrumbs. */
bool breadcrumbs_available( void );

/** @brief Return whether breadcrumb recording is enabled. */
bool breadcrumbs_enabled( void );

/** @brief Persist breadcrumb recording enablement before publishing the new setting. */
esp_err_t breadcrumbs_set_enabled( bool a_enabled );

/** @brief Return the configured maximum retained breadcrumb CSV file count. */
uint16_t breadcrumbs_file_limit( void );

/** @brief Persist a new retained breadcrumb file count and request asynchronous pruning. */
esp_err_t breadcrumbs_set_file_limit( uint16_t a_file_limit );

#endif // _RFSCOUT_BREADCRUMBS_H_
