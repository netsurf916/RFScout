/**
    nrf24_activity.c : Adaptive nRF24 activity histogram and fingerprint store
    Description: Maintains fast/slow spectrum history and conservative passive
                 classifications from validated ESB packet metadata.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "nrf24_activity.h"

#include "freertos/FreeRTOS.h"

#include <stdio.h>
#include <string.h>

#define ACTIVITY_BUSY_THRESHOLD 72U
#define WIFI_LIKE_MIN_RUN 12U
#define BLE_LIKE_MIN_ISLANDS 7U
#define PACKET_RECORD_MAX 5U
#define ADDRESS_LEDGER_MAX 64U
#define SPECTRUM_CONFIRM_SWEEPS 2U

/** @brief Active classifier state for one recently observed ESB address. */
typedef struct
{
    bool used;
    nrf24_fingerprint_t public;
    uint32_t first_seen_ms;
    uint32_t last_seen_ms;
    uint32_t interval_total_ms;
    uint16_t interval_samples;
    uint8_t min_payload;
    uint8_t max_payload;
    uint8_t no_ack_count;
} packet_record_t;

/** @brief Longer-lived address ledger used to deduplicate category counts. */
typedef struct
{
    bool used;
    uint8_t address[ 5 ];
    uint8_t address_len;
    nrf24_fingerprint_type_t counted_type;
    uint32_t last_seen_ms;
} address_ledger_t;

static uint8_t fast_activity[ NRF24_CHANNEL_COUNT ];
static uint8_t slow_peak[ NRF24_CHANNEL_COUNT ];
static packet_record_t packet_records[ PACKET_RECORD_MAX ];
static nrf24_recent_packet_t recent_packets[ NRF24_RECENT_PACKET_MAX ];
static size_t recent_packet_count;
static size_t recent_packet_next;
static portMUX_TYPE recent_packet_lock = portMUX_INITIALIZER_UNLOCKED;
static address_ledger_t address_ledger[ ADDRESS_LEDGER_MAX ];
static uint32_t category_counts[ NRF24_FP_LOGITECH_LIKE + 1U ];
static nrf24_fingerprint_type_t spectrum_candidate;
static uint8_t spectrum_candidate_sweeps;
static bool spectrum_latched;

/** @brief Update the fast EWMA activity value for one channel. */
static uint8_t fast_update( uint8_t a_old, bool a_hit )
{
    return (uint8_t)( ( (uint16_t)a_old * 7U + ( a_hit ? 255U : 0U ) ) / 8U );
}

/** @brief Compare variable-length ESB addresses. */
static bool address_bytes_equal( const uint8_t *a_left,
                                 uint8_t a_left_len,
                                 const uint8_t *a_right,
                                 uint8_t a_right_len )
{
    return ( a_left_len == a_right_len ) && ( 0 == memcmp( a_left, a_right, a_left_len ) );
}

/** @brief Compare a public fingerprint identity with one packet observation. */
static bool address_equal( const nrf24_fingerprint_t *a_record,
                           const nrf24_packet_observation_t *a_packet )
{
    return address_bytes_equal(
        a_record->address, a_record->address_len, a_packet->address, a_packet->address_len );
}

/** @brief Recognize the known Logitech pairing address used as strong evidence. */
static bool is_logitech_pairing_address( const uint8_t *a_address, uint8_t a_length )
{
    static const uint8_t pairing[ 5 ] = { 0xBBU, 0x0AU, 0xDCU, 0xA5U, 0x75U };

    return ( 5U == a_length ) && ( 0 == memcmp( a_address, pairing, sizeof( pairing ) ) );
}

/** @brief Increment a bounded detection category counter. */
static void bump_category( nrf24_fingerprint_type_t a_type )
{
    if( ( a_type > NRF24_FP_UNKNOWN ) && ( a_type <= NRF24_FP_LOGITECH_LIKE ) &&
        ( category_counts[ a_type ] < UINT32_MAX ) )
    {
        ++category_counts[ a_type ];
    }
}

