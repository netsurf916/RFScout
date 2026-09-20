/**
    apps/ota/ota_catalog.h : RFScout stable OTA catalog validation and waypoint selection
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OTA_CATALOG_H_
#define _RFSCOUT_OTA_CATALOG_H_

#include "firmware_identity.h"

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#define OTA_CATALOG_VERSION_MAX_LENGTH 31U
#define OTA_CATALOG_ARTIFACT_PATH_MAX_LENGTH 191U
#define OTA_CATALOG_SHA256_LENGTH 64U
#define OTA_CATALOG_MAX_RELEASES 32U

typedef enum
{
    OTA_CATALOG_CURRENT = 0,
    OTA_CATALOG_UPDATE_AVAILABLE,
    OTA_CATALOG_BLOCKED
} ota_catalog_result_t;

typedef struct
{
    char version[ OTA_CATALOG_VERSION_MAX_LENGTH + 1U ];
    char minimum_source_version[ OTA_CATALOG_VERSION_MAX_LENGTH + 1U ];
    char artifact_path[ OTA_CATALOG_ARTIFACT_PATH_MAX_LENGTH + 1U ];
    char sha256[ OTA_CATALOG_SHA256_LENGTH + 1U ];
    uint32_t artifact_size;
} ota_catalog_candidate_t;

typedef struct
{
    ota_catalog_result_t result;
    char latest_version[ OTA_CATALOG_VERSION_MAX_LENGTH + 1U ];
    ota_catalog_candidate_t candidate;
} ota_catalog_selection_t;

/**
 * @brief Validate a stable catalog and select the highest release directly reachable from running.
 * @details Only exact hardware, ESP-IDF target, and partition-layout matches are eligible. A newer
 *          compatible release with an unmet minimum_source_version yields OTA_CATALOG_BLOCKED.
 */
esp_err_t ota_catalog_select( const char *a_json,
                              size_t a_length,
                              const rfscout_firmware_identity_t *a_running,
                              ota_catalog_selection_t *a_selection );

#endif // _RFSCOUT_OTA_CATALOG_H_
