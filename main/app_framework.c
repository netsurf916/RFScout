/**
    app_framework.c : RFScout application descriptor framework implementation
    Description: Discovers compiled-in app descriptors and retains capability availability state.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_framework.h"

#include "apps/app_registry.h"

#include "esp_log.h"

static const char *TAG = "app_framework";
static rfscout_app_availability_t availability[ RFSCOUT_APP_COUNT ];

static bool app_id_valid( rfscout_app_id_t a_app )
{
    return (unsigned)a_app < (unsigned)RFSCOUT_APP_COUNT;
}

const rfscout_app_descriptor_t *app_framework_descriptor( rfscout_app_id_t a_app )
{
    return app_id_valid( a_app ) ? rfscout_app_registry[ a_app ] : NULL;
}

static void retain_availability( rfscout_app_id_t a_app, rfscout_app_availability_t a_availability )
{
    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_app );
    if( ( NULL == descriptor ) || ( availability[ a_app ] == a_availability ) )
    {
        return;
    }

    availability[ a_app ] = a_availability;
    if( RFSCOUT_APP_AVAILABILITY_AVAILABLE == a_availability )
    {
        ESP_LOGI( TAG, "%s available", descriptor->name );
    }
    else if( RFSCOUT_APP_AVAILABILITY_UNAVAILABLE == a_availability )
    {
        ESP_LOGI( TAG, "%s unavailable", descriptor->name );
    }
    else if( RFSCOUT_APP_AVAILABILITY_ERROR == a_availability )
    {
        ESP_LOGW( TAG, "%s availability error", descriptor->name );
    }
}

static rfscout_app_availability_t probe_descriptor( const rfscout_app_descriptor_t *a_descriptor )
{
    if( NULL == a_descriptor )
    {
        return RFSCOUT_APP_AVAILABILITY_ERROR;
    }
    if( NULL == a_descriptor->probe )
    {
        retain_availability( a_descriptor->id, RFSCOUT_APP_AVAILABILITY_AVAILABLE );
        return availability[ a_descriptor->id ];
    }

    const esp_err_t result = a_descriptor->probe();
    rfscout_app_availability_t current = RFSCOUT_APP_AVAILABILITY_ERROR;
    if( ESP_OK == result )
    {
        current = RFSCOUT_APP_AVAILABILITY_AVAILABLE;
    }
    else if( ( ESP_ERR_NOT_FOUND == result ) || ( ESP_ERR_INVALID_RESPONSE == result ) ||
             ( ESP_ERR_TIMEOUT == result ) )
    {
        current = RFSCOUT_APP_AVAILABILITY_UNAVAILABLE;
    }

    const rfscout_app_availability_t previous = availability[ a_descriptor->id ];
    retain_availability( a_descriptor->id, current );
    if( ( previous != current ) && ( RFSCOUT_APP_AVAILABILITY_ERROR == current ) )
    {
        ESP_LOGW( TAG,
                  "%s availability probe failed: %s",
                  a_descriptor->name,
                  esp_err_to_name( result ) );
    }
    return availability[ a_descriptor->id ];
}

esp_err_t app_framework_init( void )
{
    for( int app = 0; app < RFSCOUT_APP_COUNT; ++app )
    {
        const rfscout_app_descriptor_t *descriptor = rfscout_app_registry[ app ];
        availability[ app ] = RFSCOUT_APP_AVAILABILITY_UNKNOWN;
        if( ( NULL == descriptor ) || ( descriptor->id != (rfscout_app_id_t)app ) ||
            ( NULL == descriptor->name ) )
        {
            ESP_LOGE( TAG, "invalid compiled-in app descriptor id=%d", app );
            return ESP_ERR_INVALID_STATE;
        }
    }

    for( int app = 0; app < RFSCOUT_APP_COUNT; ++app )
    {
        const rfscout_app_descriptor_t *descriptor = rfscout_app_registry[ app ];
        if( RFSCOUT_APP_PROBE_NONE == descriptor->probe_policy )
        {
            availability[ app ] = RFSCOUT_APP_AVAILABILITY_AVAILABLE;
            continue;
        }
        (void)probe_descriptor( descriptor );
    }
    return ESP_OK;
}

bool app_framework_check_available( rfscout_app_id_t a_app )
{
    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_app );
    if( NULL == descriptor )
    {
        return false;
    }
    if( RFSCOUT_APP_PROBE_HOTPLUG == descriptor->probe_policy )
    {
        (void)probe_descriptor( descriptor );
    }
    return RFSCOUT_APP_AVAILABILITY_AVAILABLE == availability[ a_app ];
}

void app_framework_refresh_hotplug( rfscout_app_id_t a_app )
{
    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_app );
    if( ( NULL != descriptor ) && ( RFSCOUT_APP_PROBE_HOTPLUG == descriptor->probe_policy ) )
    {
        (void)probe_descriptor( descriptor );
    }
}

void app_framework_report_availability( rfscout_app_id_t a_app,
                                        rfscout_app_availability_t a_availability )
{
    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_app );
    if( ( NULL == descriptor ) || ( RFSCOUT_APP_PROBE_HOTPLUG != descriptor->probe_policy ) ||
        ( RFSCOUT_APP_AVAILABILITY_UNKNOWN == a_availability ) )
    {
        return;
    }
    retain_availability( a_app, a_availability );
}

bool app_framework_requires_activation( rfscout_app_id_t a_app )
{
    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_app );
    return ( NULL != descriptor ) && descriptor->requires_activation;
}

rfscout_app_availability_t app_framework_availability( rfscout_app_id_t a_app )
{
    return app_id_valid( a_app ) ? availability[ a_app ] : RFSCOUT_APP_AVAILABILITY_ERROR;
}
