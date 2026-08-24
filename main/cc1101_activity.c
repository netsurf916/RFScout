/**
    cc1101_activity.c : direct CC1101 RSSI levels, peak hold, and passive fingerprints
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "cc1101_activity.h"

#include <stdio.h>
#include <string.h>

#define CC1101_ACTIVITY_FLOOR_DBM (-105)
#define CC1101_ACTIVITY_CEILING_DBM (-35)
#define CC1101_ACTIVE_THRESHOLD 54U
#define CC1101_STRONG_THRESHOLD 110U
#define CC1101_PEAK_DECAY_DIVISOR 32U
#define CC1101_BURST_MARGIN 28U
#define CC1101_MAX_SEGMENTS 8U

typedef struct
{
    size_t start;
    size_t end;
    uint8_t max_current;
    uint8_t max_peak;
    int8_t peak_rssi;
} activity_segment_t;

static uint8_t current_level[ CC1101_SPECTRUM_BINS ];
static uint8_t slow_peak[ CC1101_SPECTRUM_BINS ];
static int8_t last_rssi[ CC1101_SPECTRUM_BINS ];
static uint32_t last_low_khz;
static uint32_t last_high_khz;
static bool have_sweep;

static uint8_t strength_from_rssi( int8_t a_rssi )
{
    if( a_rssi <= CC1101_ACTIVITY_FLOOR_DBM )
    {
        return 0U;
    }
    if( a_rssi >= CC1101_ACTIVITY_CEILING_DBM )
    {
        return 255U;
    }

    return (uint8_t)( ( (int)a_rssi - CC1101_ACTIVITY_FLOOR_DBM ) * 255 /
                      ( CC1101_ACTIVITY_CEILING_DBM - CC1101_ACTIVITY_FLOOR_DBM ) );
}

static uint32_t frequency_for_bin( size_t a_bin )
{
    if( CC1101_SPECTRUM_BINS <= 1U )
    {
        return last_low_khz;
    }

    return last_low_khz +
           (uint32_t)( ( (uint64_t)( last_high_khz - last_low_khz ) * a_bin ) /
                       ( CC1101_SPECTRUM_BINS - 1U ) );
}

static bool in_common_ism_region( uint32_t a_frequency_khz )
{
    return ( ( a_frequency_khz >= 314000U ) && ( a_frequency_khz <= 316000U ) ) ||
           ( ( a_frequency_khz >= 433000U ) && ( a_frequency_khz <= 435000U ) ) ||
           ( ( a_frequency_khz >= 863000U ) && ( a_frequency_khz <= 870000U ) ) ||
           ( ( a_frequency_khz >= 902000U ) && ( a_frequency_khz <= 928000U ) );
}

static size_t collect_segments( activity_segment_t *a_segments,
                                size_t a_capacity )
{
    size_t count = 0U;
    size_t index = 0U;

    while( ( index < CC1101_SPECTRUM_BINS ) && ( count < a_capacity ) )
    {
        if( current_level[ index ] < CC1101_ACTIVE_THRESHOLD )
        {
            ++index;
            continue;
        }

        activity_segment_t *segment = &a_segments[ count ];
        memset( segment, 0, sizeof( *segment ) );
        segment->start = index;
        segment->peak_rssi = -127;

        while( ( index < CC1101_SPECTRUM_BINS ) &&
               ( current_level[ index ] >= CC1101_ACTIVE_THRESHOLD ) )
        {
            if( current_level[ index ] > segment->max_current )
            {
                segment->max_current = current_level[ index ];
            }
            if( slow_peak[ index ] > segment->max_peak )
            {
                segment->max_peak = slow_peak[ index ];
            }
            if( last_rssi[ index ] > segment->peak_rssi )
            {
                segment->peak_rssi = last_rssi[ index ];
            }
            segment->end = index;
            ++index;
        }

        ++count;
    }

    return count;
}

static void classify_segment( const activity_segment_t *a_segment,
                              cc1101_fingerprint_t *a_output )
{
    memset( a_output, 0, sizeof( *a_output ) );

    const uint32_t start_khz = frequency_for_bin( a_segment->start );
    const uint32_t end_khz = frequency_for_bin( a_segment->end );
    const uint32_t center_khz = start_khz + ( end_khz - start_khz ) / 2U;
    const uint32_t bandwidth_khz = end_khz - start_khz;
    const bool narrow = ( a_segment->end - a_segment->start ) <= 4U;
    const bool recently_stronger =
        ( a_segment->max_peak > a_segment->max_current ) &&
        ( (uint8_t)( a_segment->max_peak - a_segment->max_current ) >= CC1101_BURST_MARGIN );

    a_output->center_khz = center_khz;
    a_output->bandwidth_khz = bandwidth_khz;
    a_output->peak_rssi = a_segment->peak_rssi;

    if( narrow && in_common_ism_region( center_khz ) && recently_stronger )
    {
        a_output->type = CC1101_FP_REMOTE_SENSOR_LIKE;
        a_output->confidence = 62U;
        snprintf( a_output->label,
                  sizeof( a_output->label ),
                  "Remote/sensor-like" );
    }
    else if( narrow &&
             ( a_segment->max_current >= CC1101_STRONG_THRESHOLD ) &&
             ( a_segment->max_peak <= a_segment->max_current + 12U ) )
    {
        a_output->type = CC1101_FP_CONTINUOUS_CARRIER;
        a_output->confidence = 58U;
        snprintf( a_output->label,
                  sizeof( a_output->label ),
                  "Carrier-like" );
    }
    else if( narrow && recently_stronger )
    {
        a_output->type = CC1101_FP_BURSTY_SIGNAL;
        a_output->confidence = 52U;
        snprintf( a_output->label,
                  sizeof( a_output->label ),
                  "Bursty narrowband" );
    }
    else if( narrow )
    {
        a_output->type = CC1101_FP_NARROW_ISM;
        a_output->confidence = 48U;
        snprintf( a_output->label,
                  sizeof( a_output->label ),
                  "Narrowband RF" );
    }
    else
    {
        a_output->type = CC1101_FP_WIDEBAND_ACTIVITY;
        a_output->confidence = 40U;
        snprintf( a_output->label,
                  sizeof( a_output->label ),
                  "Wide RF activity" );
    }
}

void cc1101_activity_reset( void )
{
    memset( current_level, 0, sizeof( current_level ) );
    memset( slow_peak, 0, sizeof( slow_peak ) );
    memset( last_rssi, -120, sizeof( last_rssi ) );
    last_low_khz = 0U;
    last_high_khz = 0U;
    have_sweep = false;
}

void cc1101_activity_observe_sweep( const int8_t *a_rssi,
                                    size_t a_count,
                                    uint32_t a_low_khz,
                                    uint32_t a_high_khz )
{
    if( ( NULL == a_rssi ) || ( 0U == a_count ) )
    {
        return;
    }

    const size_t count = a_count < CC1101_SPECTRUM_BINS ?
                         a_count : CC1101_SPECTRUM_BINS;

    last_low_khz = a_low_khz;
    last_high_khz = a_high_khz;
    have_sweep = true;

    for( size_t i = 0U; i < CC1101_SPECTRUM_BINS; ++i )
    {
        const int8_t rssi = ( i < count ) ? a_rssi[ i ] : -120;
        const uint8_t strength = strength_from_rssi( rssi );
        last_rssi[ i ] = rssi;

        /* CC1101 provides an actual RSSI measurement. Use the latest completed
           sweep directly for the visible level rather than averaging sweeps. */
        current_level[ i ] = strength;

        if( strength >= slow_peak[ i ] )
        {
            slow_peak[ i ] = strength;
        }
        else if( slow_peak[ i ] > 0U )
        {
            uint8_t decay = (uint8_t)( slow_peak[ i ] / CC1101_PEAK_DECAY_DIVISOR );
            if( 0U == decay )
            {
                decay = 1U;
            }
            slow_peak[ i ] = slow_peak[ i ] > decay ?
                             (uint8_t)( slow_peak[ i ] - decay ) : 0U;
        }
    }
}

