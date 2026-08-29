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

typedef enum
{
    RFSCOUT_WIFI_DETECTION_WIFI_DEVICE = 0,
    RFSCOUT_WIFI_DETECTION_BLE_DEVICE,
    RFSCOUT_WIFI_DETECTION_COUNT,
} rfscout_wifi_detection_event_t;

/** @brief Initialize the notification preference facade after device configuration setup. */
esp_err_t notification_preferences_init( void );

/** @brief Return whether an application is currently allowed to emit notifications. */
bool notification_preferences_app_enabled( rfscout_app_id_t a_app );

/**
 * @brief Change one application's notification gate in runtime configuration.
 * @note Applications that do not support notifications always remain disabled.
 */
esp_err_t notification_preferences_set_app_enabled( rfscout_app_id_t a_app, bool a_enabled );

/** @brief Return whether one ordinary WiFi/BT discovery event may produce general feedback. */
bool notification_preferences_wifi_detection_enabled( rfscout_wifi_detection_event_t a_event );

/** @brief Change whether one ordinary WiFi/BT discovery event may produce general feedback. */
esp_err_t
notification_preferences_set_wifi_detection_enabled( rfscout_wifi_detection_event_t a_event,
                                                     bool a_enabled );

#endif // _RFSCOUT_NOTIFICATION_PREFERENCES_H_
