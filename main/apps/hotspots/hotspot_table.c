/**
    apps/hotspots/hotspot_table.c : Hotspots observation and characterization model
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "hotspot_table.h"

#include "app_session.h"

#include <math.h>
#include <string.h>

#define HOTSPOT_TABLE_INITIAL_CAPACITY 8U

bool hotspot_bss_eligible( const rfscout_wifi_station_bss_t *a_bss )
{
    return ( NULL != a_bss ) && ( '\0' != a_bss->ssid[ 0 ] );
}

bool hotspot_entry_checkable( const hotspot_entry_t *a_entry )
{
    return NULL != a_entry && hotspot_bss_eligible( &a_entry->bss ) &&
           ( a_entry->credential_available ||
             ( a_entry->manual_requested && 0U != a_entry->manual_profile_id ) );
}

void hotspot_table_reset( hotspot_table_t *a_table )
{
    if( NULL == a_table )
    {
        return;
    }
    if( NULL != a_table->entries )
    {
        (void)app_session_free( a_table->entries );
        a_table->entries = NULL;
    }
    a_table->capacity = 0U;
    a_table->count = 0U;
    a_table->open_count = 0U;
    a_table->owe_count = 0U;
    a_table->secured_count = 0U;
    a_table->scans_completed = 0U;
}

hotspot_entry_t *hotspot_table_find( hotspot_table_t *a_table, const uint8_t a_bssid[ 6 ] )
{
    if( ( NULL == a_table ) || ( NULL == a_bssid ) )
    {
        return NULL;
    }
    for( size_t index = 0U; index < a_table->count; ++index )
    {
        if( 0 == memcmp( a_table->entries[ index ].bss.bssid, a_bssid, 6U ) )
        {
            return &a_table->entries[ index ];
        }
    }
    return NULL;
}

/** @brief Grow entry storage geometrically in app-session memory. */
static esp_err_t ensure_capacity( hotspot_table_t *a_table, size_t a_required )
{
    if( a_required > HOTSPOT_TABLE_MAX_ENTRIES )
    {
        return ESP_ERR_NO_MEM;
    }
    if( a_required <= a_table->capacity )
    {
        return ESP_OK;
    }
    size_t capacity =
        ( a_table->capacity > 0U ) ? a_table->capacity * 2U : HOTSPOT_TABLE_INITIAL_CAPACITY;
    while( capacity < a_required )
    {
        capacity *= 2U;
    }
    if( capacity > HOTSPOT_TABLE_MAX_ENTRIES )
    {
        capacity = HOTSPOT_TABLE_MAX_ENTRIES;
    }
    hotspot_entry_t *resized =
        ( NULL == a_table->entries )
            ? app_session_calloc( capacity, sizeof( hotspot_entry_t ) )
            : app_session_resize( a_table->entries, capacity, sizeof( hotspot_entry_t ) );
    if( NULL == resized )
    {
        return ESP_ERR_NO_MEM;
    }
    a_table->entries = resized;
    a_table->capacity = capacity;
    return ESP_OK;
}

/** @brief Whether a BSSID appeared in a completed scan, independent of input ordering. */
static bool scan_contains( const rfscout_wifi_station_bss_t *a_records,
                           size_t a_count,
                           const uint8_t a_bssid[ 6 ] )
{
    for( size_t index = 0U; index < a_count; ++index )
    {
        if( hotspot_bss_eligible( &a_records[ index ] ) &&
            0 == memcmp( a_records[ index ].bssid, a_bssid, 6U ) )
        {
            return true;
        }
    }
    return false;
}

/** @brief Approximate coverage radius as half the larger geographic box diagonal. */
static double observed_radius_m( const geo_bounds_t *a_bounds )
{
    const double diagonal_a = geo_distance_m( a_bounds->min_latitude,
                                              a_bounds->min_longitude,
                                              a_bounds->max_latitude,
                                              a_bounds->max_longitude );
    const double diagonal_b = geo_distance_m( a_bounds->min_latitude,
                                              a_bounds->max_longitude,
                                              a_bounds->max_latitude,
                                              a_bounds->min_longitude );
    return fmax( diagonal_a, diagonal_b ) / 2.0;
}

