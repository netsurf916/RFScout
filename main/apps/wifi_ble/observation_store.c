/**
    observation_store.c : RFScout normalized observation store
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "observation_store.h"

#include "app_session.h"
#include "device_config.h"
#include "gps.h"
#include "surveillance.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static rf_device_t *devices;
static size_t device_count;
static size_t device_capacity;
static size_t *identity_index;
static size_t identity_index_capacity;
static observation_vendor_count_t *summary_vendors;
static size_t summary_vendor_capacity;
static size_t *summary_vendor_slots;
static size_t summary_vendor_slot_capacity;
static size_t summary_vendor_count;
static uint32_t store_generation;
static SemaphoreHandle_t store_lock;
static observation_location_sink_t location_sink;
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
    identity_index = NULL;
    identity_index_capacity = 0U;
    summary_vendors = NULL;
    summary_vendor_capacity = 0U;
    summary_vendor_slots = NULL;
    summary_vendor_slot_capacity = 0U;
    summary_vendor_count = 0U;
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

static uint32_t identity_hash( const rf_device_t *a_device )
{
    uint32_t hash = 2166136261U;
    for( size_t i = 0U; i < sizeof( a_device->address ); ++i )
    {
        hash ^= a_device->address[ i ];
        hash *= 16777619U;
    }
    hash ^= a_device->address_type;
    hash *= 16777619U;
    return hash;
}

static bool same_identity( const rf_device_t *a_left, const rf_device_t *a_right )
{
    return ( a_left->address_type == a_right->address_type ) &&
           ( 0 == memcmp( a_left->address, a_right->address, 6 ) );
}

static void identity_index_insert( size_t a_device_index )
{
    if( ( NULL == identity_index ) || ( 0U == identity_index_capacity ) ||
        ( a_device_index >= device_count ) )
    {
        return;
    }

    size_t slot =
        (size_t)identity_hash( &devices[ a_device_index ] ) & ( identity_index_capacity - 1U );
    for( size_t probes = 0U; probes < identity_index_capacity; ++probes )
    {
        if( 0U == identity_index[ slot ] )
        {
            identity_index[ slot ] = a_device_index + 1U;
            return;
        }
        slot = ( slot + 1U ) & ( identity_index_capacity - 1U );
    }
}

static void rebuild_identity_index( void )
{
    if( ( NULL == identity_index ) || ( 0U == identity_index_capacity ) )
    {
        return;
    }

    memset( identity_index, 0, identity_index_capacity * sizeof( *identity_index ) );
    for( size_t i = 0U; i < device_count; ++i )
    {
        identity_index_insert( i );
    }
}

static bool ensure_identity_index_capacity( size_t a_required )
{
    size_t required_slots = a_required;
    if( required_slots <= ( SIZE_MAX / 2U ) )
    {
        required_slots *= 2U;
    }
    if( required_slots < 2U )
    {
        required_slots = 2U;
    }

    if( required_slots <= identity_index_capacity )
    {
        return true;
    }

    size_t capacity = identity_index_capacity;
    if( 0U == capacity )
    {
        capacity = RFSCOUT_OBSERVATION_INITIAL_CAPACITY * 2U;
    }
    while( capacity < required_slots )
    {
        if( capacity > ( SIZE_MAX / 2U ) )
        {
            return false;
        }
        capacity *= 2U;
    }

    size_t *replacement = app_session_resize( identity_index, capacity, sizeof( *identity_index ) );
    if( NULL == replacement )
    {
        return false;
    }

    identity_index = replacement;
    identity_index_capacity = capacity;
    store_generation = app_session_generation();
    rebuild_identity_index();
    return true;
}

static size_t find_identity( const rf_device_t *a_observation )
{
    if( ( NULL == identity_index ) || ( 0U == identity_index_capacity ) )
    {
        return SIZE_MAX;
    }

    size_t slot = (size_t)identity_hash( a_observation ) & ( identity_index_capacity - 1U );
    for( size_t probes = 0U; probes < identity_index_capacity; ++probes )
    {
        const size_t encoded_index = identity_index[ slot ];
        if( 0U == encoded_index )
        {
            return SIZE_MAX;
        }

        const size_t device_index = encoded_index - 1U;
        if( ( device_index < device_count ) &&
            same_identity( &devices[ device_index ], a_observation ) )
        {
            return device_index;
        }
        slot = ( slot + 1U ) & ( identity_index_capacity - 1U );
    }
    return SIZE_MAX;
}

static bool ensure_summary_capacity( size_t a_required )
{
    if( 0U == a_required )
    {
        return true;
    }

    size_t required_slots = a_required;
    if( required_slots <= ( SIZE_MAX / 2U ) )
    {
        required_slots *= 2U;
    }

    if( required_slots > summary_vendor_capacity )
    {
        size_t capacity = summary_vendor_capacity;
        if( 0U == capacity )
        {
            capacity = RFSCOUT_OBSERVATION_INITIAL_CAPACITY * 2U;
        }
        while( capacity < required_slots )
        {
            if( capacity > ( SIZE_MAX / 2U ) )
            {
                capacity = required_slots;
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
    }

    if( a_required > summary_vendor_slot_capacity )
    {
        size_t capacity = summary_vendor_slot_capacity;
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

        size_t *replacement =
            app_session_resize( summary_vendor_slots, capacity, sizeof( *summary_vendor_slots ) );
        if( NULL == replacement )
        {
            return false;
        }

        summary_vendor_slots = replacement;
        summary_vendor_slot_capacity = capacity;
        store_generation = app_session_generation();
    }

    return true;
}

static void clear_summary_vendors( void )
{
    for( size_t i = 0U; i < summary_vendor_count; ++i )
    {
        memset( &summary_vendors[ summary_vendor_slots[ i ] ], 0, sizeof( *summary_vendors ) );
    }
    summary_vendor_count = 0U;
}

static uint32_t vendor_hash( const char *a_name )
{
    uint32_t hash = 2166136261U;
    for( const unsigned char *current = (const unsigned char *)a_name; '\0' != *current; ++current )
    {
        hash ^= *current;
        hash *= 16777619U;
    }
    return hash;
}

static void count_vendor( const char *a_name )
{
    if( ( NULL == summary_vendors ) || ( 0U == summary_vendor_capacity ) )
    {
        return;
    }

    size_t slot = (size_t)vendor_hash( a_name ) & ( summary_vendor_capacity - 1U );
    for( size_t probes = 0U; probes < summary_vendor_capacity; ++probes )
    {
        observation_vendor_count_t *entry = &summary_vendors[ slot ];
        if( 0U == entry->count )
        {
            strlcpy( entry->name, a_name, sizeof( entry->name ) );
            entry->count = 1U;
            if( summary_vendor_count < summary_vendor_slot_capacity )
            {
                summary_vendor_slots[ summary_vendor_count++ ] = slot;
            }
            return;
        }
        if( 0 == strcmp( entry->name, a_name ) )
        {
            ++entry->count;
            return;
        }
        slot = ( slot + 1U ) & ( summary_vendor_capacity - 1U );
    }
}

static bool vendor_precedes( const observation_vendor_count_t *a_left,
                             const observation_vendor_count_t *a_right )
{
    if( a_left->count != a_right->count )
    {
        return a_left->count > a_right->count;
    }
    return strcmp( a_left->name, a_right->name ) < 0;
}

static size_t select_top_vendors( observation_vendor_count_t *a_output, size_t a_capacity )
{
    if( ( NULL == a_output ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    size_t result_count = 0U;
    memset( a_output, 0, a_capacity * sizeof( *a_output ) );

    for( size_t i = 0U; i < summary_vendor_count; ++i )
    {
        const observation_vendor_count_t *candidate = &summary_vendors[ summary_vendor_slots[ i ] ];

        size_t position = 0U;
        while( ( position < result_count ) && !vendor_precedes( candidate, &a_output[ position ] ) )
        {
            ++position;
        }
        if( position >= a_capacity )
        {
            continue;
        }

        const size_t last = ( result_count < a_capacity ) ? result_count : ( a_capacity - 1U );
        for( size_t move = last; move > position; --move )
        {
            a_output[ move ] = a_output[ move - 1U ];
        }
        a_output[ position ] = *candidate;
        if( result_count < a_capacity )
        {
            ++result_count;
        }
    }
    return result_count;
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

static void copy_history_identity( observation_history_identity_t *a_identity,
                                   const rf_device_t *a_device )
{
    memcpy( a_identity->address, a_device->address, sizeof( a_identity->address ) );
    a_identity->address_type = a_device->address_type;
    memcpy( a_identity->vendor, a_device->vendor, sizeof( a_identity->vendor ) );
    a_identity->surveillance = a_device->surveillance;
    a_identity->last_seen_ms = a_device->last_seen_ms;
    a_identity->location_valid = a_device->location_valid;
    a_identity->min_latitude = a_device->min_latitude;
    a_identity->max_latitude = a_device->max_latitude;
    a_identity->min_longitude = a_device->min_longitude;
    a_identity->max_longitude = a_device->max_longitude;
}

static void emit_event( observation_store_event_t a_event, const rf_device_t *a_device )
{
    if( NULL != event_callback )
    {
        event_callback( a_event, a_device, event_context );
    }
}

static bool has_active_wifi_ap_role( const rf_device_t *a_device )
{
    return ( NULL != a_device ) &&
           ( 0U != ( a_device->source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) ) &&
           a_device->wifi_is_ap && ( a_device->wifi_channel >= 1U ) &&
           ( a_device->wifi_channel <= RFSCOUT_WIFI_CHANNEL_MAX );
}

static void initialize_source_times( rf_device_t *a_device )
{
    if( ( 0U != ( a_device->source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) ) &&
        ( 0U == a_device->wifi_last_seen_ms ) )
    {
        a_device->wifi_last_seen_ms = a_device->last_seen_ms;
    }
    if( ( 0U != ( a_device->source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) ) &&
        a_device->wifi_is_ap && ( 0U == a_device->wifi_channel ) )
    {
        a_device->wifi_channel = (uint8_t)a_device->channel;
    }
    if( ( 0U != ( a_device->source_mask & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) ) &&
        ( 0U == a_device->ble_last_seen_ms ) )
    {
        a_device->ble_last_seen_ms = a_device->last_seen_ms;
    }
}

static bool expire_stale_devices( uint32_t a_now_ms )
{
    const uint32_t wifi_timeout_ms = timeout_ms_for_source( RF_SOURCE_WIFI );
    const uint32_t ble_timeout_ms = timeout_ms_for_source( RF_SOURCE_BLE );
    const uint32_t fallback_timeout_ms =
        (uint32_t)RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS * 1000U;
    const uint32_t managed_mask = RF_SOURCE_BIT( RF_SOURCE_WIFI ) | RF_SOURCE_BIT( RF_SOURCE_BLE );
    bool removed = false;

    size_t i = 0U;
    while( i < device_count )
    {
        rf_device_t *device = &devices[ i ];
        uint32_t sources = device->source_mask;
        const uint32_t original_sources = sources;

        const bool wifi_expired =
            ( 0U != ( sources & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) ) &&
            ( (uint32_t)( a_now_ms - device->wifi_last_seen_ms ) >= wifi_timeout_ms );
        if( wifi_expired )
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
            if( wifi_expired )
            {
                device->wifi_is_ap = false;
                device->wifi_channel = 0U;
            }

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

        device->source_mask = original_sources;
        emit_event( OBSERVATION_STORE_EVENT_DROPPED_TIMEOUT, device );
        --device_count;
        if( i != device_count )
        {
            devices[ i ] = devices[ device_count ];
        }
        memset( &devices[ device_count ], 0, sizeof( devices[ device_count ] ) );
        removed = true;
    }

    return removed;
}

esp_err_t observation_store_init( void )
{
    devices = NULL;
    device_count = 0U;
    device_capacity = 0U;
    identity_index = NULL;
    identity_index_capacity = 0U;
    summary_vendors = NULL;
    summary_vendor_capacity = 0U;
    summary_vendor_slots = NULL;
    summary_vendor_slot_capacity = 0U;
    summary_vendor_count = 0U;
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

static void copy_case_result( rf_device_t *a_observation, const rf_device_t *a_case )
{
    a_observation->surveillance = a_case->surveillance;
    a_observation->surveillance_signature = a_case->surveillance_signature;
    a_observation->surveillance_case_signature = a_case->surveillance_case_signature;
    a_observation->surveillance_evidence = a_case->surveillance_evidence;
    a_observation->surveillance_score = a_case->surveillance_score;
    memcpy( a_observation->surveillance_evidence_bits,
            a_case->surveillance_evidence_bits,
            sizeof( a_observation->surveillance_evidence_bits ) );
}

static void merge_observation( rf_device_t *a_device, const rf_device_t *a_observation )
{
    a_device->last_source = a_observation->last_source;
    a_device->source_mask |= a_observation->source_mask;
    a_device->rssi = a_observation->rssi;
    a_device->channel = a_observation->channel;
    a_device->last_seen_ms = a_observation->last_seen_ms;

    if( ( RF_SOURCE_WIFI == a_observation->last_source ) ||
        ( 0U != ( a_observation->source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) ) )
    {
        a_device->wifi_last_seen_ms = a_observation->last_seen_ms;
        /*
         * Receiver-reported channels can briefly reflect adjacent channels at a
         * hop boundary. Once AP identity and its channel are established, do
         * not reassign the AP for each later beacon/probe response. A genuine
         * AP channel migration needs separate, corroborated evidence.
         */
        if( a_observation->wifi_is_ap )
        {
            const uint8_t ap_channel = ( 0U != a_observation->wifi_channel )
                                           ? a_observation->wifi_channel
                                           : (uint8_t)a_observation->channel;
            if( ( ap_channel >= 1U ) && ( ap_channel <= RFSCOUT_WIFI_CHANNEL_MAX ) )
            {
                if( !has_active_wifi_ap_role( a_device ) )
                {
                    a_device->wifi_channel = ap_channel;
                }
                a_device->wifi_is_ap = true;
            }
        }
    }
    if( ( RF_SOURCE_BLE == a_observation->last_source ) ||
        ( 0U != ( a_observation->source_mask & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) ) )
    {
        a_device->ble_last_seen_ms = a_observation->last_seen_ms;
    }

    if( '\0' != a_observation->vendor[ 0 ] )
    {
        memcpy( a_device->vendor, a_observation->vendor, sizeof( a_device->vendor ) );
    }
    if( a_observation->has_name )
    {
        memcpy( a_device->name, a_observation->name, sizeof( a_device->name ) );
        a_device->has_name = true;
    }

    a_device->company_id = a_observation->company_id;
    memcpy( a_device->manufacturer_data,
            a_observation->manufacturer_data,
            sizeof( a_device->manufacturer_data ) );
    a_device->manufacturer_data_len = a_observation->manufacturer_data_len;
    a_device->has_manufacturer_data = a_observation->has_manufacturer_data;
    memcpy(
        a_device->service_uuids, a_observation->service_uuids, sizeof( a_device->service_uuids ) );
    a_device->service_uuid_count = a_observation->service_uuid_count;
    a_device->has_remote_id = a_observation->has_remote_id;
}

