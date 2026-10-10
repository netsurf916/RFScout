/**
    surveillance_history.c : Receiver-observation history
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "surveillance_history.h"
#include "gps.h"
#include "app_session.h"
#include "media_store.h"
#include "observation_store.h"

#include <string.h>
#include <stdio.h>

static surveillance_history_entry_t *entries;
static size_t entry_count;
static size_t entry_capacity;

static bool ensure_history_capacity( size_t a_required )
{
    if( a_required <= entry_capacity ) return true;
    if( entry_capacity > SIZE_MAX / 2U ) return false;
    size_t capacity = entry_capacity ? entry_capacity * 2U : 32U;
    if( capacity < a_required ) capacity = a_required;
    surveillance_history_entry_t *next = app_session_resize( entries, capacity,
                                                              sizeof( *entries ) );
    if( NULL == next ) return false;
    entries = next;
    entry_capacity = capacity;
    return true;
}

#define HISTORY_JOURNAL "surveillance.jsonl"

static esp_err_t append_entry( const rfscout_media_file_t *a_file,
                               const surveillance_history_entry_t *a_entry )
{
    char line[ 256 ];
    const int length = snprintf(
        line, sizeof( line ),
        "{\"type\":%u,\"mac\":\"%02X%02X%02X%02X%02X%02X\",\"class\":%u,"
        "\"min_lat\":%.7f,\"max_lat\":%.7f,\"min_lon\":%.7f,\"max_lon\":%.7f,"
        "\"seen\":%lu}\n",
        (unsigned)a_entry->address_type,
        a_entry->address[ 0 ], a_entry->address[ 1 ], a_entry->address[ 2 ],
        a_entry->address[ 3 ], a_entry->address[ 4 ], a_entry->address[ 5 ],
        (unsigned)a_entry->classification,
        a_entry->min_latitude, a_entry->max_latitude,
        a_entry->min_longitude, a_entry->max_longitude,
        (unsigned long)a_entry->last_seen_ms );
    if( length < 0 || (size_t)length >= sizeof( line ) ) return ESP_ERR_INVALID_SIZE;
    return media_write( a_file, line, (size_t)length );
}

bool surveillance_history_get( size_t a_index, surveillance_history_entry_t *a_entry )
{
    if( NULL == a_entry || a_index >= entry_count ) return false;
    *a_entry = entries[ a_index ];
    return a_entry->valid;
}

size_t surveillance_history_list( surveillance_history_entry_t *a_rows, size_t a_capacity )
{
    size_t count = 0U;
    for( size_t i = 0U; i < entry_count; ++i )
    {
        if( !entries[ i ].valid ) continue;
        if( NULL != a_rows && count < a_capacity ) a_rows[ count ] = entries[ i ];
        ++count;
    }
    return count;
}

static void replay_line( const char *a_line )
{
    unsigned type, classification, address[ 6 ];
    unsigned long seen;
    double min_lat, max_lat, min_lon, max_lon;
    if( 13 != sscanf( a_line,
                     "{\"type\":%u,\"mac\":\"%2x%2x%2x%2x%2x%2x\",\"class\":%u,"
                     "\"min_lat\":%lf,\"max_lat\":%lf,\"min_lon\":%lf,\"max_lon\":%lf,"
                     "\"seen\":%lu}",
                     &type, &address[ 0 ], &address[ 1 ], &address[ 2 ],
                     &address[ 3 ], &address[ 4 ], &address[ 5 ], &classification,
                     &min_lat, &max_lat, &min_lon, &max_lon, &seen ) )
    {
        return;
    }
    surveillance_history_entry_t *entry = NULL;
    for( size_t i = 0U; i < entry_count; ++i )
    {
        if( entries[ i ].address_type != type ) continue;
        bool same = true;
        for( size_t j = 0U; j < 6U; ++j )
        {
            if( entries[ i ].address[ j ] != address[ j ] ) same = false;
        }
        if( same ) { entry = &entries[ i ]; break; }
    }
    if( NULL == entry )
    {
        if( !ensure_history_capacity( entry_count + 1U ) ) return;
        entry = &entries[ entry_count++ ];
        memset( entry, 0, sizeof( *entry ) );
    }
    for( size_t j = 0U; j < 6U; ++j ) entry->address[ j ] = (uint8_t)address[ j ];
    entry->address_type = (uint8_t)type;
    entry->classification = (surveillance_class_t)classification;
    entry->min_latitude = min_lat;
    entry->max_latitude = max_lat;
    entry->min_longitude = min_lon;
    entry->max_longitude = max_lon;
    entry->last_seen_ms = (uint32_t)seen;
    entry->valid = true;
    entry->dirty = false;
}

void surveillance_history_open( void )
{
    entries = NULL;
    entry_count = 0U;
    entry_capacity = 0U;
    rfscout_media_collection_t collection;
    if( ESP_OK != media_app_collection( "locations", &collection ) ) return;
    rfscout_media_file_t file = { 0 };
    if( ESP_OK != media_open( &collection, HISTORY_JOURNAL,
                              RFSCOUT_MEDIA_OPEN_READ, &file ) ) return;
    char line[ 256 ];
    size_t used = 0U;
    bool overflow = false;
    char byte;
    size_t read = 0U;
    while( ESP_OK == media_read( &file, &byte, 1U, &read ) && 1U == read )
    {
        if( '\n' == byte )
        {
            if( !overflow && used > 0U )
            {
                line[ used ] = '\0';
                replay_line( line );
            }
            used = 0U;
            overflow = false;
        }
        else if( used + 1U < sizeof( line ) && !overflow )
        {
            line[ used++ ] = byte;
        }
        else
        {
            overflow = true;
        }
    }
    (void)media_close( &file );
}

void surveillance_history_flush( void )
{
    size_t dirty = 0U;
    for( size_t i = 0U; i < entry_count; ++i )
        if( entries[ i ].valid && entries[ i ].dirty ) ++dirty;
    if( 0U == dirty ) return;

    rfscout_media_collection_t collection;
    esp_err_t result = media_app_collection( "locations", &collection );
    rfscout_media_file_t file = { 0 };
    if( ESP_OK == result )
        result = media_open( &collection, HISTORY_JOURNAL,
                             RFSCOUT_MEDIA_OPEN_APPEND, &file );
    if( ESP_OK == result )
    {
        for( size_t i = 0U; i < entry_count; ++i )
        {
            if( !entries[ i ].valid || !entries[ i ].dirty ) continue;
            result = append_entry( &file, &entries[ i ] );
            if( ESP_OK != result ) break;
        }
        if( ESP_OK == result ) result = media_sync( &file );
        const esp_err_t close_result = media_close( &file );
        if( ESP_OK == result ) result = close_result;
    }
    /* One attempt per event; do not queue failed writes for retry. */
    for( size_t i = 0U; i < entry_count; ++i )
        entries[ i ].dirty = false;
    if( ESP_OK != result )
        printf( "surveillance history flush failed: %ld\\n", (long)result );
}

