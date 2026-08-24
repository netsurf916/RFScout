/**
    nrf24_activity.c : adaptive nRF24 activity histogram and fingerprint store
    Description: Maintains fast/slow spectrum history and conservative passive
                 classifications from validated ESB packet metadata.
    SPDX-License-Identifier: MIT
*/
#include "nrf24_activity.h"

#include <stdio.h>
#include <string.h>

#define ACTIVITY_BUSY_THRESHOLD 72U
#define ACTIVITY_STRONG_THRESHOLD 128U
#define WIFI_LIKE_MIN_RUN 12U
#define BLE_LIKE_MIN_ISLANDS 7U
#define PACKET_RECORD_MAX 5U
#define PACKET_STALE_MS 30000U

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

static uint8_t fast_activity[ NRF24_CHANNEL_COUNT ];
static uint8_t slow_peak[ NRF24_CHANNEL_COUNT ];
static packet_record_t packet_records[ PACKET_RECORD_MAX ];

static uint8_t fast_update( uint8_t a_old, bool a_hit )
{
    return (uint8_t)( ( (uint16_t)a_old * 7U +
                        ( a_hit ? 255U : 0U ) ) / 8U );
}

static bool address_equal( const nrf24_fingerprint_t *a_record,
                           const nrf24_packet_observation_t *a_packet )
{
    return ( a_record->address_len == a_packet->address_len ) &&
           ( 0 == memcmp( a_record->address,
                          a_packet->address,
                          a_packet->address_len ) );
}

static bool is_logitech_pairing_address( const uint8_t *a_address,
                                         uint8_t a_length )
{
    static const uint8_t pairing[ 5 ] =
    {
        0xBBU, 0x0AU, 0xDCU, 0xA5U, 0x75U
    };

    return ( 5U == a_length ) &&
           ( 0 == memcmp( a_address, pairing, sizeof( pairing ) ) );
}

static void classify_packet_record( packet_record_t *a_record )
{
    nrf24_fingerprint_t *fingerprint = &a_record->public;
    fingerprint->type = NRF24_FP_ESB;
    fingerprint->confidence = 55U;
    snprintf( fingerprint->label,
              sizeof( fingerprint->label ),
              "ESB device" );

    if( is_logitech_pairing_address( fingerprint->address,
                                     fingerprint->address_len ) )
    {
        fingerprint->type = NRF24_FP_LOGITECH_LIKE;
        fingerprint->confidence = 98U;
        snprintf( fingerprint->label,
                  sizeof( fingerprint->label ),
                  "Logitech pairing" );
        return;
    }

    if( fingerprint->packets >= 6U )
    {
        const uint32_t average_interval =
            ( a_record->interval_samples > 0U ) ?
            ( a_record->interval_total_ms / a_record->interval_samples ) :
            UINT32_MAX;
        const uint8_t payload_span =
            (uint8_t)( a_record->max_payload - a_record->min_payload );

        /* Short, rapidly repeating ESB packets are characteristic of many
           human-interface remotes/keyboards/mice. This is intentionally a
           type-level inference rather than a vendor claim. */
        if( ( average_interval <= 25U ) &&
            ( a_record->max_payload <= 20U ) )
        {
            fingerprint->type = NRF24_FP_INPUT;
            fingerprint->confidence = ( fingerprint->packets >= 20U ) ? 82U : 70U;
            snprintf( fingerprint->label,
                      sizeof( fingerprint->label ),
                      "Input-like ESB" );
        }
        /* Stable-length, slower periodic traffic is common among sensors and
           simple remotes. Require several packets and a nearly fixed size. */
        else if( ( average_interval >= 100U ) &&
                 ( payload_span <= 1U ) )
        {
            fingerprint->type = NRF24_FP_SENSOR;
            fingerprint->confidence = ( fingerprint->packets >= 12U ) ? 78U : 66U;
            snprintf( fingerprint->label,
                      sizeof( fingerprint->label ),
                      "Sensor-like ESB" );
        }
        else
        {
            fingerprint->confidence = ( fingerprint->packets >= 12U ) ? 68U : 60U;
        }
    }
}

static packet_record_t *find_or_allocate_record(
    const nrf24_packet_observation_t *a_packet )
{
    packet_record_t *oldest = NULL;

    for( size_t i = 0U; i < PACKET_RECORD_MAX; ++i )
    {
        if( packet_records[ i ].used )
        {
            if( address_equal( &packet_records[ i ].public, a_packet ) )
            {
                return &packet_records[ i ];
            }
            if( ( NULL == oldest ) ||
                ( packet_records[ i ].last_seen_ms < oldest->last_seen_ms ) )
            {
                oldest = &packet_records[ i ];
            }
        }
        else
        {
            memset( &packet_records[ i ], 0, sizeof( packet_records[ i ] ) );
            packet_records[ i ].used = true;
            return &packet_records[ i ];
        }
    }

    memset( oldest, 0, sizeof( *oldest ) );
    oldest->used = true;
    return oldest;
}

void nrf24_activity_reset( void )
{
    memset( fast_activity, 0, sizeof( fast_activity ) );
    memset( slow_peak, 0, sizeof( slow_peak ) );
    memset( packet_records, 0, sizeof( packet_records ) );
}

