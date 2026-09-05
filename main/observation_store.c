/**
    observation_store.c : RFScout normalized observation store
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "observation_store.h"

#include "app_session.h"
#include "device_config.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdlib.h>
#include <string.h>

static rf_device_t *devices;
static size_t device_count;
static size_t device_capacity;
static observation_vendor_count_t *summary_vendors;
static size_t summary_vendor_capacity;
static uint32_t store_generation;
static SemaphoreHandle_t store_lock;
static observation_store_event_callback_t event_callback;
static void *event_context;

static void synchronize_session_store( void )
{
    const uint32_t generation = app_session_generation();
    if( store_generation == generation )
    {
        return;
    }

    devices = NULL;
    device_count = 0U;
    device_capacity = 0U;
    summary_vendors = NULL;
    summary_vendor_capacity = 0U;
    store_generation = generation;
}

static bool ensure_capacity( size_t a_required )
{
    if( a_required <= device_capacity )
    {
        return true;
    }

    size_t capacity = device_capacity;
    if( 0U == capacity )
    {
        capacity = RFSCOUT_OBSERVATION_INITIAL_CAPACITY;
    }
    while( capacity < a_required )
    {
        if( capacity > ( SIZE_MAX / 2U ) )
        {
            capacity = a_required;
            break;
        }
        capacity *= 2U;
    }

    rf_device_t *replacement = app_session_resize( devices, capacity, sizeof( *devices ) );
    if( NULL == replacement )
    {
        return false;
    }

    devices = replacement;
    device_capacity = capacity;
    store_generation = app_session_generation();
    return true;
}

static bool ensure_summary_capacity( size_t a_required )
{
    if( a_required <= summary_vendor_capacity )
    {
        return true;
    }

    size_t capacity = summary_vendor_capacity;
    if( 0U == capacity )
    {
        capacity = RFSCOUT_OBSERVATION_INITIAL_CAPACITY;
    }
    while( capacity < a_required )
    {
        if( capacity > ( SIZE_MAX / 2U ) )
        {
            capacity = a_required;
            break;
        }
        capacity *= 2U;
    }

    observation_vendor_count_t *replacement =
        app_session_resize( summary_vendors, capacity, sizeof( *summary_vendors ) );
    if( NULL == replacement )
    {
        return false;
    }

    summary_vendors = replacement;
    summary_vendor_capacity = capacity;
    store_generation = app_session_generation();
    return true;
}

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

    size_t i = 0U;
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

static int compare_vendor_name( const void *a_left, const void *a_right )
{
    const observation_vendor_count_t *left = a_left;
    const observation_vendor_count_t *right = a_right;
    return strcmp( left->name, right->name );
}

static int compare_vendor_count( const void *a_left, const void *a_right )
{
    const observation_vendor_count_t *left = a_left;
    const observation_vendor_count_t *right = a_right;
    if( left->count > right->count )
    {
        return -1;
    }
    if( left->count < right->count )
    {
        return 1;
    }
    return strcmp( left->name, right->name );
}

esp_err_t observation_store_init( void )
{
    devices = NULL;
    device_count = 0U;
    device_capacity = 0U;
    summary_vendors = NULL;
    summary_vendor_capacity = 0U;
    store_generation = app_session_generation();
    event_callback = NULL;
    event_context = NULL;
    store_lock = xSemaphoreCreateMutex();
    return ( NULL != store_lock ) ? ESP_OK : ESP_ERR_NO_MEM;
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
    if( ( NULL == a_observation ) || ( NULL == store_lock ) )
    {
        return false;
    }

    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    expire_stale_devices( current_time_ms() );

    for( size_t i = 0U; i < device_count; ++i )
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

    if( !ensure_capacity( device_count + 1U ) )
    {
        if( NULL != a_is_new )
        {
            *a_is_new = false;
        }
        emit_event( OBSERVATION_STORE_EVENT_DROPPED_CAPACITY, a_observation );
        xSemaphoreGive( store_lock );
        return false;
    }

    const size_t slot = device_count++;
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
    if( NULL == store_lock )
    {
        return;
    }

    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    if( NULL != devices )
    {
        memset( devices, 0, device_capacity * sizeof( *devices ) );
    }
    device_count = 0U;
    xSemaphoreGive( store_lock );
}

void observation_store_housekeeping( void )
{
    if( NULL == store_lock )
    {
        return;
    }
    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    expire_stale_devices( current_time_ms() );
    xSemaphoreGive( store_lock );
}

size_t observation_store_count( void )
{
    if( NULL == store_lock )
    {
        return 0U;
    }
    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    expire_stale_devices( current_time_ms() );
    const size_t result = device_count;
    xSemaphoreGive( store_lock );
    return result;
}

size_t observation_store_surveillance_count( void )
{
    if( NULL == store_lock )
    {
        return 0U;
    }
    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    expire_stale_devices( current_time_ms() );
    size_t result = 0U;
    for( size_t i = 0U; i < device_count; ++i )
    {
        if( SURVEILLANCE_NONE != devices[ i ].surveillance )
        {
            ++result;
        }
    }
    xSemaphoreGive( store_lock );
    return result;
}

size_t observation_store_summary( observation_store_summary_t *a_summary,
                                  observation_vendor_count_t *a_vendors,
                                  size_t a_vendor_capacity )
{
    if( ( NULL == a_summary ) || ( NULL == store_lock ) )
    {
        return 0U;
    }

    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    expire_stale_devices( current_time_ms() );
    memset( a_summary, 0, sizeof( *a_summary ) );
    a_summary->device_count = device_count;

    const bool collect_vendors = ( NULL != a_vendors ) && ( 0U != a_vendor_capacity ) &&
                                 ( 0U != device_count ) && ensure_summary_capacity( device_count );
    size_t vendor_observation_count = 0U;

    for( size_t i = 0U; i < device_count; ++i )
    {
        const rf_device_t *device = &devices[ i ];
        if( 0U != ( device->source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) )
        {
            ++a_summary->wifi_count;
        }
        if( 0U != ( device->source_mask & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) )
        {
            ++a_summary->ble_count;
        }

        if( rf_device_address_is_randomized( device ) )
        {
            ++a_summary->random_count;
            continue;
        }
        if( '\0' == device->vendor[ 0 ] )
        {
            ++a_summary->unknown_count;
            continue;
        }
        if( collect_vendors )
        {
            strlcpy( summary_vendors[ vendor_observation_count ].name,
                     device->vendor,
                     sizeof( summary_vendors[ vendor_observation_count ].name ) );
            summary_vendors[ vendor_observation_count ].count = 1U;
            ++vendor_observation_count;
        }
    }
    xSemaphoreGive( store_lock );

    if( !collect_vendors || ( 0U == vendor_observation_count ) )
    {
        return 0U;
    }

    qsort( summary_vendors,
           vendor_observation_count,
           sizeof( *summary_vendors ),
           compare_vendor_name );

    size_t unique_count = 0U;
    for( size_t i = 0U; i < vendor_observation_count; ++i )
    {
        if( ( 0U == unique_count ) || ( 0 != strcmp( summary_vendors[ unique_count - 1U ].name,
                                                     summary_vendors[ i ].name ) ) )
        {
            if( unique_count != i )
            {
                summary_vendors[ unique_count ] = summary_vendors[ i ];
            }
            summary_vendors[ unique_count ].count = 1U;
            ++unique_count;
        }
        else
        {
            ++summary_vendors[ unique_count - 1U ].count;
        }
    }

    qsort( summary_vendors, unique_count, sizeof( *summary_vendors ), compare_vendor_count );
    const size_t result = ( unique_count < a_vendor_capacity ) ? unique_count : a_vendor_capacity;
    memcpy( a_vendors, summary_vendors, result * sizeof( *a_vendors ) );
    return result;
}