void observation_store_set_location_sink( observation_location_sink_t a_sink )
{
    if( NULL == store_lock )
        return;
    xSemaphoreTake( store_lock, portMAX_DELAY );
    location_sink = a_sink;
    xSemaphoreGive( store_lock );
}

static double eastward_degrees( double a_from, double a_to )
{
    double delta = a_to - a_from;
    if( delta < 0.0 )
        delta += 360.0;
    return delta;
}

static void extend_longitude_bounds( rf_device_t *a_device, double a_longitude )
{
    const double west = a_device->min_longitude;
    const double east = a_device->max_longitude;
    const double width = eastward_degrees( west, east );
    if( eastward_degrees( west, a_longitude ) <= width )
        return;
    const double extend_east = eastward_degrees( east, a_longitude );
    const double extend_west = eastward_degrees( a_longitude, west );
    if( extend_east <= extend_west )
        a_device->max_longitude = a_longitude;
    else
        a_device->min_longitude = a_longitude;
}

static bool record_receiver_location( rf_device_t *a_device,
                                      const rfscout_gps_snapshot_t *a_gps,
                                      observation_location_event_t *a_event )
{
    if( SURVEILLANCE_NONE == a_device->surveillance || !a_gps->location_valid ||
        a_gps->location_age_ms > 5000U )
        return false;
    if( !a_device->location_valid )
    {
        a_device->min_latitude = a_gps->latitude_deg;
        a_device->max_latitude = a_gps->latitude_deg;
        a_device->min_longitude = a_gps->longitude_deg;
        a_device->max_longitude = a_gps->longitude_deg;
        a_device->location_valid = true;
    }
    else
    {
        if( a_gps->latitude_deg < a_device->min_latitude )
            a_device->min_latitude = a_gps->latitude_deg;
        if( a_gps->latitude_deg > a_device->max_latitude )
            a_device->max_latitude = a_gps->latitude_deg;
        extend_longitude_bounds( a_device, a_gps->longitude_deg );
    }
    memcpy( a_event->address, a_device->address, sizeof( a_event->address ) );
    a_event->address_type = a_device->address_type;
    a_event->classification = a_device->surveillance;
    a_event->latitude = a_gps->latitude_deg;
    a_event->longitude = a_gps->longitude_deg;
    a_event->utc_epoch = a_gps->time_valid ? (int64_t)a_gps->utc_epoch : 0;
    a_event->seen_ms = a_device->last_seen_ms;
    return true;
}