/** @brief Find or allocate the deduplication ledger entry for a fingerprint. */
static address_ledger_t *find_or_allocate_ledger( const nrf24_fingerprint_t *a_fingerprint,
                                                  uint32_t a_last_seen_ms )
{
    address_ledger_t *oldest = NULL;

    for( size_t i = 0U; i < ADDRESS_LEDGER_MAX; ++i )
    {
        address_ledger_t *entry = &address_ledger[ i ];
        if( entry->used )
        {
            if( address_bytes_equal( entry->address,
                                     entry->address_len,
                                     a_fingerprint->address,
                                     a_fingerprint->address_len ) )
            {
                entry->last_seen_ms = a_last_seen_ms;
                return entry;
            }

            if( ( NULL == oldest ) || ( entry->last_seen_ms < oldest->last_seen_ms ) )
            {
                oldest = entry;
            }
        }
        else
        {
            memset( entry, 0, sizeof( *entry ) );
            entry->used = true;
            memcpy( entry->address, a_fingerprint->address, a_fingerprint->address_len );
            entry->address_len = a_fingerprint->address_len;
            entry->last_seen_ms = a_last_seen_ms;
            return entry;
        }
    }

    /* The ledger is deliberately much larger than the active classifier cache.
       If it eventually fills, recycle the oldest identity and first remove its
       contribution so the displayed category totals remain internally sane. */
    if( ( NULL != oldest ) && ( oldest->counted_type > NRF24_FP_UNKNOWN ) &&
        ( oldest->counted_type <= NRF24_FP_LOGITECH_LIKE ) &&
        ( category_counts[ oldest->counted_type ] > 0U ) )
    {
        --category_counts[ oldest->counted_type ];
    }

    if( NULL != oldest )
    {
        memset( oldest, 0, sizeof( *oldest ) );
        oldest->used = true;
        memcpy( oldest->address, a_fingerprint->address, a_fingerprint->address_len );
        oldest->address_len = a_fingerprint->address_len;
        oldest->last_seen_ms = a_last_seen_ms;
    }

    return oldest;
}

/** @brief Reconcile a classifier result with its deduplicated category count. */
static void reconcile_category( const packet_record_t *a_record )
{
    const nrf24_fingerprint_t *fingerprint = &a_record->public;
    address_ledger_t *ledger = find_or_allocate_ledger( fingerprint, a_record->last_seen_ms );

    if( NULL == ledger )
    {
        return;
    }

    if( NRF24_FP_UNKNOWN == ledger->counted_type )
    {
        bump_category( fingerprint->type );
        ledger->counted_type = fingerprint->type;
    }
    else if( ledger->counted_type != fingerprint->type )
    {
        if( category_counts[ ledger->counted_type ] > 0U )
        {
            --category_counts[ ledger->counted_type ];
        }
        bump_category( fingerprint->type );
        ledger->counted_type = fingerprint->type;
    }
}

/** @brief Classify one active packet record from timing, payload, and address evidence. */
static void classify_packet_record( packet_record_t *a_record )
{
    nrf24_fingerprint_t *fingerprint = &a_record->public;
    fingerprint->type = NRF24_FP_ESB;
    fingerprint->confidence = 55U;
    snprintf( fingerprint->label, sizeof( fingerprint->label ), "ESB device" );

    if( is_logitech_pairing_address( fingerprint->address, fingerprint->address_len ) )
    {
        fingerprint->type = NRF24_FP_LOGITECH_LIKE;
        fingerprint->confidence = 98U;
        snprintf( fingerprint->label, sizeof( fingerprint->label ), "Logitech?" );
    }
    else if( fingerprint->packets >= 6U )
    {
        const uint32_t average_interval =
            ( a_record->interval_samples > 0U )
                ? a_record->interval_total_ms / a_record->interval_samples
                : UINT32_MAX;
        const uint8_t payload_span = (uint8_t)( a_record->max_payload - a_record->min_payload );

        if( ( average_interval <= 25U ) && ( a_record->max_payload <= 20U ) )
        {
            fingerprint->type = NRF24_FP_INPUT;
            fingerprint->confidence = ( fingerprint->packets >= 20U ) ? 82U : 70U;
            snprintf( fingerprint->label, sizeof( fingerprint->label ), "Input?" );
        }
        else if( ( average_interval >= 100U ) && ( payload_span <= 1U ) )
        {
            fingerprint->type = NRF24_FP_SENSOR;
            fingerprint->confidence = ( fingerprint->packets >= 12U ) ? 78U : 66U;
            snprintf( fingerprint->label, sizeof( fingerprint->label ), "Sensor?" );
        }
        else
        {
            fingerprint->confidence = ( fingerprint->packets >= 12U ) ? 68U : 60U;
        }
    }

    reconcile_category( a_record );
}

