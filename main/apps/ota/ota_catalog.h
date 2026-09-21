/**
    apps/ota/ota_catalog.h : RFScout OTA build-catalog validation and stable selection
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

#define OTA_CATALOG_BUILD_ID_MAX_LENGTH 63U
#define OTA_CATALOG_VERSION_MAX_LENGTH 31U
#define OTA_CATALOG_COMMIT_LENGTH 40U
#define OTA_CATALOG_ARTIFACT_PATH_MAX_LENGTH 191U
#define OTA_CATALOG_SHA256_LENGTH 64U
#define OTA_CATALOG_MAX_BUILDS 32U

typedef enum
{
    OTA_CATALOG_CURRENT = 0,
    OTA_CATALOG_UPDATE_AVAILABLE
} ota_catalog_result_t;

typedef struct
{
    char build_id[ OTA_CATALOG_BUILD_ID_MAX_LENGTH + 1U ];
    char version[ OTA_CATALOG_VERSION_MAX_LENGTH + 1U ];
    char commit[ OTA_CATALOG_COMMIT_LENGTH + 1U ];
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
 * @brief Validate the unified OTA build catalog and select the newest compatible stable release.
 * @details The catalog may also contain main, pull-request, draft, and prerelease builds for
 *          explicit/manual selection. Stable selection accepts only entries marked stable and
 *          still requires exact project, hardware, ESP-IDF target, and partition-layout identity.
 */
esp_err_t ota_catalog_select( const char *a_json,
                              size_t a_length,
                              const rfscout_firmware_identity_t *a_running,
                              ota_catalog_selection_t *a_selection );

#endif // _RFSCOUT_OTA_CATALOG_H_