size_t cc1101_activity_levels( uint8_t *a_current,
                               uint8_t *a_peak,
                               size_t a_capacity )
{
    if( ( NULL == a_current ) || ( NULL == a_peak ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    const size_t count = a_capacity < CC1101_SPECTRUM_BINS ?
                         a_capacity : CC1101_SPECTRUM_BINS;
    memcpy( a_current, current_level, count );
    memcpy( a_peak, slow_peak, count );
    return count;
}

size_t cc1101_activity_fingerprints( cc1101_fingerprint_t *a_output,
                                     size_t a_capacity )
{
    if( ( NULL == a_output ) || ( 0U == a_capacity ) || !have_sweep )
    {
        return 0U;
    }

    activity_segment_t segments[ CC1101_MAX_SEGMENTS ];
    const size_t segment_count = collect_segments( segments, CC1101_MAX_SEGMENTS );
    size_t count = segment_count < a_capacity ? segment_count : a_capacity;

    for( size_t i = 0U; i < count; ++i )
    {
        classify_segment( &segments[ i ], &a_output[ i ] );
    }

    for( size_t i = 1U; i < count; ++i )
    {
        cc1101_fingerprint_t current = a_output[ i ];
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

const char *cc1101_activity_fingerprint_name( cc1101_fingerprint_type_t a_type )
{
    switch( a_type )
    {
        case CC1101_FP_NARROW_ISM: return "Narrowband RF";
        case CC1101_FP_REMOTE_SENSOR_LIKE: return "Remote/sensor-like";
        case CC1101_FP_CONTINUOUS_CARRIER: return "Carrier-like";
        case CC1101_FP_BURSTY_SIGNAL: return "Bursty narrowband";
        case CC1101_FP_WIDEBAND_ACTIVITY: return "Wide RF activity";
        case CC1101_FP_UNKNOWN:
        default: return "Unknown RF";
    }
}
