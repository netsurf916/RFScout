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
#include <math.h>

static surveillance_history_entry_t *entries;
static size_t entry_count;
static size_t entry_capacity;
static size_t journal_appends;
static observation_history_identity_t *sample_buffer;
static size_t sample_capacity;

#define HISTORY_LIMIT 48U
static rfscout_media_file_t replay_file;
static bool replay_active;
static bool replay_complete;
static bool reference_valid;
static double reference_lat;
static double reference_lon;
static char replay_buffer[ 256 ];
static size_t replay_used;
static bool replay_overflow;

static bool ensure_history_capacity( size_t a_required )
{
    if( a_required > HISTORY_LIMIT )
        return false;
    if( a_required <= entry_capacity )
        return true;
    if( entry_capacity > SIZE_MAX / 2U )
        return false;
    size_t capacity = entry_capacity ? entry_capacity * 2U : 32U;
    if( capacity < a_required )
        capacity = a_required;
    if( capacity > HISTORY_LIMIT )
        capacity = HISTORY_LIMIT;
    surveillance_history_entry_t *next =
        app_session_resize( entries, capacity, sizeof( *entries ) );
    if( NULL == next )
        return false;
    entries = next;
    entry_capacity = capacity;
    return true;
}

#define HISTORY_JOURNAL "surveillance.jsonl"

static esp_err_t append_entry( const rfscout_media_file_t *a_file,
                               const surveillance_history_entry_t *a_entry )
{
    char line[ 256 ];
    const int length =
        snprintf( line,
                  sizeof( line ),
                  "{\"type\":%u,\"mac\":\"%02X%02X%02X%02X%02X%02X\",\"class\":%u,"
                  "\"min_lat\":%.7f,\"max_lat\":%.7f,\"min_lon\":%.7f,\"max_lon\":%.7f,"
                  "\"seen\":%lu}\n",
                  (unsigned)a_entry->address_type,
                  a_entry->address[ 0 ],
                  a_entry->address[ 1 ],
                  a_entry->address[ 2 ],
                  a_entry->address[ 3 ],
                  a_entry->address[ 4 ],
                  a_entry->address[ 5 ],
                  (unsigned)a_entry->classification,
                  a_entry->min_latitude,
                  a_entry->max_latitude,
                  a_entry->min_longitude,
                  a_entry->max_longitude,
                  (unsigned long)a_entry->last_seen_ms );
    if( length < 0 || (size_t)length >= sizeof( line ) )
        return ESP_ERR_INVALID_SIZE;
    return media_write( a_file, line, (size_t)length );
}

typedef struct
{
    uint8_t address[ 6 ];
    uint8_t address_type;
    surveillance_class_t classification;
    double latitude;
    double longitude;
    int64_t utc_epoch;
    uint32_t seen_ms;
} history_point_t;

#define HISTORY_PENDING_LIMIT 128U
static history_point_t *pending_points;
static size_t pending_count;

static esp_err_t append_point( const rfscout_media_file_t *a_file,
                               const history_point_t *a_point )
{
    char line[ 256 ];
    const int length = snprintf(
        line, sizeof( line ),
        "{\"type\":%u,\"mac\":\"%02X%02X%02X%02X%02X%02X\",\"class\":%u,"
        "\"lat\":%.7f,\"lon\":%.7f,\"utc\":%lld,\"seen\":%lu}\n",
        (unsigned)a_point->address_type,
        a_point->address[ 0 ], a_point->address[ 1 ], a_point->address[ 2 ],
        a_point->address[ 3 ], a_point->address[ 4 ], a_point->address[ 5 ],
        (unsigned)a_point->classification,
        a_point->latitude, a_point->longitude,
        (long long)a_point->utc_epoch, (unsigned long)a_point->seen_ms );
    if( length < 0 || (size_t)length >= sizeof( line ) )
        return ESP_ERR_INVALID_SIZE;
    return media_write( a_file, line, (size_t)length );
}

bool surveillance_history_get( size_t a_index, surveillance_history_entry_t *a_entry )
{
    if( NULL == a_entry || a_index >= entry_count )
        return false;
    *a_entry = entries[ a_index ];
    return a_entry->valid;
}

