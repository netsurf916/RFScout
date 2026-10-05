/**
    app_framework.h : RFScout application descriptor framework
    Description: Discovers compiled-in app descriptors and retains capability availability state.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_FRAMEWORK_H_
#define _RFSCOUT_APP_FRAMEWORK_H_

#include "app_descriptor.h"

#include <stdbool.h>

/** @brief Semantic result retained by the framework after an app probe. */
typedef enum
{
    RFSCOUT_APP_AVAILABILITY_UNKNOWN = 0,
    RFSCOUT_APP_AVAILABILITY_AVAILABLE,
    RFSCOUT_APP_AVAILABILITY_UNAVAILABLE
} rfscout_app_availability_t;

/** @brief Validate descriptors and run initial hardware discovery. */
esp_err_t app_framework_init( void );

/** @brief Return one compiled-in app descriptor or NULL for an unknown ID. */
const rfscout_app_descriptor_t *app_framework_descriptor( rfscout_app_id_t a_app );

/**
 * @brief Check whether an app's required runtime capability is available.
 * @details Hot-plug hardware is refreshed on every explicit availability check.
 */
bool app_framework_check_available( rfscout_app_id_t a_app );

/** @brief Report runtime-observed availability for any probed application. */
void app_framework_report_availability( rfscout_app_id_t a_app,
                                        rfscout_app_availability_t a_availability );

/** @brief Return the most recently retained availability state. */
rfscout_app_availability_t app_framework_availability( rfscout_app_id_t a_app );

#endif // _RFSCOUT_APP_FRAMEWORK_H_
