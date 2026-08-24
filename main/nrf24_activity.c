/**
    nrf24_activity.c : adaptive nRF24 activity histogram and fingerprint store
    SPDX-License-Identifier: MIT
*/
#include "nrf24_activity.h"
#include <string.h>

static uint8_t fast_activity[ NRF24_CHANNEL_COUNT ];
static uint8_t slow_peak[ NRF24_CHANNEL_COUNT ];
static nrf24_fingerprint_t fingerprints[ NRF24_FINGERPRINT_MAX ];
static size_t fingerprint_count;

static uint8_t fast_update( uint8_t a_old, bool a_hit )
{
    /* EWMA: recent sweeps dominate without counters ever saturating. */
    return (uint8_t)( ( (uint16_t)a_old * 7U + ( a_hit ? 255U : 0U ) ) / 8U );
}

void nrf24_activity_reset( void )
{
    memset( fast_activity, 0, sizeof( fast_activity ) );
    memset( slow_peak, 0, sizeof( slow_peak ) );
    memset( fingerprints, 0, sizeof( fingerprints ) );
    fingerprint_count = 0U;
}

void nrf24_activity_observe_sweep( const uint8_t *a_hits, size_t a_count )
{
    if( NULL == a_hits ) return;
    const size_t count = a_count < NRF24_CHANNEL_COUNT ? a_count : NRF24_CHANNEL_COUNT;
    for( size_t i = 0; i < count; ++i )
    {
        fast_activity[ i ] = fast_update( fast_activity[ i ], 0U != a_hits[ i ] );
        if( fast_activity[ i ] >= slow_peak[ i ] )
        {
            slow_peak[ i ] = fast_activity[ i ];
        }
        else if( slow_peak[ i ] > 0U )
        {
            /* Slow peak-hold decay: at least one unit, faster when very high. */
            uint8_t decay = (uint8_t)( slow_peak[ i ] / 32U );
            if( 0U == decay ) decay = 1U;
            slow_peak[ i ] = slow_peak[ i ] > decay ?
                             (uint8_t)( slow_peak[ i ] - decay ) : 0U;
        }
    }
}

size_t nrf24_activity_histogram( uint8_t *a_fast,
                                 uint8_t *a_slow,
                                 size_t a_capacity )
{
    if( NULL == a_fast || NULL == a_slow || 0U == a_capacity ) return 0U;
    const size_t count = a_capacity < NRF24_CHANNEL_COUNT ?
                         a_capacity : NRF24_CHANNEL_COUNT;
    memcpy( a_fast, fast_activity, count );
    memcpy( a_slow, slow_peak, count );
    return count;
}

size_t nrf24_activity_fingerprints( nrf24_fingerprint_t *a_output,
                                    size_t a_capacity )
{
    if( NULL == a_output || 0U == a_capacity ) return 0U;
    const size_t count = a_capacity < fingerprint_count ? a_capacity : fingerprint_count;
    memcpy( a_output, fingerprints, count * sizeof( fingerprints[ 0 ] ) );
    return count;
}

const char *nrf24_activity_fingerprint_name( nrf24_fingerprint_type_t a_type )
{
    switch( a_type )
    {
        case NRF24_FP_ESB: return "ESB device";
        case NRF24_FP_INPUT: return "Input device";
        case NRF24_FP_SENSOR: return "Sensor/remote";
        case NRF24_FP_WIFI_LIKE: return "Wi-Fi-like";
        case NRF24_FP_BLE_LIKE: return "BLE-like";
        default: return "Unknown";
    }
}

/* Fingerprints intentionally remain empty until packet candidates pass
   framing/CRC validation. RPD energy alone is not evidence of an nRF24 device.
   The store and UI contract are established here for the packet-discovery
   stage: channel/rate, address, payload length, packet count and confidence. */
