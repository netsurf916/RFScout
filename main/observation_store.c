/**
    observation_store.c : RFScout normalized observation store
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "observation_store.h"

#include "device_config.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static rf_device_t *devices;
static size_t device_count;
static SemaphoreHandle_t store_lock;
static observation_store_event_callback_t event_callback;
static void *event_context;

static uint32_t current_time_ms( void )
{
    return (uint32_t)( esp_timer_get_time() / 1000ULL );
}

static uint32_t timeout_ms_for_source( rf_source_t a_source )
{
    uint16_t seconds = RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS;
    if( RF_SOURCE_WIFI == a_source )
    {
        seconds = device_config_wifi_timeout_seconds();
    }
    else if( RF_SOURCE_BLE == a_source )
    {
        seconds = device_config_ble_timeout_seconds();
    }
    return (uint32_t)seconds * 1000U;
}

static bool same_identity( const rf_device_t *a_left, const rf_device_t *a_right )
{
    return ( a_left->address_type == a_right->address_type ) &&
           ( 0 == memcmp( a_left->address, a_right->address, 6 ) );
}

static void emit_event( observation_store_event_t a_event, const rf_device_t *a_device )
{
    if( NULL != event_callback )
    {
        event_callback( a_event, a_device, event_context );
    }
}

static void initialize_source_times( rf_device_t *a_device )
{
    if( ( 0U != ( a_device->source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) ) &&
        ( 0U == a_device->wifi_last_seen_ms ) )
    {
        a_device->wifi_last_seen_ms = a_device->last_seen_ms;
    }
    if( ( 0U != ( a_device->source_mask & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) ) &&
        ( 0U == a_device->ble_last_seen_ms ) )
    {
        a_device->ble_last_seen_ms = a_device->last_seen_ms;
    }
}

static void expire_stale_devices( uint32_t a_now_ms )
{
    const uint32_t wifi_timeout_ms = timeout_ms_for_source( RF_SOURCE_WIFI );
    const uint32_t ble_timeout_ms = timeout_ms_for_source( RF_SOURCE_BLE );
    const uint32_t fallback_timeout_ms =
        (uint32_t)RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS * 1000U;
    const uint32_t managed_mask = RF_SOURCE_BIT( RF_SOURCE_WIFI ) | RF_SOURCE_BIT( RF_SOURCE_BLE );

    size_t i = 0;
    while( i < device_count )
    {
        rf_device_t *device = &devices[ i ];
        uint32_t sources = device->source_mask;
        const uint32_t original_sources = sources;

        if( ( 0U != ( sources & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) ) &&
            ( (uint32_t)( a_now_ms - device->wifi_last_seen_ms ) >= wifi_timeout_ms ) )
        {
            sources &= ~RF_SOURCE_BIT( RF_SOURCE_WIFI );
        }
        if( ( 0U != ( sources & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) ) &&
            ( (uint32_t)( a_now_ms - device->ble_last_seen_ms ) >= ble_timeout_ms ) )
        {
            sources &= ~RF_SOURCE_BIT( RF_SOURCE_BLE );
        }

        const uint32_t other_sources = sources & ~managed_mask;
        if( ( 0U != other_sources ) &&
            ( (uint32_t)( a_now_ms - device->last_seen_ms ) >= fallback_timeout_ms ) )
        {
            sources &= managed_mask;
        }

        device->source_mask = sources;
        if( 0U != sources )
        {
            uint32_t active_last_seen = 0U;
            if( 0U != ( sources & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) )
            {
                active_last_seen = device->wifi_last_seen_ms;
            }
            if( ( 0U != ( sources & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) ) &&
                ( device->ble_last_seen_ms > active_last_seen ) )
            {
                active_last_seen = device->ble_last_seen_ms;
            }
            if( ( 0U != ( sources & ~managed_mask ) ) &&
                ( device->last_seen_ms > active_last_seen ) )
            {
                active_last_seen = device->last_seen_ms;
            }
            device->last_seen_ms = active_last_seen;

            const bool last_source_valid = ( device->last_source >= RF_SOURCE_WIFI ) &&
                                           ( device->last_source <= RF_SOURCE_IR );
            if( !last_source_valid || ( 0U == ( sources & RF_SOURCE_BIT( device->last_source ) ) ) )
            {
                if( 0U != ( sources & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) )
                {
                    device->last_source = RF_SOURCE_WIFI;
                }
                else if( 0U != ( sources & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) )
                {
                    device->last_source = RF_SOURCE_BLE;
                }
            }
            ++i;
            continue;
        }

        rf_device_t dropped = *device;
        dropped.source_mask = original_sources;
        --device_count;
        if( i != device_count )
        {
            devices[ i ] = devices[ device_count ];
        }
        memset( &devices[ device_count ], 0, sizeof( devices[ device_count ] ) );
        emit_event( OBSERVATION_STORE_EVENT_DROPPED_TIMEOUT, &dropped );
    }
}

static size_t oldest_slot( void )
{
    size_t oldest = 0;
    for( size_t i = 1; i < device_count; ++i )
    {
        if( devices[ i ].last_seen_ms < devices[ oldest ].last_seen_ms )
        {
            oldest = i;
        }
    }
    return oldest;
}

esp_err_t observation_store_init( void )
{
    device_count = 0;
    event_callback = NULL;
    event_context = NULL;
    store_lock = xSemaphoreCreateMutex();
    if( NULL == store_lock )
    {
        return ESP_ERR_NO_MEM;
    }

    devices = heap_caps_calloc(
        RFSCOUT_DEVICE_CAPACITY, sizeof( *devices ), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    if( NULL == devices )
    {
        vSemaphoreDelete( store_lock );
        store_lock = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void observation_store_set_event_callback( observation_store_event_callback_t a_callback,
                                           void *a_context )
{
    if( NULL == store_lock )
    {
        event_callback = a_callback;
        event_context = a_context;
        return;
    }

    xSemaphoreTake( store_lock, portMAX_DELAY );
    event_callback = a_callback;
    event_context = a_context;
    xSemaphoreGive( store_lock );
}

bool observation_store_observe( const rf_device_t *a_observation, bool *a_is_new )
{
    if( ( NULL == a_observation ) || ( NULL == store_lock ) || ( NULL == devices ) )
    {
        return false;
    }

    xSemaphoreTake( store_lock, portMAX_DELAY );
    expire_stale_devices( current_time_ms() );

    for( size_t i = 0; i < device_count; ++i )
    {
        if( !same_identity( &devices[ i ], a_observation ) )
        {
            continue;
        }

        const uint32_t first_seen = devices[ i ].first_seen_ms;
        const uint32_t sources = devices[ i ].source_mask | a_observation->source_mask;
        uint32_t wifi_last_seen = devices[ i ].wifi_last_seen_ms;
        uint32_t ble_last_seen = devices[ i ].ble_last_seen_ms;
        const surveillance_class_t prior_class = devices[ i ].surveillance;
        const surveillance_signature_id_t prior_signature = devices[ i ].surveillance_signature;
        const uint8_t prior_score = devices[ i ].surveillance_score;
        const uint8_t prior_evidence = devices[ i ].surveillance_evidence;
        char prior_vendor[ RFSCOUT_VENDOR_NAME_MAX ];
        char prior_name[ RFSCOUT_DEVICE_NAME_MAX ];

        if( ( RF_SOURCE_WIFI == a_observation->last_source ) ||
            ( 0U != ( a_observation->source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) ) )
        {
            wifi_last_seen = a_observation->last_seen_ms;
        }
        if( ( RF_SOURCE_BLE == a_observation->last_source ) ||
            ( 0U != ( a_observation->source_mask & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) ) )
        {
            ble_last_seen = a_observation->last_seen_ms;
        }

        memcpy( prior_vendor, devices[ i ].vendor, sizeof( prior_vendor ) );
        memcpy( prior_name, devices[ i ].name, sizeof( prior_name ) );
        const bool prior_has_name = devices[ i ].has_name;

        devices[ i ] = *a_observation;
        devices[ i ].first_seen_ms = first_seen;
        devices[ i ].source_mask = sources;
        devices[ i ].wifi_last_seen_ms = wifi_last_seen;
        devices[ i ].ble_last_seen_ms = ble_last_seen;

        if( SURVEILLANCE_NONE == devices[ i ].surveillance )
        {
            devices[ i ].surveillance = prior_class;
            devices[ i ].surveillance_signature = prior_signature;
            devices[ i ].surveillance_score = prior_score;
            devices[ i ].surveillance_evidence = prior_evidence;
        }

        if( '\0' == devices[ i ].vendor[ 0 ] )
        {
            memcpy( devices[ i ].vendor, prior_vendor, sizeof( prior_vendor ) );
        }

        if( !devices[ i ].has_name && prior_has_name )
        {
            memcpy( devices[ i ].name, prior_name, sizeof( prior_name ) );
            devices[ i ].has_name = true;
        }

        if( NULL != a_is_new )
        {
            *a_is_new = false;
        }
        xSemaphoreGive( store_lock );
        return true;
    }

    size_t slot = device_count;
    if( device_count < RFSCOUT_DEVICE_CAPACITY )
    {
        ++device_count;
    }
    else
    {
        slot = oldest_slot();
        rf_device_t dropped = devices[ slot ];
        emit_event( OBSERVATION_STORE_EVENT_DROPPED_CAPACITY, &dropped );
    }

    devices[ slot ] = *a_observation;
    initialize_source_times( &devices[ slot ] );
    emit_event( OBSERVATION_STORE_EVENT_ADDED, &devices[ slot ] );
    if( NULL != a_is_new )
    {
        *a_is_new = true;
    }
    xSemaphoreGive( store_lock );
    return true;
}

void observation_store_reset( void )
{
    if( ( NULL == store_lock ) || ( NULL == devices ) )
    {
        return;
    }

    xSemaphoreTake( store_lock, portMAX_DELAY );
    memset( devices, 0, RFSCOUT_DEVICE_CAPACITY * sizeof( *devices ) );
    device_count = 0U;
    xSemaphoreGive( store_lock );
}

void observation_store_housekeeping( void )
{
    if( ( NULL == store_lock ) || ( NULL == devices ) )
    {
        return;
    }
    xSemaphoreTake( store_lock, portMAX_DELAY );
    expire_stale_devices( current_time_ms() );
    xSemaphoreGive( store_lock );
}

size_t observation_store_count( void )
{
    if( ( NULL == store_lock ) || ( NULL == devices ) )
    {
        return 0;
    }
    xSemaphoreTake( store_lock, portMAX_DELAY );
    expire_stale_devices( current_time_ms() );
    const size_t result = device_count;
    xSemaphoreGive( store_lock );
    return result;
}

size_t observation_store_surveillance_count( void )
{
    if( ( NULL == store_lock ) || ( NULL == devices ) )
    {
        return 0;
    }
    xSemaphoreTake( store_lock, portMAX_DELAY );
    expire_stale_devices( current_time_ms() );
    size_t result = 0;
    for( size_t i = 0; i < device_count; ++i )
    {
        if( SURVEILLANCE_NONE != devices[ i ].surveillance )
        {
            ++result;
        }
    }
    xSemaphoreGive( store_lock );
    return result;
}

size_t observation_store_snapshot( rf_device_t *a_output, size_t a_capacity )
{
    if( ( NULL == a_output ) || ( 0U == a_capacity ) || ( NULL == store_lock ) ||
        ( NULL == devices ) )
    {
        return 0;
    }
    xSemaphoreTake( store_lock, portMAX_DELAY );
    expire_stale_devices( current_time_ms() );
    const size_t count = ( device_count < a_capacity ) ? device_count : a_capacity;
    memcpy( a_output, devices, count * sizeof( *a_output ) );
    xSemaphoreGive( store_lock );
    return count;
}
