/**
    hotspot_sightings.c : Bounded journal of eligible but not necessarily validated APs
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "hotspot_sightings.h"

#include "app_session.h"
#include "device_config.h"
#include "geo.h"
#include "media_journal.h"
#include "observation_journal.h"

#include "esp_log.h"

#include <string.h>

#define SIGHTINGS_COLLECTION "sightings"
#define SIGHTINGS_PREFIX "hotspots"
#define SIGHTINGS_SEGMENT_BYTES ( 256U * 1024U )
#define SIGHTINGS_MAX_SEGMENTS 32U
#define SIGHTINGS_TRACKED_CAPACITY 128U
#define SIGHTINGS_PENDING_CAPACITY 32U
#define SIGHTINGS_FLUSH_INTERVAL_US 1000000LL

typedef struct
{
    uint8_t bssid[ 6 ];
    int32_t auth_mode;
    geo_movement_gate_t location_gate;
    bool valid;
} sighting_gate_t;

typedef struct
{
    observation_journal_record_t observation;
    char ssid[ RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U ];
    int32_t auth_mode;
    rfscout_wifi_station_security_t security;
} sighting_record_t;

static const char *TAG = "hotspot_sightings";
static media_journal_t journal;
static sighting_gate_t *gates;
static sighting_record_t *pending;
static size_t gate_count;
static size_t next_gate;
static size_t pending_count;
static int64_t next_flush_us;
static bool journal_bound;

static int format_sighting( void *a_context, size_t a_index, char *a_line, size_t a_capacity )
{
    const sighting_record_t *record = &( (const sighting_record_t *)a_context )[ a_index ];
    int length = observation_journal_format_prefix( &record->observation, a_line, a_capacity );
    length = media_journal_printf( a_line, a_capacity, length, ",\"ssid\":" );
    if( length < 0 )
    {
        return -1;
    }
    const int written = media_journal_json_string(
        &a_line[ length ], a_capacity - (size_t)length, record->ssid, strlen( record->ssid ) );
    if( written < 0 )
    {
        return -1;
    }
    return media_journal_printf( a_line,
                                 a_capacity,
                                 length + written,
                                 ",\"auth_mode\":%ld,\"security\":%u}",
                                 (long)record->auth_mode,
                                 (unsigned)record->security );
}

static void flush( void )
{
    if( !journal_bound || 0U == pending_count )
    {
        return;
    }
    const esp_err_t result = media_journal_append( &journal, format_sighting, pending, pending_count );
    if( ESP_OK == result || media_journal_outcome_uncertain( &journal ) )
    {
        pending_count = 0U;
    }
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "sightings flush failed: %s", esp_err_to_name( result ) );
    }
}

esp_err_t hotspot_sightings_open( void )
{
    hotspot_sightings_close();
    gates = app_session_calloc( SIGHTINGS_TRACKED_CAPACITY, sizeof( *gates ) );
    pending = app_session_calloc( SIGHTINGS_PENDING_CAPACITY, sizeof( *pending ) );
    if( NULL == gates || NULL == pending )
    {
        return ESP_ERR_NO_MEM;
    }
    rfscout_media_collection_t collection;
    esp_err_t result = media_app_collection( SIGHTINGS_COLLECTION, &collection );
    if( ESP_OK == result )
    {
        result = media_journal_init(
            &journal, &collection, SIGHTINGS_PREFIX, SIGHTINGS_SEGMENT_BYTES, SIGHTINGS_MAX_SEGMENTS );
    }
    journal_bound = ESP_OK == result;
    return result;
}

void hotspot_sightings_observe( const rfscout_wifi_station_bss_t *a_bss,
                                const rfscout_observation_metadata_t *a_metadata,
                                bool a_eligible )
{
    if( !journal_bound || !a_eligible || NULL == a_bss || NULL == a_metadata )
    {
        return;
    }
    size_t slot = gate_count;
    for( size_t index = 0U; index < gate_count; ++index )
    {
        if( 0 == memcmp( gates[ index ].bssid, a_bss->bssid, 6U ) &&
            gates[ index ].auth_mode == a_bss->auth_mode )
        {
            slot = index;
            break;
        }
    }
    const bool first = slot == gate_count;
    geo_movement_gate_t gate = first ? ( geo_movement_gate_t ){ 0 } : gates[ slot ].location_gate;
    const bool moved = a_metadata->location_valid &&
                       geo_movement_gate_accept( &gate,
                                                 a_metadata->latitude,
                                                 a_metadata->longitude,
                                                 device_config_get()->gps_movement_m );
    if( !first && !moved )
    {
        return;
    }
    if( pending_count == SIGHTINGS_PENDING_CAPACITY )
    {
        flush();
        if( pending_count == SIGHTINGS_PENDING_CAPACITY )
        {
            return; /* Preserve a bounded retry batch rather than grow memory. */
        }
    }

    sighting_record_t *record = &pending[ pending_count++ ];
    memset( record, 0, sizeof( *record ) );
    record->observation.source = RF_SOURCE_WIFI;
    memcpy( record->observation.address, a_bss->bssid, 6U );
    record->observation.seen_ms = a_metadata->seen_ms;
    record->observation.utc_epoch = a_metadata->utc_epoch;
    record->observation.rssi = a_bss->rssi;
    record->observation.channel = a_bss->channel;
    record->observation.location_valid = a_metadata->location_valid;
    record->observation.latitude = a_metadata->latitude;
    record->observation.longitude = a_metadata->longitude;
    memcpy( record->ssid, a_bss->ssid, sizeof( record->ssid ) );
    record->auth_mode = a_bss->auth_mode;
    record->security = a_bss->security;

    if( first )
    {
        slot = gate_count < SIGHTINGS_TRACKED_CAPACITY ? gate_count++ : next_gate;
        if( gate_count == SIGHTINGS_TRACKED_CAPACITY )
        {
            next_gate = ( slot + 1U ) % SIGHTINGS_TRACKED_CAPACITY;
        }
    }
    memcpy( gates[ slot ].bssid, a_bss->bssid, 6U );
    gates[ slot ].auth_mode = a_bss->auth_mode;
    gates[ slot ].location_gate = gate;
    gates[ slot ].valid = true;
}

void hotspot_sightings_poll( int64_t a_now_us )
{
    if( journal_bound && a_now_us >= next_flush_us )
    {
        next_flush_us = a_now_us + SIGHTINGS_FLUSH_INTERVAL_US;
        flush();
    }
}

void hotspot_sightings_close( void )
{
    flush();
    journal_bound = false;
    gates = NULL;
    pending = NULL;
    gate_count = 0U;
    next_gate = 0U;
    pending_count = 0U;
    next_flush_us = 0;
}