size_t surveillance_history_list( surveillance_history_entry_t *a_rows, size_t a_capacity )
{
    size_t count = 0U;
    for( size_t i = 0U; i < entry_count; ++i )
    {
        if( !entries[ i ].valid )
            continue;
        if( NULL != a_rows && count < a_capacity )
            a_rows[ count ] = entries[ i ];
        ++count;
    }
    return count;
}

static double history_distance( double a_lat, double a_lon )
{
    const double lat = ( a_lat - reference_lat ) * 0.017453292519943295;
    const double lon = ( a_lon - reference_lon ) * 0.017453292519943295;
    const double cosine = cos( reference_lat * 0.017453292519943295 );
    return 6371000.0 * sqrt( lat * lat + lon * lon * cosine * cosine );
}

static void history_sort( void )
{
    for( size_t i = 1U; i < entry_count; ++i )
    {
        surveillance_history_entry_t value = entries[ i ];
        size_t j = i;
        while( j > 0U && entries[ j - 1U ].nearest_distance_m > value.nearest_distance_m )
        {
            entries[ j ] = entries[ j - 1U ];
            --j;
        }
        entries[ j ] = value;
    }
}

static void replay_line( const char *a_line )
{
    unsigned type, classification, address[ 6 ];
    unsigned long seen;
    double min_lat, max_lat, min_lon, max_lon;
    double latitude = 0.0, longitude = 0.0;
    long long utc = 0;
    const bool discrete = 12 == sscanf(
        a_line,
        "{\"type\":%u,\"mac\":\"%2x%2x%2x%2x%2x%2x\",\"class\":%u,"
        "\"lat\":%lf,\"lon\":%lf,\"utc\":%lld,\"seen\":%lu}",
        &type, &address[ 0 ], &address[ 1 ], &address[ 2 ],
        &address[ 3 ], &address[ 4 ], &address[ 5 ], &classification,
        &latitude, &longitude, &utc, &seen );
    if( !discrete && 13 != sscanf( a_line,
                      "{\"type\":%u,\"mac\":\"%2x%2x%2x%2x%2x%2x\",\"class\":%u,"
                      "\"min_lat\":%lf,\"max_lat\":%lf,\"min_lon\":%lf,\"max_lon\":%lf,"
                      "\"seen\":%lu}",
                      &type,
                      &address[ 0 ],
                      &address[ 1 ],
                      &address[ 2 ],
                      &address[ 3 ],
                      &address[ 4 ],
                      &address[ 5 ],
                      &classification,
                      &min_lat,
                      &max_lat,
                      &min_lon,
                      &max_lon,
                      &seen ) )
    {
        return;
    }
    if( type > UINT8_MAX || classification > SURVEILLANCE_OTHER )
        return;
    for( size_t i = 0U; i < 6U; ++i )
        if( address[ i ] > UINT8_MAX )
            return;
    surveillance_history_entry_t *entry = NULL;
    for( size_t i = 0U; i < entry_count; ++i )
    {
        if( entries[ i ].address_type != type )
            continue;
        bool same = true;
        for( size_t j = 0U; j < 6U; ++j )
        {
            if( entries[ i ].address[ j ] != address[ j ] )
                same = false;
        }
        if( same )
        {
            entry = &entries[ i ];
            break;
        }
    }
    if( !discrete )
    {
        latitude = fmax( min_lat, fmin( reference_lat, max_lat ) );
        longitude = fmax( min_lon, fmin( reference_lon, max_lon ) );
    }
    if( !isfinite( latitude ) || !isfinite( longitude ) ||
        fabs( latitude ) > 90.0 || fabs( longitude ) > 180.0 )
        return;
    const double distance = history_distance( latitude, longitude );
    if( NULL != entry && distance >= entry->nearest_distance_m )
        return;
    if( NULL == entry && entry_count == HISTORY_LIMIT )
    {
        history_sort();
        if( distance >= entries[ entry_count - 1U ].nearest_distance_m )
            return;
        entry = &entries[ entry_count - 1U ];
    }
    if( NULL == entry )
    {
        if( !ensure_history_capacity( entry_count + 1U ) )
            return;
        entry = &entries[ entry_count++ ];
        memset( entry, 0, sizeof( *entry ) );
    }
    for( size_t j = 0U; j < 6U; ++j )
        entry->address[ j ] = (uint8_t)address[ j ];
    entry->address_type = (uint8_t)type;
    entry->classification = (surveillance_class_t)classification;
    entry->min_latitude = discrete ? latitude : min_lat;
    entry->max_latitude = discrete ? latitude : max_lat;
    entry->min_longitude = discrete ? longitude : min_lon;
    entry->max_longitude = discrete ? longitude : max_lon;
    entry->last_seen_ms = (uint32_t)seen;
    entry->valid = true;
    entry->nearest_distance_m = distance;
    entry->dirty = false;
    history_sort();
}