/** @brief Find or allocate one entry in the small active packet classifier cache. */
static packet_record_t *find_or_allocate_record( const nrf24_packet_observation_t *a_packet )
{
    packet_record_t *oldest = NULL;

    for( size_t i = 0U; i < PACKET_RECORD_MAX; ++i )
    {
        packet_record_t *record = &packet_records[ i ];
        if( record->used )
        {
            if( address_equal( &record->public, a_packet ) )
            {
                return record;
            }
            if( ( NULL == oldest ) || ( record->last_seen_ms < oldest->last_seen_ms ) )
            {
                oldest = record;
            }
        }
        else
        {
            memset( record, 0, sizeof( *record ) );
            record->used = true;
            return record;
        }
    }

    if( NULL != oldest )
    {
        memset( oldest, 0, sizeof( *oldest ) );
        oldest->used = true;
    }
    return oldest;
}

void nrf24_activity_reset_counts( void )
{
    memset( category_counts, 0, sizeof( category_counts ) );
    memset( packet_records, 0, sizeof( packet_records ) );
    memset( address_ledger, 0, sizeof( address_ledger ) );
    spectrum_candidate = NRF24_FP_UNKNOWN;
    spectrum_candidate_sweeps = 0U;
    spectrum_latched = false;
}

void nrf24_activity_reset( void )
{
    memset( fast_activity, 0, sizeof( fast_activity ) );
    memset( slow_peak, 0, sizeof( slow_peak ) );
    nrf24_activity_reset_counts();

    portENTER_CRITICAL( &recent_packet_lock );
    memset( recent_packets, 0, sizeof( recent_packets ) );
    recent_packet_count = 0U;
    recent_packet_next = 0U;
    portEXIT_CRITICAL( &recent_packet_lock );
}

/** @brief Infer broad Wi-Fi/BLE-like spectrum shapes from the fast histogram. */
static nrf24_fingerprint_type_t spectrum_signature( void )
{
    unsigned longest_run = 0U;
    unsigned current_run = 0U;
    unsigned islands = 0U;
    unsigned busy_channels = 0U;
    bool in_island = false;

    for( size_t channel = 0U; channel < NRF24_CHANNEL_COUNT; ++channel )
    {
        const bool busy = fast_activity[ channel ] >= ACTIVITY_BUSY_THRESHOLD;

        if( busy )
        {
            ++busy_channels;
            ++current_run;
            if( current_run > longest_run )
            {
                longest_run = current_run;
            }
            if( !in_island )
            {
                ++islands;
                in_island = true;
            }
        }
        else
        {
            current_run = 0U;
            in_island = false;
        }
    }

    if( longest_run >= WIFI_LIKE_MIN_RUN )
    {
        return NRF24_FP_WIFI_LIKE;
    }
    if( ( islands >= BLE_LIKE_MIN_ISLANDS ) && ( busy_channels < 28U ) )
    {
        return NRF24_FP_BLE_LIKE;
    }
    return NRF24_FP_UNKNOWN;
}