static bool out_of_range( const hotspot_entry_t *a_entry,
                          uint16_t a_threshold_m,
                          const rfscout_gps_snapshot_t *a_gps )
{
    if( 0U == a_threshold_m || !gps_location_fresh( a_gps ) || !a_entry->live_bounds.valid ||
        a_entry->visible )
    {
        return false;
    }
    double latitude = 0.0;
    double longitude = 0.0;
    if( !geo_bounds_center( &a_entry->live_bounds, &latitude, &longitude ) )
    {
        return false;
    }
    const double center_distance =
        geo_distance_m( latitude, longitude, a_gps->latitude_deg, a_gps->longitude_deg );
    return center_distance - observed_radius_m( &a_entry->live_bounds ) > (double)a_threshold_m;
}

/** @brief Compact entries in place without disturbing the current scan's observations. */
static void remove_entry( hotspot_table_t *a_table, size_t a_index )
{
    --a_table->count;
    if( a_index < a_table->count )
    {
        memmove( &a_table->entries[ a_index ],
                 &a_table->entries[ a_index + 1U ],
                 ( a_table->count - a_index ) * sizeof( hotspot_entry_t ) );
    }
    memset( &a_table->entries[ a_table->count ], 0, sizeof( hotspot_entry_t ) );
}

/** @brief Open and OWE BSSIDs need no saved credentials and are guest-accessible candidates. */
static bool guest_accessible( const rfscout_wifi_station_bss_t *a_bss )
{
    return RFSCOUT_WIFI_STATION_SECURITY_OPEN == a_bss->security ||
           RFSCOUT_WIFI_STATION_SECURITY_OWE == a_bss->security;
}

/**
 * @brief Prefer evicting secured BSSIDs, then least recently observed within each class.
 * @details Never discard a guest-accessible record to admit a secured one. Current-scan
 *          sightings and queued manual requests remain protected regardless of security.
 */
static size_t eviction_victim( const hotspot_table_t *a_table,
                               const rfscout_wifi_station_bss_t *a_incoming )
{
    const bool incoming_guest = guest_accessible( a_incoming );
    size_t victim = a_table->count;
    for( size_t index = 0U; index < a_table->count; ++index )
    {
        const hotspot_entry_t *entry = &a_table->entries[ index ];
        if( entry->visible || entry->manual_requested )
        {
            continue;
        }
        const bool entry_guest = guest_accessible( &entry->bss );
        if( entry_guest && !incoming_guest )
        {
            /* A secured newcomer must not displace an OPEN/OWE observation. */
            continue;
        }
        if( victim == a_table->count ||
            ( !entry_guest && guest_accessible( &a_table->entries[ victim ].bss ) ) ||
            ( entry_guest == guest_accessible( &a_table->entries[ victim ].bss ) &&
              entry->last_seen_scan < a_table->entries[ victim ].last_seen_scan ) )
        {
            victim = index;
        }
    }
    return victim;
}