bool observation_store_observe( rf_device_t *a_observation,
                                bool *a_is_new,
                                bool *a_surveillance_new )
{
    if( ( NULL == a_observation ) || ( NULL == store_lock ) )
    {
        return false;
    }

    if( NULL != a_surveillance_new )
    {
        *a_surveillance_new = false;
    }

    rfscout_gps_snapshot_t gps = { 0 };
    (void)gps_snapshot_get( &gps );
    observation_location_event_t location_event = { 0 };
    bool location_ready = false;
    observation_location_sink_t sink = NULL;

    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();

    const size_t existing_index = find_identity( a_observation );
    if( SIZE_MAX != existing_index )
    {
        rf_device_t *device = &devices[ existing_index ];
        const bool was_surveillance = SURVEILLANCE_NONE != device->surveillance;

        surveillance_case_observe( device, a_observation );
        merge_observation( device, a_observation );
        copy_case_result( a_observation, device );
        location_ready = record_receiver_location( device, &gps, &location_event );

        const bool became_surveillance =
            !was_surveillance && ( SURVEILLANCE_NONE != device->surveillance ) &&
            ( SURVEILLANCE_SIGNATURE_NONE != device->surveillance_signature );
        if( NULL != a_surveillance_new )
        {
            *a_surveillance_new = became_surveillance;
        }
        if( became_surveillance )
        {
            emit_event( OBSERVATION_STORE_EVENT_SURVEILLANCE_CHANGED, device );
        }

        if( NULL != a_is_new )
        {
            *a_is_new = false;
        }
        sink = location_sink;
        xSemaphoreGive( store_lock );
        if( location_ready && NULL != sink )
            sink( &location_event );
        return true;
    }

    if( !ensure_capacity( device_count + 1U ) ||
        !ensure_identity_index_capacity( device_count + 1U ) )
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
    rf_device_t *device = &devices[ slot ];
    *device = *a_observation;
    initialize_source_times( device );
    if( 0U != surveillance_evidence_count( device ) )
    {
        surveillance_case_evaluate( device );
    }
    copy_case_result( a_observation, device );
    location_ready = record_receiver_location( device, &gps, &location_event );
    identity_index_insert( slot );
    emit_event( OBSERVATION_STORE_EVENT_ADDED, device );

    if( NULL != a_is_new )
    {
        *a_is_new = true;
    }
    if( NULL != a_surveillance_new )
    {
        *a_surveillance_new = ( SURVEILLANCE_NONE != device->surveillance ) &&
                              ( SURVEILLANCE_SIGNATURE_NONE != device->surveillance_signature );
    }
    sink = location_sink;
    xSemaphoreGive( store_lock );
    if( location_ready && NULL != sink )
        sink( &location_event );
    return true;
}

