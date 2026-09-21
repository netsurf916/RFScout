/**
    apps/ota/ota_catalog.h : RFScout OTA build-catalog validation and selection
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OTA_CATALOG_H_
#define _RFSCOUT_OTA_CATALOG_H_

#include "firmware_identity.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_CATALOG_BUILD_ID_MAX_LENGTH 63U
#define OTA_CATALOG_BUILD_LABEL_MAX_LENGTH 95U
#define OTA_CATALOG_VERSION_MAX_LENGTH 31U
#define OTA_CATALOG_COMMIT_LENGTH 40U
#define OTA_CATALOG_ARTIFACT_PATH_MAX_LENGTH 191U
#define OTA_CATALOG_SHA256_LENGTH 64U
#define OTA_CATALOG_VISIBLE_BUILDS 32U

typedef enum
{
    OTA_CATALOG_CURRENT = 0,
    OTA_CATALOG_UPDATE_AVAILABLE
} ota_catalog_result_t;

typedef enum
{
    OTA_CATALOG_BUILD_RELEASE = 0,
    OTA_CATALOG_BUILD_MAIN,
    OTA_CATALOG_BUILD_PULL_REQUEST
} ota_catalog_build_kind_t;

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
    ota_catalog_build_kind_t kind;
    char label[ OTA_CATALOG_BUILD_LABEL_MAX_LENGTH + 1U ];
    int32_t pull_request;
    bool stable;
    bool draft;
    bool prerelease;
    ota_catalog_candidate_t candidate;
} ota_catalog_build_t;

typedef struct
{
    ota_catalog_result_t result;
    char latest_version[ OTA_CATALOG_VERSION_MAX_LENGTH + 1U ];
    ota_catalog_candidate_t candidate;
} ota_catalog_selection_t;

esp_err_t ota_catalog_select( const char *a_json,
                              size_t a_length,
                              const rfscout_firmware_identity_t *a_running,
                              ota_catalog_selection_t *a_selection );

esp_err_t ota_catalog_list( const char *a_json,
                            size_t a_length,
                            const rfscout_firmware_identity_t *a_running,
                            ota_catalog_build_t *a_builds,
                            size_t a_capacity,
                            size_t *a_count );

#endif // _RFSCOUT_OTA_CATALOG_H_