esp_err_t hotspot_table_apply_scan( hotspot_table_t *a_table,
                                    const rfscout_wifi_station_bss_t *a_records,
                                    size_t a_count,
                                    const hotspot_retention_t *a_retention,
                                    const rfscout_gps_snapshot_t *a_gps )
{
    if( NULL == a_table || ( NULL == a_records && 0U != a_count ) || NULL == a_retention ||
        0U == a_retention->missed_scan_limit )
    {
        return ESP_ERR_INVALID_ARG;
    }

    const bool located = gps_location_fresh( a_gps );
    const uint32_t scan_ordinal = a_table->scans_completed + 1U;
    a_table->open_count = 0U;
    a_table->owe_count = 0U;
    a_table->secured_count = 0U;

    /* Mark all current-scan sightings before admission/eviction. A new BSSID cannot push out a
       previously known BSSID that is also visible later in the same scan results. */
    for( size_t index = 0U; index < a_table->count; ++index )
    {
        hotspot_entry_t *entry = &a_table->entries[ index ];
        entry->visible = scan_contains( a_records, a_count, entry->bss.bssid );
    }

    for( size_t record = 0U; record < a_count; ++record )
    {
        const rfscout_wifi_station_bss_t *bss = &a_records[ record ];
        if( RFSCOUT_WIFI_STATION_SECURITY_OPEN == bss->security )
        {
            ++a_table->open_count;
        }
        else if( RFSCOUT_WIFI_STATION_SECURITY_OWE == bss->security )
        {
            ++a_table->owe_count;
        }
        else
        {
            ++a_table->secured_count;
        }
        if( !hotspot_bss_eligible( bss ) )
        {
            continue;
        }
        hotspot_entry_t *entry = hotspot_table_find( a_table, bss->bssid );
        if( NULL == entry )
        {
            continue;
        }
        if( 0 != strcmp( entry->bss.ssid, bss->ssid ) || entry->bss.security != bss->security ||
            entry->bss.auth_mode != bss->auth_mode )
        {
            /* Changed identity starts a new observation session, including location bounds. */
            memset( entry, 0, sizeof( *entry ) );
        }
        entry->bss = *bss;
        entry->visible = true;
        entry->missed_scans = 0U;
        entry->last_seen_scan = scan_ordinal;
        if( located )
        {
            geo_bounds_extend( &entry->live_bounds, a_gps->latitude_deg, a_gps->longitude_deg );
        }
    }

    /* Normal expiry happens before admitting new BSSIDs, minimizing capacity churn. */
    for( size_t index = 0U; index < a_table->count; )
    {
        hotspot_entry_t *entry = &a_table->entries[ index ];
        if( !entry->visible )
        {
            if( entry->missed_scans < UINT8_MAX )
            {
                ++entry->missed_scans;
            }
            if( entry->missed_scans >= a_retention->missed_scan_limit ||
                out_of_range( entry, a_retention->distance_threshold_m, a_gps ) )
            {
                remove_entry( a_table, index );
                continue;
            }
        }
        ++index;
    }

    esp_err_t result = ESP_OK;
    for( size_t record = 0U; record < a_count; ++record )
    {
        const rfscout_wifi_station_bss_t *bss = &a_records[ record ];
        if( !hotspot_bss_eligible( bss ) || NULL != hotspot_table_find( a_table, bss->bssid ) )
        {
            continue;
        }
        if( a_table->count == HOTSPOT_TABLE_MAX_ENTRIES )
        {
            const size_t victim = eviction_victim( a_table, bss );
            if( victim == a_table->count )
            {
                result = ESP_ERR_NO_MEM;
                continue;
            }
            remove_entry( a_table, victim );
        }
        if( ESP_OK != ensure_capacity( a_table, a_table->count + 1U ) )
        {
            result = ESP_ERR_NO_MEM;
            continue;
        }
        hotspot_entry_t *entry = &a_table->entries[ a_table->count++ ];
        memset( entry, 0, sizeof( *entry ) );
        entry->bss = *bss;
        entry->visible = true;
        entry->last_seen_scan = scan_ordinal;
        if( located )
        {
            geo_bounds_extend( &entry->live_bounds, a_gps->latitude_deg, a_gps->longitude_deg );
        }
    }
    a_table->scans_completed = scan_ordinal;
    return result;
}

bool hotspot_auto_pending( const hotspot_entry_t *a_entry )
{
    return ( NULL != a_entry ) && hotspot_entry_checkable( a_entry ) &&
           ( HOTSPOT_STATUS_GREEN != a_entry->status ) &&
           ( HOTSPOT_STATUS_YELLOW != a_entry->status );
}

bool hotspot_auto_gate_open( const hotspot_entry_t *a_entry )
{
    if( ( NULL == a_entry ) || ( a_entry->bss.rssi < HOTSPOT_AUTO_RSSI_FLOOR_DBM ) )
    {
        return false;
    }
    return !a_entry->retry_gate_active ||
           ( (int)a_entry->bss.rssi >=
             (int)a_entry->retry_gate_rssi + HOTSPOT_RETRY_IMPROVEMENT_DB );
}

