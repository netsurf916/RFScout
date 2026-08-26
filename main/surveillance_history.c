/**
    surveillance_history.c : Retained RFScout surveillance detections
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "surveillance_history.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

typedef struct
{
    uint8_t address[ 6 ];
    uint8_t address_type;
    surveillance_class_t classification;
    surveillance_signature_id_t signature;
    uint32_t last_seen_ms;
} surveillance_history_entry_t;

static surveillance_history_entry_t entries[ RFSCOUT_SURVEILLANCE_HISTORY_CAPACITY ];
static size_t entry_count;
static SemaphoreHandle_t history_lock;

/** @brief Return the current monotonic time in milliseconds. */
static uint32_t current_time_ms( void )
{
    return (uint32_t)( esp_timer_get_time() / 1000ULL );
}

/** @brief Determine whether a history entry belongs to an observation. */
static bool same_identity( const surveillance_history_entry_t *a_entry,
                           const rf_device_t *a_observation )
{
    return ( a_entry->address_type == a_observation->address_type ) &&
           ( 0 == memcmp( a_entry->address, a_observation->address, 6 ) );
}

/**
 * @brief Remove detections older than the extended surveillance window.
 * @details Removal swaps the last active entry into an expired slot. The fixed
 *          store never allocates or frees memory during normal operation.
 */
static void expire_stale_entries( uint32_t a_now_ms )
{
    size_t i = 0;
    while( i < entry_count )
    {
        if( (uint32_t)( a_now_ms - entries[ i ].last_seen_ms ) < RFSCOUT_SURVEILLANCE_TIMEOUT_MS )
        {
            ++i;
            continue;
        }

        --entry_count;
        if( i != entry_count )
        {
            entries[ i ] = entries[ entry_count ];
        }
        memset( &entries[ entry_count ], 0, sizeof( entries[ entry_count ] ) );
    }
}

/** @brief Find the least-recently-seen entry when the store is full. */
static size_t oldest_entry( void )
{
    size_t oldest = 0;
    for( size_t i = 1; i < entry_count; ++i )
    {
        if( entries[ i ].last_seen_ms < entries[ oldest ].last_seen_ms )
        {
            oldest = i;
        }
    }
    return oldest;
}

esp_err_t surveillance_history_init( void )
{
    memset( entries, 0, sizeof( entries ) );
    entry_count = 0;
    history_lock = xSemaphoreCreateMutex();
    return ( NULL != history_lock ) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool surveillance_history_observe( const rf_device_t *a_observation )
{
    if( ( NULL == a_observation ) || ( SURVEILLANCE_NONE == a_observation->surveillance ) ||
        ( NULL == history_lock ) )
    {
        return false;
    }

    xSemaphoreTake( history_lock, portMAX_DELAY );
    const uint32_t now_ms = current_time_ms();
    expire_stale_entries( now_ms );

    for( size_t i = 0; i < entry_count; ++i )
    {
        if( !same_identity( &entries[ i ], a_observation ) )
        {
            continue;
        }

        entries[ i ].classification = a_observation->surveillance;
        entries[ i ].signature = a_observation->surveillance_signature;
        entries[ i ].last_seen_ms = now_ms;
        xSemaphoreGive( history_lock );
        return true;
    }

    const size_t slot =
        ( entry_count < RFSCOUT_SURVEILLANCE_HISTORY_CAPACITY ) ? entry_count++ : oldest_entry();

    memcpy( entries[ slot ].address, a_observation->address, 6 );
    entries[ slot ].address_type = a_observation->address_type;
    entries[ slot ].classification = a_observation->surveillance;
    entries[ slot ].signature = a_observation->surveillance_signature;
    entries[ slot ].last_seen_ms = now_ms;

    xSemaphoreGive( history_lock );
    return true;
}

size_t surveillance_history_counts( uint16_t *a_counts, size_t a_capacity )
{
    if( ( NULL == a_counts ) || ( a_capacity < RFSCOUT_SURVEILLANCE_CLASS_COUNT ) ||
        ( NULL == history_lock ) )
    {
        return 0;
    }

    memset( a_counts, 0, a_capacity * sizeof( *a_counts ) );
    xSemaphoreTake( history_lock, portMAX_DELAY );
    expire_stale_entries( current_time_ms() );

    for( size_t i = 0; i < entry_count; ++i )
    {
        const size_t classification = (size_t)entries[ i ].classification;
        if( classification < a_capacity )
        {
            ++a_counts[ classification ];
        }
    }

    const size_t result = entry_count;
    xSemaphoreGive( history_lock );
    return result;
}

size_t surveillance_history_signature_counts( uint16_t *a_counts, size_t a_capacity )
{
    if( ( NULL == a_counts ) || ( a_capacity < SURVEILLANCE_SIGNATURE_COUNT ) ||
        ( NULL == history_lock ) )
    {
        return 0;
    }

    memset( a_counts, 0, a_capacity * sizeof( *a_counts ) );
    xSemaphoreTake( history_lock, portMAX_DELAY );
    expire_stale_entries( current_time_ms() );

    for( size_t i = 0; i < entry_count; ++i )
    {
        const size_t signature = (size_t)entries[ i ].signature;
        if( ( signature > (size_t)SURVEILLANCE_SIGNATURE_NONE ) && ( signature < a_capacity ) )
        {
            ++a_counts[ signature ];
        }
    }

    const size_t result = entry_count;
    xSemaphoreGive( history_lock );
    return result;
}
