/**
    ota_acquisition.h : RFScout OTA catalog acquisition service
    Description: Owns bounded cancellable retrieval of the stable update catalog.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OTA_ACQUISITION_H_
#define _RFSCOUT_OTA_ACQUISITION_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_ACQUISITION_CATALOG_MAX_LENGTH 16384U

typedef enum
{
    OTA_ACQUISITION_IDLE = 0,
    OTA_ACQUISITION_FETCHING,
    OTA_ACQUISITION_READY,
    OTA_ACQUISITION_FAILED
} ota_acquisition_state_t;

typedef struct
{
    ota_acquisition_state_t state;
    esp_err_t error;
    int http_status;
    size_t length;
    uint32_t generation;
} ota_acquisition_status_t;

/** @brief Begin one explicit fetch of the stable RFScout OTA catalog. */
esp_err_t ota_acquisition_begin_catalog( void );

/** @brief Snapshot acquisition state without exposing HTTP/TLS implementation details. */
ota_acquisition_status_t ota_acquisition_status( void );

/** @brief Copy a completed catalog into caller storage, including a terminating NUL byte. */
esp_err_t ota_acquisition_copy_catalog( char *a_destination,
                                        size_t a_capacity,
                                        size_t *a_length );

/** @brief Cancel/join any in-flight request and erase transient acquisition data. */
esp_err_t ota_acquisition_stop( void );

/** @brief Return whether an acquisition worker currently owns network activity. */
bool ota_acquisition_is_active( void );

#endif // _RFSCOUT_OTA_ACQUISITION_H_
