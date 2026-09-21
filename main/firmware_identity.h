/**
    firmware_identity.h : RFScout running firmware identity
    Description: Exposes update-relevant identity without leaking ESP-IDF image metadata to apps.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_FIRMWARE_IDENTITY_H_
#define _RFSCOUT_FIRMWARE_IDENTITY_H_

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_FIRMWARE_SHA256_LENGTH 32U
#define RFSCOUT_FIRMWARE_SHA256_HEX_LENGTH 64U

typedef struct
{
    const char *project;
    const char *version;
    const char *hardware_target;
    const char *idf_target;
    const char *partition_layout;
} rfscout_firmware_identity_t;

/** @brief Return immutable identity for the currently running application image and hardware. */
const rfscout_firmware_identity_t *firmware_identity( void );

/**
 * @brief Hash the exact running application-image byte range stored in flash.
 * @details The returned SHA-256 covers the same bytes as the packaged application .bin file,
 *          including the ESP-IDF appended simple hash when present. This intentionally differs
 *          from esp_partition_get_sha256(), which returns the image-content digest for apps.
 */
esp_err_t firmware_running_image_sha256( uint8_t a_digest[ RFSCOUT_FIRMWARE_SHA256_LENGTH ],
                                         size_t *a_image_length );

/** @brief Convert a 32-byte SHA-256 digest to lower-case hexadecimal. */
void firmware_sha256_hex( const uint8_t a_digest[ RFSCOUT_FIRMWARE_SHA256_LENGTH ],
                          char a_hex[ RFSCOUT_FIRMWARE_SHA256_HEX_LENGTH + 1U ] );

#endif // _RFSCOUT_FIRMWARE_IDENTITY_H_