void observation_store_reevaluate_surveillance( size_t *a_newly_qualified, size_t a_count_capacity )
{
    if( NULL == store_lock )
    {
        return;
    }

    if( NULL != a_newly_qualified )
    {
        memset( a_newly_qualified, 0, a_count_capacity * sizeof( *a_newly_qualified ) );
    }

    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    for( size_t i = 0U; i < device_count; ++i )
    {
        rf_device_t *device = &devices[ i ];
        const bool was_surveillance = SURVEILLANCE_NONE != device->surveillance;
        surveillance_case_evaluate( device );
        const bool became_surveillance =
            !was_surveillance && ( SURVEILLANCE_NONE != device->surveillance ) &&
            ( SURVEILLANCE_SIGNATURE_NONE != device->surveillance_signature );
        if( !became_surveillance )
        {
            continue;
        }

        emit_event( OBSERVATION_STORE_EVENT_SURVEILLANCE_CHANGED, device );
        if( ( NULL != a_newly_qualified ) && ( (size_t)device->surveillance < a_count_capacity ) )
        {
            ++a_newly_qualified[ device->surveillance ];
        }
    }
    xSemaphoreGive( store_lock );
}

size_t observation_store_surveillance_devices( observation_history_identity_t *a_devices,
                                               size_t a_capacity )
{
    if( NULL == store_lock )
    {
        return 0U;
    }
    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    size_t total = 0U;
    for( size_t i = 0U; i < device_count; ++i )
    {
        if( SURVEILLANCE_NONE == devices[ i ].surveillance )
        {
            continue;
        }
        if( NULL != a_devices && total < a_capacity )
        {
            copy_history_identity( &a_devices[ total ], &devices[ i ] );
        }
        ++total;
    }
    xSemaphoreGive( store_lock );
    return total;
}