void surveillance_history_open( void )
{
    if( replay_active )
        (void)media_close( &replay_file );
    replay_active = false;
    replay_complete = false;
    reference_valid = false;
    replay_used = 0U;
    replay_overflow = false;
    if( NULL != entries )
        (void)app_session_free( entries );
    entries = NULL;
    entry_count = 0U;
    entry_capacity = 0U;
    journal_appends = 0U;
    if( NULL != sample_buffer )
        (void)app_session_free( sample_buffer );
    sample_buffer = NULL;
    sample_capacity = 0U;
    if( NULL != pending_points )
        (void)app_session_free( pending_points );
    pending_points = NULL;
    pending_count = 0U;
}

void surveillance_history_replay_step( bool a_foreground )
{
    rfscout_gps_snapshot_t gps = { 0 };
    if( !gps_snapshot_get( &gps ) || !gps.location_valid || gps.location_age_ms > 5000U )
    {
        if( replay_active )
            (void)media_close( &replay_file );
        replay_active = false;
        replay_used = 0U;
        return;
    }
    if( !reference_valid ||
        ( reference_valid && history_distance( gps.latitude_deg, gps.longitude_deg ) > 100.0 ) )
    {
        if( replay_active )
            (void)media_close( &replay_file );
        replay_active = false;
        reference_lat = gps.latitude_deg;
        reference_lon = gps.longitude_deg;
        reference_valid = true;
        replay_complete = false;
        replay_used = 0U;
        entry_count = 0U;
    }
    if( replay_complete )
        return;
    if( !replay_active )
    {
        rfscout_media_collection_t collection;
        if( ESP_OK != media_app_collection( "locations", &collection ) ||
            ESP_OK != media_open( &collection, HISTORY_JOURNAL,
                                  RFSCOUT_MEDIA_OPEN_READ, &replay_file ) )
            return;
        replay_active = true;
    }
    size_t budget = a_foreground ? 2048U : 256U;
    char chunk[ 64 ];
    while( budget > 0U )
    {
        size_t count = 0U;
        const size_t requested = budget < sizeof( chunk ) ? budget : sizeof( chunk );
        if( ESP_OK != media_read( &replay_file, chunk, requested, &count ) )
            break;
        if( count == 0U )
        {
            replay_complete = true;
            break;
        }
        budget -= count;
        for( size_t i = 0U; i < count; ++i )
        {
            if( chunk[ i ] == '\n' )
            {
                if( replay_used && !replay_overflow )
                {
                    replay_buffer[ replay_used ] = '\0';
                    replay_line( replay_buffer );
                }
                replay_used = 0U;
                replay_overflow = false;
            }
            else if( !replay_overflow && replay_used + 1U < sizeof( replay_buffer ) )
                replay_buffer[ replay_used++ ] = chunk[ i ];
            else
                replay_overflow = true;
        }
    }
    if( replay_complete )
    {
        (void)media_close( &replay_file );
        replay_active = false;
    }
}

