/**
    apps/hotspots/hotspot_table.c : Hotspots observation and characterization model
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "hotspot_table.h"

#include "app_session.h"
#include "observation_retention.h"

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

/** @brief Compact entries in place, without retaining any per-sweep snapshot. */
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

static bool guest_accessible( const rfscout_wifi_station_bss_t *a_bss )
{
    return RFSCOUT_WIFI_STATION_SECURITY_OPEN == a_bss->security ||
           RFSCOUT_WIFI_STATION_SECURITY_OWE == a_bss->security;
}

/**
 * @brief Under capacity pressure, choose an absent, unqueued victim.
 * @details Secured records yield to guest candidates; guests are never evicted for
 *          secured new arrivals. Older observations rank first, RSSI breaks age ties.
 */
static size_t eviction_victim( const hotspot_table_t *a_table,
                               const rfscout_wifi_station_bss_t *a_incoming,
                               uint32_t a_monitor_ms )
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
            continue;
        }
        if( victim == a_table->count )
        {
            victim = index;
            continue;
        }
        const hotspot_entry_t *current = &a_table->entries[ victim ];
        const bool current_guest = guest_accessible( &current->bss );
        const uint32_t age = a_monitor_ms - entry->last_seen_ms;
        const uint32_t current_age = a_monitor_ms - current->last_seen_ms;
        if( ( !entry_guest && current_guest ) ||
            ( entry_guest == current_guest &&
              ( age > current_age ||
                ( age == current_age && entry->bss.rssi < current->bss.rssi ) ) ) )
        {
            victim = index;
        }
    }
    return victim;
}

void hotspot_table_begin_sweep( hotspot_table_t *a_table )
{
    if( NULL == a_table )
    {
        return;
    }
    ++a_table->scans_completed;
    if( 0U == a_table->scans_completed )
    {
        ++a_table->scans_completed;
        /* Unlikely uptime wrap: avoid false current-sweep identity matches. */
        for( size_t index = 0U; index < a_table->count; ++index )
        {
            a_table->entries[ index ].last_seen_scan = 0U;
        }
    }
    for( size_t index = 0U; index < a_table->count; ++index )
    {
        a_table->entries[ index ].visible = false;
    }
}

esp_err_t hotspot_table_observe( hotspot_table_t *a_table,
                                 const rfscout_wifi_station_bss_t *a_bss,
                                 uint32_t a_monitor_ms,
                                 const rfscout_gps_snapshot_t *a_gps )
{
    if( NULL == a_table || !hotspot_bss_eligible( a_bss ) || 0U == a_table->scans_completed )
    {
        return ESP_ERR_INVALID_ARG;
    }
    hotspot_entry_t *entry = hotspot_table_find( a_table, a_bss->bssid );
    if( NULL == entry )
    {
        if( HOTSPOT_TABLE_MAX_ENTRIES == a_table->count )
        {
            const size_t victim = eviction_victim( a_table, a_bss, a_monitor_ms );
            if( victim == a_table->count )
            {
                return ESP_ERR_NO_MEM;
            }
            remove_entry( a_table, victim );
        }
        const esp_err_t result = ensure_capacity( a_table, a_table->count + 1U );
        if( ESP_OK != result )
        {
            return result;
        }
        entry = &a_table->entries[ a_table->count++ ];
        memset( entry, 0, sizeof( *entry ) );
    }
    else if( 0 != strcmp( entry->bss.ssid, a_bss->ssid ) ||
             entry->bss.security != a_bss->security || entry->bss.auth_mode != a_bss->auth_mode )
    {
        /* Identity changed: start a new Live session including credential evidence. */
        memset( entry, 0, sizeof( *entry ) );
    }

    entry->bss = *a_bss;
    entry->last_seen_ms = a_monitor_ms;
    entry->last_seen_scan = a_table->scans_completed;
    entry->visible = true;
    if( gps_location_fresh( a_gps ) )
    {
        geo_bounds_extend( &entry->live_bounds, a_gps->latitude_deg, a_gps->longitude_deg );
    }
    return ESP_OK;
}

void hotspot_table_finish_sweep( hotspot_table_t *a_table,
                                 uint32_t a_monitor_ms,
                                 const hotspot_retention_t *a_retention,
                                 const rfscout_gps_snapshot_t *a_gps )
{
    if( NULL == a_table || NULL == a_retention )
    {
        return;
    }

    const bool located = gps_location_fresh( a_gps );
    for( size_t index = 0U; index < a_table->count; )
    {
        hotspot_entry_t *entry = &a_table->entries[ index ];
        const bool out_of_range =
            observation_retention_outside_coverage( &entry->live_bounds,
                                                    located ? a_gps->latitude_deg : 0.0,
                                                    located ? a_gps->longitude_deg : 0.0,
                                                    a_retention->distance_threshold_m,
                                                    located,
                                                    entry->visible );
        if( !entry->visible && !entry->manual_requested &&
            ( observation_retention_expired(
                  a_monitor_ms, entry->last_seen_ms, a_retention->timeout_ms, 0U ) ||
              out_of_range ) )
        {
            remove_entry( a_table, index );
            continue;
        }
        ++index;
    }

    a_table->open_count = 0U;
    a_table->owe_count = 0U;
    a_table->secured_count = 0U;
    for( size_t index = 0U; index < a_table->count; ++index )
    {
        const hotspot_entry_t *entry = &a_table->entries[ index ];
        if( !entry->visible )
        {
            continue;
        }
        if( RFSCOUT_WIFI_STATION_SECURITY_OPEN == entry->bss.security )
        {
            ++a_table->open_count;
        }
        else if( RFSCOUT_WIFI_STATION_SECURITY_OWE == entry->bss.security )
        {
            ++a_table->owe_count;
        }
        else
        {
            ++a_table->secured_count;
        }
    }
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