bool observation_store_surveillance_get( size_t a_index, observation_history_identity_t *a_device )
{
    if( NULL == store_lock || NULL == a_device )
        return false;
    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    size_t index = 0U;
    bool found = false;
    for( size_t i = 0U; i < device_count; ++i )
    {
        if( SURVEILLANCE_NONE == devices[ i ].surveillance )
            continue;
        if( index++ == a_index )
        {
            copy_history_identity( a_device, &devices[ i ] );
            found = true;
            break;
        }
    }
    xSemaphoreGive( store_lock );
    return found;
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
    if( NULL != identity_index )
    {
        memset( identity_index, 0, identity_index_capacity * sizeof( *identity_index ) );
    }
    device_count = 0U;
    xSemaphoreGive( store_lock );
}

void observation_store_clear_source( rf_source_t a_source )
{
    if( ( NULL == store_lock ) ||
        ( ( RF_SOURCE_WIFI != a_source ) && ( RF_SOURCE_BLE != a_source ) ) )
    {
        return;
    }

    const uint32_t source_bit = RF_SOURCE_BIT( a_source );
    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();

    size_t i = 0U;
    while( i < device_count )
    {
        rf_device_t *device = &devices[ i ];
        if( 0U == ( device->source_mask & source_bit ) )
        {
            ++i;
            continue;
        }

        device->source_mask &= ~source_bit;
        if( RF_SOURCE_WIFI == a_source )
        {
            device->wifi_is_ap = false;
            device->wifi_channel = 0U;
            device->wifi_last_seen_ms = 0U;
        }
        else
        {
            device->ble_last_seen_ms = 0U;
        }

        if( 0U == device->source_mask )
        {
            --device_count;
            if( i != device_count )
            {
                devices[ i ] = devices[ device_count ];
            }
            memset( &devices[ device_count ], 0, sizeof( devices[ device_count ] ) );
            continue;
        }

        if( device->last_source == a_source )
        {
            device->last_source =
                ( 0U != ( device->source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) ) ? RF_SOURCE_WIFI
                                                                                    : RF_SOURCE_BLE;
        }
        device->last_seen_ms = ( device->wifi_last_seen_ms > device->ble_last_seen_ms )
                                   ? device->wifi_last_seen_ms
                                   : device->ble_last_seen_ms;
        ++i;
    }

    rebuild_identity_index();
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
    if( expire_stale_devices( current_time_ms() ) )
    {
        rebuild_identity_index();
    }
    xSemaphoreGive( store_lock );
}