static void flush_pending( bool a_include_active )
{
    if( pending_count == 0U )
        return;
    if( replay_active )
        (void)media_close( &replay_file );
    replay_active = false;
    replay_complete = false;
    replay_used = 0U;
    rfscout_media_collection_t collection;
    rfscout_media_file_t file = { 0 };
    esp_err_t result = media_app_collection( "locations", &collection );
    if( ESP_OK == result )
        result = media_open( &collection, HISTORY_JOURNAL, RFSCOUT_MEDIA_OPEN_APPEND, &file );
    if( ESP_OK == result )
    {
        for( size_t i = 0U; i < pending_count; ++i )
        {
            result = append_point( &file, &pending_points[ i ] );
            if( ESP_OK != result )
                break;
        }
        if( ESP_OK == result )
            result = media_sync( &file );
        const esp_err_t close_result = media_close( &file );
        if( ESP_OK == result )
            result = close_result;
    }
    pending_count = 0U;
    if( ESP_OK != result )
        printf( "surveillance history flush failed: %ld\\n", (long)result );
    (void)a_include_active;
}

void surveillance_history_flush( void )
{
    flush_pending( true );
    if( replay_active )
        (void)media_close( &replay_file );
    replay_active = false;
}

void surveillance_history_compact( void )
{
    /* Discrete records are authoritative; do not collapse them into bounding boxes. */
}

void surveillance_history_process_expired( void )
{
    observation_history_identity_t expired[ 4 ] = { 0 };
    size_t count;
    bool changed = false;
    while( 0U != ( count = observation_store_take_expired( expired, 4U ) ) )
    {
        for( size_t i = 0U; i < count; ++i )
        {
            for( size_t j = 0U; j < entry_count; ++j )
            {
                surveillance_history_entry_t *entry = &entries[ j ];
                if( entry->valid && entry->dirty &&
                    entry->address_type == expired[ i ].address_type &&
                    0 == memcmp( entry->address, expired[ i ].address, sizeof( entry->address ) ) )
                {
                    entry->completed = true;
                    changed = true;
                    break;
                }
            }
        }
    }
    if( changed )
    {
        if( replay_active )
            (void)media_close( &replay_file );
        replay_active = false;
        replay_complete = false;
        replay_used = 0U;
        flush_pending( false );
        if( journal_appends >= 64U )
            surveillance_history_compact();
    }
}

void surveillance_history_sample( void )
{
    rfscout_gps_snapshot_t gps = { 0 };
    if( !gps_snapshot_get( &gps ) || !gps.location_valid || gps.location_age_ms > 5000U )
        return;
    const size_t total = observation_store_surveillance_devices( NULL, 0U );
    if( 0U == total )
        return;
    if( total > sample_capacity )
    {
        observation_history_identity_t *next =
            app_session_resize( sample_buffer, total, sizeof( *sample_buffer ) );
        if( NULL == next )
            return;
        sample_buffer = next;
        sample_capacity = total;
    }
    const size_t copied = observation_store_surveillance_devices( sample_buffer, sample_capacity );
    const size_t count = copied < total ? copied : total;
    for( size_t i = 0U; i < count; ++i )
    {
        const observation_history_identity_t *device = &sample_buffer[ i ];
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
            if( !ensure_history_capacity( entry_count + 1U ) )
                continue;
            entry = &entries[ entry_count++ ];
            memset( entry, 0, sizeof( *entry ) );
            memcpy( entry->address, device->address, 6U );
            entry->address_type = device->address_type;
            entry->min_latitude = entry->max_latitude = gps.latitude_deg;
            entry->min_longitude = entry->max_longitude = gps.longitude_deg;
            entry->valid = true;
        }
        else if( entry->last_seen_ms == device->last_seen_ms )
            continue;
        else
        {
            if( gps.latitude_deg < entry->min_latitude )
                entry->min_latitude = gps.latitude_deg;
            if( gps.latitude_deg > entry->max_latitude )
                entry->max_latitude = gps.latitude_deg;
            if( gps.longitude_deg < entry->min_longitude )
                entry->min_longitude = gps.longitude_deg;
            if( gps.longitude_deg > entry->max_longitude )
                entry->max_longitude = gps.longitude_deg;
        }
        entry->last_seen_ms = device->last_seen_ms;
        entry->classification = device->surveillance;
        memcpy( entry->vendor, device->vendor, sizeof( entry->vendor ) );
        entry->dirty = true;
    }
}