/** @brief Debounce broad spectrum-shape fingerprints across consecutive sweeps. */
static void observe_spectrum_signature( void )
{
    const nrf24_fingerprint_type_t signature = spectrum_signature();

    if( NRF24_FP_UNKNOWN == signature )
    {
        spectrum_candidate = NRF24_FP_UNKNOWN;
        spectrum_candidate_sweeps = 0U;
        spectrum_latched = false;
        return;
    }

    if( signature != spectrum_candidate )
    {
        spectrum_candidate = signature;
        spectrum_candidate_sweeps = 1U;
        spectrum_latched = false;
        return;
    }

    if( spectrum_candidate_sweeps < UINT8_MAX )
    {
        ++spectrum_candidate_sweeps;
    }
    if( !spectrum_latched && ( spectrum_candidate_sweeps >= SPECTRUM_CONFIRM_SWEEPS ) )
    {
        bump_category( signature );
        spectrum_latched = true;
    }
}

void nrf24_activity_observe_sweep( const uint8_t *a_hits, size_t a_count )
{
    if( NULL == a_hits )
    {
        return;
    }

    const size_t count = ( a_count < NRF24_CHANNEL_COUNT ) ? a_count : NRF24_CHANNEL_COUNT;

    for( size_t i = 0U; i < count; ++i )
    {
        fast_activity[ i ] = fast_update( fast_activity[ i ], 0U != a_hits[ i ] );

        if( fast_activity[ i ] >= slow_peak[ i ] )
        {
            slow_peak[ i ] = fast_activity[ i ];
        }
        else if( slow_peak[ i ] > 0U )
        {
            uint8_t decay = (uint8_t)( slow_peak[ i ] / 32U );
            if( 0U == decay )
            {
                decay = 1U;
            }
            slow_peak[ i ] = ( slow_peak[ i ] > decay ) ? (uint8_t)( slow_peak[ i ] - decay ) : 0U;
        }
    }

    observe_spectrum_signature();
}

void nrf24_activity_observe_packet( const nrf24_packet_observation_t *a_packet )
{
    if( ( NULL == a_packet ) || !a_packet->crc_valid || ( a_packet->address_len < 3U ) ||
        ( a_packet->address_len > 5U ) || ( a_packet->payload_len > 32U ) )
    {
        return;
    }

    packet_record_t *record = find_or_allocate_record( a_packet );
    if( NULL == record )
    {
        return;
    }

    portENTER_CRITICAL( &recent_packet_lock );
    nrf24_recent_packet_t *recent = &recent_packets[ recent_packet_next ];
    memset( recent, 0, sizeof( *recent ) );
    recent->channel = a_packet->channel;
    memcpy( recent->address, a_packet->address, a_packet->address_len );
    recent->address_len = a_packet->address_len;
    memcpy( recent->payload, a_packet->payload, a_packet->payload_len );
    recent->payload_len = a_packet->payload_len;
    recent->timestamp_ms = a_packet->timestamp_ms;
    recent_packet_next = ( recent_packet_next + 1U ) % NRF24_RECENT_PACKET_MAX;
    if( recent_packet_count < NRF24_RECENT_PACKET_MAX )
    {
        ++recent_packet_count;
    }
    portEXIT_CRITICAL( &recent_packet_lock );

    nrf24_fingerprint_t *fingerprint = &record->public;
    if( 0U == fingerprint->packets )
    {
        memcpy( fingerprint->address, a_packet->address, a_packet->address_len );
        fingerprint->address_len = a_packet->address_len;
        fingerprint->bitrate = a_packet->bitrate;
        fingerprint->channel = a_packet->channel;
        record->first_seen_ms = a_packet->timestamp_ms;
        record->min_payload = a_packet->payload_len;
        record->max_payload = a_packet->payload_len;
    }
    else if( a_packet->timestamp_ms > record->last_seen_ms )
    {
        record->interval_total_ms += a_packet->timestamp_ms - record->last_seen_ms;
        ++record->interval_samples;
    }

    record->last_seen_ms = a_packet->timestamp_ms;
    fingerprint->channel = a_packet->channel;
    fingerprint->payload_len = a_packet->payload_len;
    ++fingerprint->packets;

    if( a_packet->payload_len < record->min_payload )
    {
        record->min_payload = a_packet->payload_len;
    }
    if( a_packet->payload_len > record->max_payload )
    {
        record->max_payload = a_packet->payload_len;
    }
    if( a_packet->no_ack && ( record->no_ack_count < UINT8_MAX ) )
    {
        ++record->no_ack_count;
    }

    classify_packet_record( record );
}