bool hotspot_table_manual_pending( const hotspot_table_t *a_table )
{
    if( NULL == a_table )
    {
        return false;
    }
    for( size_t index = 0U; index < a_table->count; ++index )
    {
        const hotspot_entry_t *entry = &a_table->entries[ index ];
        if( entry->manual_requested && entry->visible && hotspot_entry_checkable( entry ) )
        {
            return true;
        }
    }
    return false;
}

hotspot_entry_t *hotspot_table_next_candidate( hotspot_table_t *a_table, bool a_auto_enabled )
{
    if( NULL == a_table )
    {
        return NULL;
    }

    for( size_t index = 0U; index < a_table->count; ++index )
    {
        hotspot_entry_t *entry = &a_table->entries[ index ];
        if( entry->manual_requested && entry->visible && hotspot_entry_checkable( entry ) )
        {
            return entry;
        }
    }
    if( !a_auto_enabled )
    {
        return NULL;
    }

    hotspot_entry_t *best = NULL;
    for( size_t index = 0U; index < a_table->count; ++index )
    {
        hotspot_entry_t *entry = &a_table->entries[ index ];
        if( !entry->visible || !hotspot_auto_pending( entry ) || !hotspot_auto_gate_open( entry ) )
        {
            continue;
        }
        if( ( NULL == best ) || ( entry->bss.rssi > best->bss.rssi ) )
        {
            best = entry;
        }
    }
    return best;
}

void hotspot_entry_record_result( hotspot_entry_t *a_entry, const hotspot_result_t *a_result )
{
    if( ( NULL == a_entry ) || ( NULL == a_result ) )
    {
        return;
    }

    if( a_entry->attempts < UINT16_MAX )
    {
        ++a_entry->attempts;
    }
    a_entry->last = *a_result;
    a_entry->has_result = true;

    if( ( HOTSPOT_STATUS_GREEN == a_result->outcome ) ||
        ( HOTSPOT_STATUS_YELLOW == a_result->outcome ) )
    {
        a_entry->success = *a_result;
        a_entry->has_success = true;
        a_entry->status = a_result->outcome;
        a_entry->retry_gate_active = false;
        return;
    }

    /* A failed attempt never erases an earlier usable result in the same observation session. */
    if( ( HOTSPOT_STATUS_GREEN != a_entry->status ) &&
        ( HOTSPOT_STATUS_YELLOW != a_entry->status ) )
    {
        a_entry->status = HOTSPOT_STATUS_RED;
    }
    a_entry->retry_gate_active = true;
    a_entry->retry_gate_rssi = a_result->start_rssi;
}

size_t hotspot_table_order( const hotspot_table_t *a_table, size_t *a_indices, size_t a_capacity )
{
    if( ( NULL == a_table ) || ( NULL == a_indices ) )
    {
        return 0U;
    }

    size_t written = 0U;
    for( size_t index = 0U; ( index < a_table->count ) && ( written < a_capacity ); ++index )
    {
        a_indices[ written++ ] = index;
    }

    /* Insertion sort: tables are small and the order must be stable. */
    for( size_t outer = 1U; outer < written; ++outer )
    {
        const size_t moving = a_indices[ outer ];
        const hotspot_entry_t *candidate = &a_table->entries[ moving ];
        size_t position = outer;
        while( position > 0U )
        {
            const hotspot_entry_t *previous = &a_table->entries[ a_indices[ position - 1U ] ];
            const bool before = ( candidate->visible && !previous->visible ) ||
                                ( ( candidate->visible == previous->visible ) &&
                                  ( candidate->bss.rssi > previous->bss.rssi ) );
            if( !before )
            {
                break;
            }
            a_indices[ position ] = a_indices[ position - 1U ];
            --position;
        }
        a_indices[ position ] = moving;
    }
    return written;
}
