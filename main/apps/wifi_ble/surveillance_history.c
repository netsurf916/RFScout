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

void surveillance_history_open( void )
{
    entries = NULL;
    entry_count = 0U;
    entry_capacity = 0U;
}

void surveillance_history_flush( void )
{
    rfscout_media_collection_t collection;
    if( ESP_OK != media_app_collection( "locations", &collection ) ) return;
    rfscout_media_file_t file = { 0 };
    if( ESP_OK != media_open( &collection, HISTORY_JOURNAL,
                              RFSCOUT_MEDIA_OPEN_APPEND, &file ) ) return;
    for( size_t i = 0U; i < HISTORY_CAPACITY; ++i )
    {
        if( entries[ i ].valid && entries[ i ].dirty &&
            ESP_OK == append_entry( &file, &entries[ i ] ) )
        {
            entries[ i ].dirty = false;
        }
    }
    (void)media_sync( &file );
    (void)media_close( &file );
}

void surveillance_history_compact( void )
{
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
            for( size_t j = 0U; j < HISTORY_CAPACITY; ++j )
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
    if( !gps_snapshot_get( &gps ) || !gps.location_valid ) return;
    rf_device_t active[ 32 ] = { 0 };
    const size_t total = observation_store_surveillance_devices( active, 32U );
    const size_t count = total < 32U ? total : 32U;
    for( size_t i = 0U; i < count; ++i )
    {
        const rf_device_t *device = &active[ i ];
        surveillance_history_entry_t *entry = NULL;
        for( size_t j = 0U; j < HISTORY_CAPACITY; ++j )
        {
            if( entries[ j ].valid && entries[ j ].address_type == device->address_type &&
                0 == memcmp( entries[ j ].address, device->address, sizeof( device->address ) ) )
            {
                entry = &entries[ j ];
                break;
            }
        }
        if( NULL == entry )
        {
            for( size_t j = 0U; j < HISTORY_CAPACITY; ++j )
            {
                if( !entries[ j ].valid )
                {
                    entry = &entries[ j ];
                    break;
                }
            }
        }
        if( NULL == entry ) continue;
        if( !entry->valid )
        {
            memcpy( entry->address, device->address, sizeof( device->address ) );
            entry->address_type = device->address_type;
            entry->min_latitude = entry->max_latitude = gps.latitude_deg;
            entry->min_longitude = entry->max_longitude = gps.longitude_deg;
            entry->valid = true;
        }
        else if( entry->last_seen_ms != device->last_seen_ms )
        {
            if( gps.latitude_deg < entry->min_latitude ) entry->min_latitude = gps.latitude_deg;
            if( gps.latitude_deg > entry->max_latitude ) entry->max_latitude = gps.latitude_deg;
            if( gps.longitude_deg < entry->min_longitude ) entry->min_longitude = gps.longitude_deg;
            if( gps.longitude_deg > entry->max_longitude ) entry->max_longitude = gps.longitude_deg;
        }
        else continue;
        entry->last_seen_ms = device->last_seen_ms;
        entry->classification = device->surveillance;
        memcpy( entry->vendor, device->vendor, sizeof( entry->vendor ) );
        entry->dirty = true;
    }
}