static uint16_t wifi_ap_mask_locked( void )
{
    uint16_t mask = 0U;
    for( size_t i = 0U; i < device_count; ++i )
    {
        if( has_active_wifi_ap_role( &devices[ i ] ) )
        {
            mask |= (uint16_t)( 1U << ( devices[ i ].wifi_channel - 1U ) );
        }
    }
    return mask;
}

uint16_t observation_store_wifi_ap_channel_mask( void )
{
    if( NULL == store_lock )
    {
        return 0U;
    }
    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    const uint16_t result = wifi_ap_mask_locked();
    xSemaphoreGive( store_lock );
    return result;
}

size_t observation_store_count( void )
{
    if( NULL == store_lock )
    {
        return 0U;
    }
    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
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

static size_t active_surveillance_locked( observation_surveillance_signature_t *a_rows,
                                          size_t a_capacity,
                                          size_t *a_total_devices )
{
    if( NULL != a_total_devices )
    {
        *a_total_devices = 0U;
    }
    size_t total_devices = 0U;
    size_t group_count = 0U;
    const size_t signature_count = surveillance_signature_count();

    for( size_t signature = 1U; signature < signature_count; ++signature )
    {
        observation_surveillance_signature_t group = { 0 };
        group.signature = (surveillance_signature_id_t)signature;

        for( size_t i = 0U; i < device_count; ++i )
        {
            const rf_device_t *device = &devices[ i ];
            if( ( SURVEILLANCE_NONE == device->surveillance ) ||
                ( device->surveillance_signature != group.signature ) )
            {
                continue;
            }

            ++group.count;
            ++total_devices;
            if( device->last_seen_ms > group.last_seen_ms )
            {
                group.last_seen_ms = device->last_seen_ms;
            }
        }

        if( 0U == group.count )
        {
            continue;
        }

        ++group_count;
        if( ( NULL == a_rows ) || ( 0U == a_capacity ) )
        {
            continue;
        }

        size_t position = 0U;
        const size_t used = ( group_count - 1U < a_capacity ) ? group_count - 1U : a_capacity;
        while( ( position < used ) && ( a_rows[ position ].last_seen_ms >= group.last_seen_ms ) )
        {
            ++position;
        }
        if( position >= a_capacity )
        {
            continue;
        }

        const size_t last = ( used < a_capacity ) ? used : ( a_capacity - 1U );
        for( size_t move = last; move > position; --move )
        {
            a_rows[ move ] = a_rows[ move - 1U ];
        }
        a_rows[ position ] = group;
    }

    if( NULL != a_total_devices )
    {
        *a_total_devices = total_devices;
    }
    return group_count;
}

size_t observation_store_active_surveillance( observation_surveillance_signature_t *a_rows,
                                              size_t a_capacity,
                                              size_t *a_total_devices )
{
    if( NULL == store_lock )
        return 0U;
    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    const size_t result = active_surveillance_locked( a_rows, a_capacity, a_total_devices );
    xSemaphoreGive( store_lock );
    return result;
}

static size_t summary_locked( observation_store_summary_t *a_summary,
                              observation_vendor_count_t *a_vendors,
                              size_t a_vendor_capacity )
{
    memset( a_summary, 0, sizeof( *a_summary ) );
    a_summary->device_count = device_count;

    clear_summary_vendors();
    const bool collect_vendors = ( NULL != a_vendors ) && ( 0U != a_vendor_capacity ) &&
                                 ( 0U != device_count ) && ensure_summary_capacity( device_count );

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
            count_vendor( device->vendor );
        }
    }
    return collect_vendors ? select_top_vendors( a_vendors, a_vendor_capacity ) : 0U;
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
    const size_t result = summary_locked( a_summary, a_vendors, a_vendor_capacity );
    xSemaphoreGive( store_lock );
    return result;
}

void observation_store_ui_snapshot( observation_ui_snapshot_t *a_snapshot,
                                   observation_vendor_count_t *a_vendors,
                                   size_t a_vendor_capacity,
                                   observation_surveillance_signature_t *a_signatures,
                                   size_t a_signature_capacity )
{
    if( NULL == a_snapshot )
    {
        return;
    }
    memset( a_snapshot, 0, sizeof( *a_snapshot ) );
    if( NULL == store_lock )
    {
        return;
    }

    xSemaphoreTake( store_lock, portMAX_DELAY );
    synchronize_session_store();
    a_snapshot->vendor_count =
        summary_locked( &a_snapshot->summary, a_vendors, a_vendor_capacity );
    a_snapshot->signature_count = active_surveillance_locked(
        a_signatures, a_signature_capacity, &a_snapshot->surveillance_count );
    a_snapshot->ap_channel_mask = wifi_ap_mask_locked();
    xSemaphoreGive( store_lock );
}
