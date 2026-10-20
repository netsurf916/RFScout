/**
    apps/hotspots/hotspot_table.c : Hotspots observation and characterization model
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "hotspot_table.h"

#include "app_session.h"

#include <string.h>

#define HOTSPOT_TABLE_INITIAL_CAPACITY 8U

bool hotspot_bss_eligible( const rfscout_wifi_station_bss_t *a_bss )
{
    return ( NULL != a_bss ) && ( '\0' != a_bss->ssid[ 0 ] );
}

void hotspot_table_reset( hotspot_table_t *a_table )
{
    if( NULL == a_table )
    {
        return;
    }
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

esp_err_t hotspot_table_apply_scan( hotspot_table_t *a_table,
                                    const rfscout_wifi_station_bss_t *a_records,
                                    size_t a_count )
{
    if( ( NULL == a_table ) || ( ( NULL == a_records ) && ( 0U != a_count ) ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    for( size_t index = 0U; index < a_table->count; ++index )
    {
        a_table->entries[ index ].visible = false;
    }

    esp_err_t result = ESP_OK;
    a_table->open_count = 0U;
    a_table->owe_count = 0U;
    a_table->secured_count = 0U;
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
        if( NULL != entry )
        {
            /* A changed SSID or security for the same BSSID is a different network; restart it. */
            if( ( 0 != strcmp( entry->bss.ssid, bss->ssid ) ) ||
                ( entry->bss.security != bss->security ) ||
                ( entry->bss.auth_mode != bss->auth_mode ) )
            {
                memset( entry, 0, sizeof( *entry ) );
            }
            entry->bss = *bss;
            entry->visible = true;
            entry->missed_scans = 0U;
            continue;
        }

        if( ESP_OK != ensure_capacity( a_table, a_table->count + 1U ) )
        {
            result = ESP_ERR_NO_MEM;
            continue;
        }
        entry = &a_table->entries[ a_table->count++ ];
        memset( entry, 0, sizeof( *entry ) );
        entry->bss = *bss;
        entry->visible = true;
    }

    size_t kept = 0U;
    for( size_t index = 0U; index < a_table->count; ++index )
    {
        hotspot_entry_t *entry = &a_table->entries[ index ];
        if( !entry->visible )
        {
            ++entry->missed_scans;
            if( entry->missed_scans >= HOTSPOT_MISSED_SCAN_LIMIT )
            {
                continue;
            }
        }
        if( kept != index )
        {
            a_table->entries[ kept ] = *entry;
        }
        ++kept;
    }
    a_table->count = kept;
    ++a_table->scans_completed;
    return result;
}

bool hotspot_auto_pending( const hotspot_entry_t *a_entry )
{
    return ( NULL != a_entry ) && hotspot_bss_eligible( &a_entry->bss ) &&
           ( RFSCOUT_WIFI_STATION_SECURITY_SECURED != a_entry->bss.security ||
             a_entry->credential_available ) &&
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
        if( entry->manual_requested && entry->visible &&
            ( RFSCOUT_WIFI_STATION_SECURITY_SECURED != entry->bss.security ||
              entry->credential_available ) )
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
        if( entry->manual_requested && entry->visible && hotspot_bss_eligible( &entry->bss ) &&
            ( RFSCOUT_WIFI_STATION_SECURITY_SECURED != entry->bss.security ||
              entry->credential_available ) )
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
