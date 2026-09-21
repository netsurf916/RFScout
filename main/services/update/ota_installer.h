/**
    ota_installer.h : RFScout transport-neutral OTA image installer
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OTA_INSTALLER_H_
#define _RFSCOUT_OTA_INSTALLER_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_INSTALLER_VERSION_MAX_LENGTH 31U
#define OTA_INSTALLER_SHA256_HEX_LENGTH 64U

typedef enum
{
    OTA_INSTALLER_IDLE = 0,
    OTA_INSTALLER_WRITING,
    OTA_INSTALLER_VERIFYING,
    OTA_INSTALLER_VERIFIED,
    OTA_INSTALLER_COMMITTED,
    OTA_INSTALLER_FAILED
} ota_installer_state_t;

typedef struct
{
    char version[ OTA_INSTALLER_VERSION_MAX_LENGTH + 1U ];
    char sha256[ OTA_INSTALLER_SHA256_HEX_LENGTH + 1U ];
    uint32_t size;
} ota_installer_image_t;

typedef struct
{
    ota_installer_state_t state;
    esp_err_t error;
    uint32_t bytes_written;
    uint32_t expected_size;
    uint32_t generation;
} ota_installer_status_t;

esp_err_t ota_installer_begin( const ota_installer_image_t *a_image );
esp_err_t ota_installer_write( const void *a_data, size_t a_length );
esp_err_t ota_installer_finish( void );
esp_err_t ota_installer_commit( void );
esp_err_t ota_installer_abort( void );
ota_installer_status_t ota_installer_status( void );
bool ota_installer_is_active( void );

#endif // _RFSCOUT_OTA_INSTALLER_H_
