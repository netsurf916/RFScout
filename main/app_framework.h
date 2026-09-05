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
    RFSCOUT_APP_AVAILABILITY_UNAVAILABLE,
    RFSCOUT_APP_AVAILABILITY_ERROR
} rfscout_app_availability_t;

/** @brief Validate descriptors and run startup/hot-plug discovery once. */
esp_err_t app_framework_init( void );

/** @brief Return one compiled-in app descriptor or NULL for an unknown ID. */
const rfscout_app_descriptor_t *app_framework_descriptor( rfscout_app_id_t a_app );

/** @brief Return the descriptor's user-facing application name. */
const char *app_framework_name( rfscout_app_id_t a_app );

/** @brief Check whether an app's required runtime capability is available. */
bool app_framework_check_available( rfscout_app_id_t a_app );

/** @brief Refresh one app only when its descriptor is hot-pluggable. */
void app_framework_refresh_hotplug( rfscout_app_id_t a_app );

/** @brief Report an active hot-plug app's observed runtime availability. */
void app_framework_report_availability( rfscout_app_id_t a_app,
                                        rfscout_app_availability_t a_availability );

/** @brief Return whether the app uses delayed hardware activation after entry. */
bool app_framework_requires_activation( rfscout_app_id_t a_app );

/** @brief Return the descriptor's probe policy. */
rfscout_app_probe_policy_t app_framework_probe_policy( rfscout_app_id_t a_app );

/** @brief Return the platform service mask declared by the app descriptor. */
uint32_t app_framework_services( rfscout_app_id_t a_app );

/** @brief Return the most recently retained availability state. */
rfscout_app_availability_t app_framework_availability( rfscout_app_id_t a_app );

#endif // _RFSCOUT_APP_FRAMEWORK_H_
