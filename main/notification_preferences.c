/**
    notification_preferences.c : RFScout per-application notification preferences
    Description: Persists independent app notification gates without affecting application content audio.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "notification_preferences.h"

#include "app_audio_policy.h"

#include "nvs.h"

#include <stddef.h>
#include <stdint.h>

#define NOTIFICATION_PREFS_NAMESPACE "rfscout_sound"

static bool app_enabled[ RFSCOUT_APP_COUNT ];

static const char *app_key( rfscout_app_id_t a_app )
{
    static const char *keys[ RFSCOUT_APP_COUNT ] = {
        [RFSCOUT_APP_WIFI_BT] = "wifi_notify",
        [RFSCOUT_APP_CC1101] = "cc_notify",
        [RFSCOUT_APP_NRF24] = "nr_notify",
        [RFSCOUT_APP_AUDIO] = "audio_notify",
        [RFSCOUT_APP_THERMAL] = "thermal_notify",
        [RFSCOUT_APP_DEVICE] = "device_notify",
    };
    return ( a_app < RFSCOUT_APP_COUNT ) ? keys[ a_app ] : NULL;
}

esp_err_t notification_preferences_init( void )
{
    for( size_t index = 0U; index < RFSCOUT_APP_COUNT; ++index )
    {
        const rfscout_app_audio_policy_t *policy =
            app_audio_policy_get( (rfscout_app_id_t)index );
        app_enabled[ index ] = policy->notifications_supported && policy->notifications_enabled_default;
    }

    nvs_handle_t handle;
    esp_err_t result = nvs_open( NOTIFICATION_PREFS_NAMESPACE, NVS_READONLY, &handle );
    if( ESP_ERR_NVS_NOT_FOUND == result )
    {
        return ESP_OK;
    }
    if( ESP_OK != result )
    {
        return result;
    }

    for( size_t index = 0U; index < RFSCOUT_APP_COUNT; ++index )
    {
        const rfscout_app_audio_policy_t *policy =
            app_audio_policy_get( (rfscout_app_id_t)index );
        if( !policy->notifications_supported )
        {
            app_enabled[ index ] = false;
            continue;
        }

        uint8_t stored = app_enabled[ index ] ? 1U : 0U;
        const char *key = app_key( (rfscout_app_id_t)index );
        if( ( NULL != key ) && ( ESP_OK == nvs_get_u8( handle, key, &stored ) ) )
        {
            app_enabled[ index ] = ( 0U != stored );
        }
    }

    nvs_close( handle );
    return ESP_OK;
}

bool notification_preferences_app_enabled( rfscout_app_id_t a_app )
{
    if( a_app >= RFSCOUT_APP_COUNT )
    {
        return false;
    }
    const rfscout_app_audio_policy_t *policy = app_audio_policy_get( a_app );
    return policy->notifications_supported && app_enabled[ a_app ];
}

esp_err_t notification_preferences_set_app_enabled( rfscout_app_id_t a_app, bool a_enabled )
{
    if( a_app >= RFSCOUT_APP_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }

    const rfscout_app_audio_policy_t *policy = app_audio_policy_get( a_app );
    if( !policy->notifications_supported )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const char *key = app_key( a_app );
    if( NULL == key )
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t result = nvs_open( NOTIFICATION_PREFS_NAMESPACE, NVS_READWRITE, &handle );
    if( ESP_OK != result )
    {
        return result;
    }

    result = nvs_set_u8( handle, key, a_enabled ? 1U : 0U );
    if( ESP_OK == result )
    {
        result = nvs_commit( handle );
    }
    nvs_close( handle );

    if( ESP_OK == result )
    {
        app_enabled[ a_app ] = a_enabled;
    }
    return result;
}
