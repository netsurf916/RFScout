/**
    app_framework.h : RFScout application capability framework
    Description: Declares app-owned availability probing policies and cached capability state.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_FRAMEWORK_H_
#define _RFSCOUT_APP_FRAMEWORK_H_

#include "app_manager.h"

#include "esp_err.h"

#include <stdbool.h>

/** @brief When an application's hardware availability should be probed. */
typedef enum
{
    RFSCOUT_APP_PROBE_NONE = 0,
    RFSCOUT_APP_PROBE_STARTUP,
    RFSCOUT_APP_PROBE_HOTPLUG
} rfscout_app_probe_policy_t;

/** @brief Semantic result retained by the framework after an app probe. */
typedef enum
{
    RFSCOUT_APP_AVAILABILITY_UNKNOWN = 0,
    RFSCOUT_APP_AVAILABILITY_AVAILABLE,
    RFSCOUT_APP_AVAILABILITY_UNAVAILABLE,
    RFSCOUT_APP_AVAILABILITY_ERROR
} rfscout_app_availability_t;

/** @brief Initialize app descriptors and run startup/hot-plug discovery once. */
esp_err_t app_framework_init( void );

/**
 * @brief Check whether an app's required runtime capability is available.
 *
 * Startup-cached capabilities return their boot-time result. Hot-pluggable
 * capabilities execute their app-owned probe before returning.
 */
bool app_framework_check_available( rfscout_app_id_t a_app );

/** @brief Refresh one app only when its descriptor is hot-pluggable. */
void app_framework_refresh_hotplug( rfscout_app_id_t a_app );

/** @brief Return whether the app uses delayed hardware activation after entry. */
bool app_framework_requires_activation( rfscout_app_id_t a_app );

/** @brief Return the descriptor's probe policy. */
rfscout_app_probe_policy_t app_framework_probe_policy( rfscout_app_id_t a_app );

/** @brief Return the most recently retained availability state. */
rfscout_app_availability_t app_framework_availability( rfscout_app_id_t a_app );

#endif // _RFSCOUT_APP_FRAMEWORK_H_