void nrf24_activity_observe_sweep( const uint8_t *a_hits, size_t a_count )
{
    if( NULL == a_hits )
    {
        return;
    }

    const size_t count = a_count < NRF24_CHANNEL_COUNT ?
                         a_count : NRF24_CHANNEL_COUNT;
    for( size_t i = 0U; i < count; ++i )
    {
        fast_activity[ i ] =
            fast_update( fast_activity[ i ], 0U != a_hits[ i ] );

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
            slow_peak[ i ] = slow_peak[ i ] > decay ?
                             (uint8_t)( slow_peak[ i ] - decay ) : 0U;
        }
    }
}

void nrf24_activity_observe_packet( const nrf24_packet_observation_t *a_packet )
{
    if( ( NULL == a_packet ) ||
        !a_packet->crc_valid ||
        ( a_packet->address_len < 3U ) ||
        ( a_packet->address_len > 5U ) ||
        ( a_packet->payload_len > 32U ) )
    {
        return;
    }

    packet_record_t *record = find_or_allocate_record( a_packet );
    nrf24_fingerprint_t *fingerprint = &record->public;

    if( 0U == fingerprint->packets )
    {
        memcpy( fingerprint->address,
                a_packet->address,
                a_packet->address_len );
        fingerprint->address_len = a_packet->address_len;
        fingerprint->bitrate = a_packet->bitrate;
        fingerprint->channel = a_packet->channel;
        record->first_seen_ms = a_packet->timestamp_ms;
        record->min_payload = a_packet->payload_len;
        record->max_payload = a_packet->payload_len;
    }
    else if( a_packet->timestamp_ms > record->last_seen_ms )
    {
        record->interval_total_ms +=
            a_packet->timestamp_ms - record->last_seen_ms;
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

size_t nrf24_activity_histogram( uint8_t *a_fast,
                                 uint8_t *a_slow,
                                 size_t a_capacity )
{
    if( ( NULL == a_fast ) || ( NULL == a_slow ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    const size_t count = a_capacity < NRF24_CHANNEL_COUNT ?
                         a_capacity : NRF24_CHANNEL_COUNT;
    memcpy( a_fast, fast_activity, count );
    memcpy( a_slow, slow_peak, count );
    return count;
}

static bool spectrum_signature( nrf24_fingerprint_t *a_output )
{
    unsigned longest_run = 0U;
    unsigned current_run = 0U;
    unsigned islands = 0U;
    bool in_island = false;
    unsigned busy_channels = 0U;

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

    memset( a_output, 0, sizeof( *a_output ) );
    if( longest_run >= WIFI_LIKE_MIN_RUN )
    {
        a_output->type = NRF24_FP_WIFI_LIKE;
        a_output->confidence = longest_run >= 18U ? 58U : 48U;
        snprintf( a_output->label,
                  sizeof( a_output->label ),
                  "Wi-Fi-like RF" );
        return true;
    }

    if( ( islands >= BLE_LIKE_MIN_ISLANDS ) &&
        ( busy_channels < 28U ) )
    {
        a_output->type = NRF24_FP_BLE_LIKE;
        a_output->confidence = 35U;
        snprintf( a_output->label,
                  sizeof( a_output->label ),
                  "Hopping RF" );
        return true;
    }

    return false;
}

size_t nrf24_activity_fingerprints( nrf24_fingerprint_t *a_output,
                                    size_t a_capacity )
{
    if( ( NULL == a_output ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    size_t count = 0U;
    uint32_t newest_seen = 0U;
    for( size_t i = 0U; i < PACKET_RECORD_MAX; ++i )
    {
        if( packet_records[ i ].used &&
            ( packet_records[ i ].last_seen_ms > newest_seen ) )
        {
            newest_seen = packet_records[ i ].last_seen_ms;
        }
    }

    for( size_t i = 0U;
         ( i < PACKET_RECORD_MAX ) && ( count < a_capacity );
         ++i )
    {
        if( !packet_records[ i ].used )
        {
            continue;
        }
        if( ( newest_seen > packet_records[ i ].last_seen_ms ) &&
            ( ( newest_seen - packet_records[ i ].last_seen_ms ) >
              PACKET_STALE_MS ) )
        {
            continue;
        }
        a_output[ count++ ] = packet_records[ i ].public;
    }

    if( count < a_capacity )
    {
        nrf24_fingerprint_t signature;
        if( spectrum_signature( &signature ) )
        {
            a_output[ count++ ] = signature;
        }
    }

    /* Highest-confidence observations are most useful in the small side list. */
    for( size_t i = 1U; i < count; ++i )
    {
        nrf24_fingerprint_t current = a_output[ i ];
        size_t position = i;
        while( ( position > 0U ) &&
               ( current.confidence > a_output[ position - 1U ].confidence ) )
        {
            a_output[ position ] = a_output[ position - 1U ];
            --position;
        }
        a_output[ position ] = current;
    }

    return count;
}

const char *nrf24_activity_fingerprint_name( nrf24_fingerprint_type_t a_type )
{
    switch( a_type )
    {
        case NRF24_FP_ESB: return "ESB device";
        case NRF24_FP_INPUT: return "Input-like";
        case NRF24_FP_SENSOR: return "Sensor-like";
        case NRF24_FP_WIFI_LIKE: return "Wi-Fi-like";
        case NRF24_FP_BLE_LIKE: return "Hopping RF";
        case NRF24_FP_LOGITECH_LIKE: return "Logitech-like";
        default: return "Unknown";
    }
}