void surveillance_history_compact( void )
{
    rfscout_media_collection_t collection;
    if( ESP_OK != media_app_collection( "locations", &collection ) ) return;
    rfscout_media_file_t file = { 0 };
    if( ESP_OK != media_open( &collection, "surveillance.tmp",
                              RFSCOUT_MEDIA_OPEN_REPLACE, &file ) ) return;
    bool complete = true;
    for( size_t i = 0U; i < entry_count; ++i )
    {
        if( entries[ i ].valid && ESP_OK != append_entry( &file, &entries[ i ] ) )
        {
            complete = false;
            break;
        }
    }
    if( complete ) complete = ESP_OK == media_sync( &file );
    if( ESP_OK != media_close( &file ) ) complete = false;
    if( complete ) (void)media_rename( &collection, "surveillance.tmp", HISTORY_JOURNAL );
}

void surveillance_history_process_expired( void )
{
    rf_device_t expired[ 8 ] = { 0 };
    size_t count;
    bool changed = false;
    while( 0U != ( count = observation_store_take_expired( expired, 8U ) ) )
    {
        for( size_t i = 0U; i < count; ++i )
        {
            for( size_t j = 0U; j < entry_count; ++j )
            {
                surveillance_history_entry_t *entry = &entries[ j ];
                if( entry->valid && entry->dirty &&
                    entry->address_type == expired[ i ].address_type &&
                    0 == memcmp( entry->address, expired[ i ].address,
                                 sizeof( entry->address ) ) )
                {
                    changed = true;
                    break;
                }
            }
        }
    }
    if( changed )
    {
        surveillance_history_flush();
    }
}

void surveillance_history_sample( void )
{
    rfscout_gps_snapshot_t gps = { 0 };
    if( !gps_snapshot_get( &gps ) || !gps.location_valid ||
        gps.location_age_ms > 5000U ) return;
    const size_t total = observation_store_surveillance_devices( NULL, 0U );
    if( 0U == total ) return;
    rf_device_t *active = app_session_resize( NULL, total, sizeof( *active ) );
    if( NULL == active ) return;
    const size_t copied = observation_store_surveillance_devices( active, total );
    const size_t count = copied < total ? copied : total;
    for( size_t i = 0U; i < count; ++i )
    {
        const rf_device_t *device = &active[ i ];
        surveillance_history_entry_t *entry = NULL;
        for( size_t j = 0U; j < entry_count; ++j )
        {
            if( entries[ j ].valid && entries[ j ].address_type == device->address_type &&
                0 == memcmp( entries[ j ].address, device->address, 6U ) )
            {
                entry = &entries[ j ];
                break;
            }
        }
        if( NULL == entry )
        {
            if( !ensure_history_capacity( entry_count + 1U ) ) continue;
            entry = &entries[ entry_count++ ];
            memset( entry, 0, sizeof( *entry ) );
            memcpy( entry->address, device->address, 6U );
            entry->address_type = device->address_type;
            entry->min_latitude = entry->max_latitude = gps.latitude_deg;
            entry->min_longitude = entry->max_longitude = gps.longitude_deg;
            entry->valid = true;
        }
        else if( entry->last_seen_ms == device->last_seen_ms ) continue;
        else
        {
            if( gps.latitude_deg < entry->min_latitude ) entry->min_latitude = gps.latitude_deg;
            if( gps.latitude_deg > entry->max_latitude ) entry->max_latitude = gps.latitude_deg;
            if( gps.longitude_deg < entry->min_longitude ) entry->min_longitude = gps.longitude_deg;
            if( gps.longitude_deg > entry->max_longitude ) entry->max_longitude = gps.longitude_deg;
        }
        entry->last_seen_ms = device->last_seen_ms;
        entry->classification = device->surveillance;
        memcpy( entry->vendor, device->vendor, sizeof( entry->vendor ) );
        entry->dirty = true;
    }
    (void)app_session_free( active );
}