size_t nrf24_activity_recent_packets( nrf24_recent_packet_t *a_output, size_t a_capacity )
{
    if( ( NULL == a_output ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    portENTER_CRITICAL( &recent_packet_lock );
    const size_t count = ( a_capacity < recent_packet_count ) ? a_capacity : recent_packet_count;
    for( size_t i = 0U; i < count; ++i )
    {
        const size_t source =
            ( recent_packet_next + NRF24_RECENT_PACKET_MAX - 1U - i ) % NRF24_RECENT_PACKET_MAX;
        a_output[ i ] = recent_packets[ source ];
    }
    portEXIT_CRITICAL( &recent_packet_lock );
    return count;
}

size_t nrf24_activity_histogram( uint8_t *a_fast, uint8_t *a_slow, size_t a_capacity )
{
    if( ( NULL == a_fast ) || ( NULL == a_slow ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    const size_t count = ( a_capacity < NRF24_CHANNEL_COUNT ) ? a_capacity : NRF24_CHANNEL_COUNT;
    memcpy( a_fast, fast_activity, count );
    memcpy( a_slow, slow_peak, count );
    return count;
}

size_t nrf24_activity_fingerprints( nrf24_fingerprint_t *a_output, size_t a_capacity )
{
    static const nrf24_fingerprint_type_t rows[ NRF24_FINGERPRINT_MAX ] = {
        NRF24_FP_LOGITECH_LIKE,
        NRF24_FP_INPUT,
        NRF24_FP_SENSOR,
        NRF24_FP_ESB,
        NRF24_FP_WIFI_LIKE,
        NRF24_FP_BLE_LIKE,
    };

    if( ( NULL == a_output ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    const size_t count =
        ( a_capacity < NRF24_FINGERPRINT_MAX ) ? a_capacity : NRF24_FINGERPRINT_MAX;

    for( size_t i = 0U; i < count; ++i )
    {
        memset( &a_output[ i ], 0, sizeof( a_output[ i ] ) );
        a_output[ i ].type = rows[ i ];
        a_output[ i ].packets = category_counts[ rows[ i ] ];
        snprintf( a_output[ i ].label,
                  sizeof( a_output[ i ].label ),
                  "%s",
                  nrf24_activity_fingerprint_name( rows[ i ] ) );
    }

    return count;
}

uint32_t nrf24_activity_category_count( nrf24_fingerprint_type_t a_type )
{
    if( ( a_type <= NRF24_FP_UNKNOWN ) || ( a_type > NRF24_FP_LOGITECH_LIKE ) )
    {
        return 0U;
    }
    return category_counts[ a_type ];
}

const char *nrf24_activity_fingerprint_name( nrf24_fingerprint_type_t a_type )
{
    switch( a_type )
    {
        case NRF24_FP_ESB:
            return "ESB device";
        case NRF24_FP_INPUT:
            return "Input?";
        case NRF24_FP_SENSOR:
            return "Sensor?";
        case NRF24_FP_WIFI_LIKE:
            return "Wi-Fi?";
        case NRF24_FP_BLE_LIKE:
            return "Hopping RF?";
        case NRF24_FP_LOGITECH_LIKE:
            return "Logitech?";
        default:
            return "Unknown";
    }
}
