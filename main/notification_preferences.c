/**
    notification_preferences.c : RFScout per-application notification preferences
    Description: Exposes app notification gates stored in the canonical device configuration.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "notification_preferences.h"

#include "device_config.h"

esp_err_t notification_preferences_init( void )
{
    return ESP_OK;
}

bool notification_preferences_app_enabled( rfscout_app_id_t a_app )
{
    return device_config_notification_app_enabled( a_app );
}

esp_err_t notification_preferences_set_app_enabled( rfscout_app_id_t a_app, bool a_enabled )
{
    return device_config_set_notification_app_enabled( a_app, a_enabled );
}
