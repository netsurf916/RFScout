/**
    ota_acquisition.h : RFScout cancellable HTTPS OTA acquisition service
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

typedef enum
{
    OTA_ACQUISITION_KIND_NONE = 0,
    OTA_ACQUISITION_KIND_CATALOG,
    OTA_ACQUISITION_KIND_FIRMWARE
} ota_acquisition_kind_t;

typedef enum
{
    OTA_ACQUISITION_IDLE = 0,
    OTA_ACQUISITION_FETCHING,
    OTA_ACQUISITION_READY,
    OTA_ACQUISITION_FAILED
} ota_acquisition_state_t;

typedef struct
{
    ota_acquisition_kind_t kind;
    ota_acquisition_state_t state;
    esp_err_t error;
    int http_status;
    size_t length;
    uint32_t generation;
} ota_acquisition_status_t;

esp_err_t ota_acquisition_begin_catalog( void );
esp_err_t ota_acquisition_begin_firmware( const char *a_artifact_path );
void ota_acquisition_status( ota_acquisition_status_t *a_status );
esp_err_t ota_acquisition_copy_catalog( char *a_destination, size_t a_capacity, size_t *a_length );
esp_err_t ota_acquisition_stop( void );
bool ota_acquisition_is_active( void );

#endif // _RFSCOUT_OTA_ACQUISITION_H_
