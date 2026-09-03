/**
    app_framework.c : RFScout application capability framework implementation
    Description: Keeps probing policy in app descriptors while device-specific detection remains in each service/driver.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_framework.h"

#include "cc1101.h"
#include "mlx90640.h"
#include "nrf24.h"
#include "recorder.h"
#include "storage.h"

#include "esp_log.h"

#include <stddef.h>

typedef esp_err_t ( *app_probe_fn_t )( void );

typedef struct
{
    const char *name;
    rfscout_app_probe_policy_t probe_policy;
    app_probe_fn_t probe;
    bool requires_activation;
    rfscout_app_availability_t availability;
} app_descriptor_t;

static const char *TAG = "app_framework";

static esp_err_t probe_recorder( void )
{
    if( !storage_is_available() )
    {
        const esp_err_t mount_result = storage_init();
        if( ESP_OK != mount_result )
        {
            return ESP_ERR_NOT_FOUND;
        }
    }

    const esp_err_t storage_result = storage_probe_card();
    if( ESP_OK != storage_result )
    {
        storage_shutdown();
        return ESP_ERR_NOT_FOUND;
    }

    return recorder_probe_microphone();
}

static app_descriptor_t descriptors[ RFSCOUT_APP_COUNT ] = {
    [RFSCOUT_APP_WIFI_BT] =
        { "WiFi / BT", RFSCOUT_APP_PROBE_NONE, NULL, true, RFSCOUT_APP_AVAILABILITY_AVAILABLE },
    [RFSCOUT_APP_CC1101] = { "CC1101",
                             RFSCOUT_APP_PROBE_STARTUP,
                             cc1101_init,
                             true,
                             RFSCOUT_APP_AVAILABILITY_UNKNOWN },
    [RFSCOUT_APP_NRF24] =
        { "nRF24", RFSCOUT_APP_PROBE_STARTUP, nrf24_init, true, RFSCOUT_APP_AVAILABILITY_UNKNOWN },
    [RFSCOUT_APP_AUDIO] = { "Recorder",
                            RFSCOUT_APP_PROBE_HOTPLUG,
                            probe_recorder,
                            false,
                            RFSCOUT_APP_AVAILABILITY_UNKNOWN },
    [RFSCOUT_APP_THERMAL] = { "Thermal",
                              RFSCOUT_APP_PROBE_HOTPLUG,
                              mlx90640_probe,
                              true,
                              RFSCOUT_APP_AVAILABILITY_UNKNOWN },
    [RFSCOUT_APP_DEVICE] =
        { "Home", RFSCOUT_APP_PROBE_NONE, NULL, false, RFSCOUT_APP_AVAILABILITY_AVAILABLE },
};

static app_descriptor_t *descriptor_for( rfscout_app_id_t a_app )
{
    if( (unsigned)a_app >= (unsigned)RFSCOUT_APP_COUNT )
    {
        return NULL;
    }
    return &descriptors[ a_app ];
}

static void retain_availability( app_descriptor_t *a_descriptor,
                                 rfscout_app_availability_t a_availability )
{
    if( NULL == a_descriptor || a_descriptor->availability == a_availability )
    {
        return;
    }

    a_descriptor->availability = a_availability;
    if( RFSCOUT_APP_AVAILABILITY_AVAILABLE == a_availability )
    {
        ESP_LOGI( TAG, "%s available", a_descriptor->name );
    }
    else if( RFSCOUT_APP_AVAILABILITY_UNAVAILABLE == a_availability )
    {
        ESP_LOGI( TAG, "%s unavailable", a_descriptor->name );
    }
    else if( RFSCOUT_APP_AVAILABILITY_ERROR == a_availability )
    {
        ESP_LOGW( TAG, "%s availability error", a_descriptor->name );
    }
}

static rfscout_app_availability_t probe_descriptor( app_descriptor_t *a_descriptor )
{
    if( NULL == a_descriptor )
    {
        return RFSCOUT_APP_AVAILABILITY_ERROR;
    }
    if( NULL == a_descriptor->probe )
    {
        retain_availability( a_descriptor, RFSCOUT_APP_AVAILABILITY_AVAILABLE );
        return a_descriptor->availability;
    }

    const esp_err_t result = a_descriptor->probe();
    rfscout_app_availability_t availability = RFSCOUT_APP_AVAILABILITY_ERROR;
    if( ESP_OK == result )
    {
        availability = RFSCOUT_APP_AVAILABILITY_AVAILABLE;
    }
    else if( ( ESP_ERR_NOT_FOUND == result ) || ( ESP_ERR_INVALID_RESPONSE == result ) ||
             ( ESP_ERR_TIMEOUT == result ) )
    {
        availability = RFSCOUT_APP_AVAILABILITY_UNAVAILABLE;
    }

    const rfscout_app_availability_t previous = a_descriptor->availability;
    retain_availability( a_descriptor, availability );
    if( previous != availability && RFSCOUT_APP_AVAILABILITY_ERROR == availability )
    {
        ESP_LOGW( TAG,
                  "%s availability probe failed: %s",
                  a_descriptor->name,
                  esp_err_to_name( result ) );
    }
    return a_descriptor->availability;
}

esp_err_t app_framework_init( void )
{
    for( int app = 0; app < RFSCOUT_APP_COUNT; ++app )
    {
        app_descriptor_t *descriptor = &descriptors[ app ];
        if( RFSCOUT_APP_PROBE_NONE == descriptor->probe_policy )
        {
            descriptor->availability = RFSCOUT_APP_AVAILABILITY_AVAILABLE;
            continue;
        }
        (void)probe_descriptor( descriptor );
    }
    return ESP_OK;
}

bool app_framework_check_available( rfscout_app_id_t a_app )
{
    app_descriptor_t *descriptor = descriptor_for( a_app );
    if( NULL == descriptor )
    {
        return false;
    }
    if( RFSCOUT_APP_PROBE_HOTPLUG == descriptor->probe_policy )
    {
        (void)probe_descriptor( descriptor );
    }
    return RFSCOUT_APP_AVAILABILITY_AVAILABLE == descriptor->availability;
}

void app_framework_refresh_hotplug( rfscout_app_id_t a_app )
{
    app_descriptor_t *descriptor = descriptor_for( a_app );
    if( ( NULL != descriptor ) && ( RFSCOUT_APP_PROBE_HOTPLUG == descriptor->probe_policy ) )
    {
        (void)probe_descriptor( descriptor );
    }
}

void app_framework_report_availability( rfscout_app_id_t a_app,
                                        rfscout_app_availability_t a_availability )
{
    app_descriptor_t *descriptor = descriptor_for( a_app );
    if( NULL == descriptor || RFSCOUT_APP_PROBE_HOTPLUG != descriptor->probe_policy ||
        RFSCOUT_APP_AVAILABILITY_UNKNOWN == a_availability )
    {
        return;
    }
    retain_availability( descriptor, a_availability );
}

bool app_framework_requires_activation( rfscout_app_id_t a_app )
{
    const app_descriptor_t *descriptor = descriptor_for( a_app );
    return ( NULL != descriptor ) && descriptor->requires_activation;
}

rfscout_app_probe_policy_t app_framework_probe_policy( rfscout_app_id_t a_app )
{
    const app_descriptor_t *descriptor = descriptor_for( a_app );
    return ( NULL != descriptor ) ? descriptor->probe_policy : RFSCOUT_APP_PROBE_NONE;
}

rfscout_app_availability_t app_framework_availability( rfscout_app_id_t a_app )
{
    const app_descriptor_t *descriptor = descriptor_for( a_app );
    return ( NULL != descriptor ) ? descriptor->availability : RFSCOUT_APP_AVAILABILITY_ERROR;
}
