/**
    surveillance_history.c : Receiver-observation history
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "surveillance_history.h"
#include "gps.h"
#include "media_store.h"
#include "observation_store.h"

#include <string.h>

#define HISTORY_CAPACITY 128U
static surveillance_history_entry_t entries[ HISTORY_CAPACITY ];

size_t surveillance_history_list( surveillance_history_entry_t *a_rows, size_t a_capacity )
{
    size_t count = 0U;
    for( size_t i = 0U; i < HISTORY_CAPACITY; ++i )
    {
        if( !entries[ i ].valid ) continue;
        if( NULL != a_rows && count < a_capacity ) a_rows[ count ] = entries[ i ];
        ++count;
    }
    return count;
}

void surveillance_history_open( void )
{
    memset( entries, 0, sizeof( entries ) );
}

void surveillance_history_flush( void )
{
}

void surveillance_history_compact( void )
{
}

void surveillance_history_sample( void )
{
    rfscout_gps_snapshot_t gps = { 0 };
    if( !gps_snapshot_get( &gps ) || !gps.location_valid ) return;
    rf_device_t active[ HISTORY_CAPACITY ] = { 0 };
    const size_t total = observation_store_surveillance_devices( active, HISTORY_CAPACITY );
    const size_t count = total < HISTORY_CAPACITY ? total : HISTORY_CAPACITY;
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
