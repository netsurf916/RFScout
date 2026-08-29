/**
    notification_preferences.h : RFScout per-application notification preferences
    Description: Exposes app notification gates from the canonical device configuration.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_NOTIFICATION_PREFERENCES_H_
#define _RFSCOUT_NOTIFICATION_PREFERENCES_H_

#include "app_manager.h"
#include "esp_err.h"

#include <stdbool.h>

/** @brief Initialize the notification preference facade after device configuration setup. */
esp_err_t notification_preferences_init( void );

/** @brief Return whether an application is currently allowed to emit notifications. */
bool notification_preferences_app_enabled( rfscout_app_id_t a_app );

/**
 * @brief Change one application's notification gate in runtime configuration.
 * @note Applications that do not support notifications always remain disabled.
 */
esp_err_t notification_preferences_set_app_enabled( rfscout_app_id_t a_app, bool a_enabled );

#endif // _RFSCOUT_NOTIFICATION_PREFERENCES_H_
