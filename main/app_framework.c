/**
    app_framework.c : RFScout application descriptor framework implementation
    Description: Discovers compiled-in app descriptors and retains capability availability state.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_framework.h"

#include "apps/app_registry.h"
#include "audio_capture.h"
#include "board.h"
#include "event.h"
#include "media_store.h"
#include "persistent_store.h"

#include "esp_log.h"

#include <stddef.h>
#include <string.h>

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
    if( NULL == descriptor )
    {
        return;
    }

    const rfscout_app_availability_t previous = availability[ a_app ];
    if( previous == a_availability )
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
    if( RFSCOUT_APP_AVAILABILITY_UNKNOWN == previous )
    {
        rfscout_event_emit( RFSCOUT_EVENT_APP_DISCOVERED, (uint8_t)a_app, (int32_t)a_availability );
    }
    else if( RFSCOUT_APP_AVAILABILITY_AVAILABLE == a_availability )
    {
        rfscout_event_emit( RFSCOUT_EVENT_APP_AVAILABLE, (uint8_t)a_app, (int32_t)a_availability );
    }
    else if( RFSCOUT_APP_AVAILABILITY_UNAVAILABLE == a_availability )
    {
        rfscout_event_emit(
            RFSCOUT_EVENT_APP_UNAVAILABLE, (uint8_t)a_app, (int32_t)a_availability );
    }
}

static rfscout_app_availability_t probe_descriptor( const rfscout_app_descriptor_t *a_descriptor )
{
    if( NULL == a_descriptor )
    {
        return RFSCOUT_APP_AVAILABILITY_UNAVAILABLE;
    }
    if( NULL == a_descriptor->probe )
    {
        retain_availability( a_descriptor->id, RFSCOUT_APP_AVAILABILITY_AVAILABLE );
        return availability[ a_descriptor->id ];
    }

    const esp_err_t result = a_descriptor->probe();
    if( ESP_ERR_INVALID_STATE == result )
    {
        /* A shared provider can be legitimately busy in another active app. A deferred probe
           must not turn resource contention into a false hardware-removal event. */
        return availability[ a_descriptor->id ];
    }

    const rfscout_app_availability_t current = ( ESP_OK == result )
                                                   ? RFSCOUT_APP_AVAILABILITY_AVAILABLE
                                                   : RFSCOUT_APP_AVAILABILITY_UNAVAILABLE;

    const rfscout_app_availability_t previous = availability[ a_descriptor->id ];
    retain_availability( a_descriptor->id, current );
    if( ( ESP_OK != result ) && ( previous != current ) && ( ESP_ERR_NOT_FOUND != result ) &&
        ( ESP_ERR_INVALID_RESPONSE != result ) && ( ESP_ERR_TIMEOUT != result ) )
    {
        ESP_LOGW( TAG,
                  "%s availability probe failed: %s",
                  a_descriptor->name,
                  esp_err_to_name( result ) );
    }
    return availability[ a_descriptor->id ];
}

static bool persistence_identity_valid( const rfscout_app_descriptor_t *a_descriptor )
{
    if( NULL == a_descriptor || NULL == a_descriptor->persistence_key ||
        '\0' == a_descriptor->persistence_key[ 0 ] )
    {
        return false;
    }
    const size_t length =
        strnlen( a_descriptor->persistence_key, RFSCOUT_PERSISTENT_KEY_MAX_BYTES + 1U );
    /* The persistence key is also the app's readable SD media owner directory. */
    return length > 0U && length <= RFSCOUT_PERSISTENT_KEY_MAX_BYTES &&
           media_owner_name_valid( a_descriptor->persistence_key );
}

esp_err_t app_framework_init( void )
{
    if( !board_profile_selected() )
    {
        return ESP_ERR_INVALID_STATE;
    }
    for( int app = 0; app < RFSCOUT_APP_COUNT; ++app )
    {
        const rfscout_app_descriptor_t *descriptor = rfscout_app_registry[ app ];
        availability[ app ] = RFSCOUT_APP_AVAILABILITY_UNKNOWN;
        if( ( NULL == descriptor ) || ( descriptor->id != (rfscout_app_id_t)app ) ||
            ( NULL == descriptor->name ) || ( NULL == descriptor->runtime ) ||
            !persistence_identity_valid( descriptor ) )
        {
            ESP_LOGE( TAG, "invalid compiled-in app descriptor id=%d", app );
            return ESP_ERR_INVALID_STATE;
        }
        for( int previous = 0; previous < app; ++previous )
        {
            const rfscout_app_descriptor_t *other = rfscout_app_registry[ previous ];
            if( NULL != other && NULL != other->persistence_key &&
                0 == strcmp( descriptor->persistence_key, other->persistence_key ) )
            {
                ESP_LOGE( TAG,
                          "duplicate app persistence key '%s' ids=%d,%d",
                          descriptor->persistence_key,
                          previous,
                          app );
                return ESP_ERR_INVALID_STATE;
            }
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

    unsigned available_count = 0U;
    unsigned unavailable_count = 0U;
    unsigned deferred_count = 0U;
    for( int app = 0; app < RFSCOUT_APP_COUNT; ++app )
    {
        const rfscout_app_descriptor_t *descriptor = rfscout_app_registry[ app ];
        const rfscout_app_availability_t state = availability[ app ];
        if( RFSCOUT_APP_AVAILABILITY_AVAILABLE == state )
        {
            ++available_count;
        }
        else if( RFSCOUT_APP_AVAILABILITY_UNAVAILABLE == state )
        {
            ++unavailable_count;
        }
        else
        {
            ++deferred_count;
        }
        ESP_LOGI( TAG, "POST: app=%s availability=%s", descriptor->name,
                  ( RFSCOUT_APP_AVAILABILITY_AVAILABLE == state )    ? "available"
                  : ( RFSCOUT_APP_AVAILABILITY_UNAVAILABLE == state ) ? "unavailable"
                                                                      : "deferred" );
    }
    ESP_LOGI( TAG, "POST: apps available=%u unavailable=%u deferred=%u", available_count,
              unavailable_count, deferred_count );
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

void app_framework_report_availability( rfscout_app_id_t a_app,
                                        rfscout_app_availability_t a_availability )
{
    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_app );
    if( ( NULL == descriptor ) || ( RFSCOUT_APP_PROBE_NONE == descriptor->probe_policy ) ||
        ( RFSCOUT_APP_AVAILABILITY_UNKNOWN == a_availability ) )
    {
        return;
    }
    retain_availability( a_app, a_availability );
}

rfscout_app_availability_t app_framework_availability( rfscout_app_id_t a_app )
{
    return app_id_valid( a_app ) ? availability[ a_app ] : RFSCOUT_APP_AVAILABILITY_UNAVAILABLE;
}
