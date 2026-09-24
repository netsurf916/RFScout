/**
    cc1101_activity.c : direct CC1101 RSSI levels and passive fingerprints
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "cc1101_activity.h"

#include "app_session.h"

#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#define CC1101_ACTIVITY_FLOOR_DBM ( -105 )
#define CC1101_ACTIVITY_CEILING_DBM ( -35 )
#define CC1101_ACTIVE_THRESHOLD 54U
#define CC1101_STRONG_THRESHOLD 110U
#define CC1101_BURST_MARGIN 28U
#define CC1101_MAX_SEGMENTS 8U
#define CC1101_FP_CONFIRM_SWEEPS 2U
#define CC1101_FP_HOLD_SWEEPS 24U
#define CC1101_FP_MATCH_TOLERANCE_KHZ 12U
#define CC1101_CLASSIFIER_WARMUP_SWEEPS 3U
#define CC1101_HISTORY_INITIAL_CAPACITY 16U

typedef struct
{
    size_t start;
    size_t end;
    uint8_t max_current;
    uint8_t max_history;
    int8_t peak_rssi;
} activity_segment_t;

typedef struct
{
    cc1101_fingerprint_t fingerprint;
    uint8_t seen_streak;
    uint8_t missed_sweeps;
    bool confirmed;
    bool matched_this_sweep;
    bool episode_active;
    uint32_t episode_started_ms;
    uint32_t last_seen_ms;
} tracked_fingerprint_t;

static uint8_t current_level[ CC1101_SPECTRUM_BINS ];
/* Classifier-only history spans the two-sweep confirmation window. It is not
   rendered and has no slow-decay/peak-hold behavior. */
static uint8_t history_recent[ CC1101_SPECTRUM_BINS ];
static uint8_t history_older[ CC1101_SPECTRUM_BINS ];
static int8_t last_rssi[ CC1101_SPECTRUM_BINS ];
static uint32_t last_low_khz;
static uint32_t last_high_khz;
static bool have_sweep;
static uint8_t classifier_warmup_sweeps;
static tracked_fingerprint_t tracked[ CC1101_FINGERPRINT_MAX ];
static uint16_t fingerprint_counts[ CC1101_FP_TYPE_COUNT ];
static cc1101_activity_observation_t *history;
static size_t history_count;
static size_t history_capacity;
static uint32_t history_generation;
static uint32_t history_dropped;

static uint32_t frequency_delta( uint32_t a_left, uint32_t a_right )
{
    return a_left > a_right ? a_left - a_right : a_right - a_left;
}

static uint32_t current_time_ms( void )
{
    return (uint32_t)( esp_timer_get_time() / 1000ULL );
}

static void synchronize_history_session( void )
{
    const uint32_t generation = app_session_generation();
    if( history_generation == generation )
    {
        return;
    }

    history = NULL;
    history_count = 0U;
    history_capacity = 0U;
    history_dropped = 0U;
    history_generation = generation;
}

static bool ensure_history_capacity( size_t a_required )
{
    if( a_required <= history_capacity )
    {
        return true;
    }

    size_t capacity = history_capacity;
    if( 0U == capacity )
    {
        capacity = CC1101_HISTORY_INITIAL_CAPACITY;
    }
    while( capacity < a_required )
    {
        if( capacity > ( SIZE_MAX / 2U ) )
        {
            capacity = a_required;
            break;
        }
        capacity *= 2U;
    }

    cc1101_activity_observation_t *replacement =
        app_session_resize( history, capacity, sizeof( *history ) );
    if( NULL == replacement )
    {
        return false;
    }

    history = replacement;
    history_capacity = capacity;
    history_generation = app_session_generation();
    return true;
}

static uint32_t previous_observation_end_ms( const cc1101_fingerprint_t *a_fingerprint )
{
    for( size_t index = history_count; index > 0U; --index )
    {
        const cc1101_activity_observation_t *observation = &history[ index - 1U ];
        if( ( observation->type == a_fingerprint->type ) &&
            ( frequency_delta( observation->center_khz, a_fingerprint->center_khz ) <=
              CC1101_FP_MATCH_TOLERANCE_KHZ ) )
        {
            return observation->ended_ms;
        }
    }
    return 0U;
}

static void append_history( const tracked_fingerprint_t *a_entry )
{
    if( ( NULL == a_entry ) || !a_entry->confirmed || !a_entry->episode_active )
    {
        return;
    }

    synchronize_history_session();
    if( !ensure_history_capacity( history_count + 1U ) )
    {
        if( history_dropped < UINT32_MAX )
        {
            ++history_dropped;
        }
        return;
    }

    const uint32_t previous_end_ms = previous_observation_end_ms( &a_entry->fingerprint );
    cc1101_activity_observation_t *observation = &history[ history_count++ ];
    memset( observation, 0, sizeof( *observation ) );
    observation->type = a_entry->fingerprint.type;
    observation->confidence = a_entry->fingerprint.confidence;
    observation->center_khz = a_entry->fingerprint.center_khz;
    observation->bandwidth_khz = a_entry->fingerprint.bandwidth_khz;
    observation->peak_rssi = a_entry->fingerprint.peak_rssi;
    observation->started_ms = a_entry->episode_started_ms;
    observation->ended_ms = a_entry->last_seen_ms;
    observation->duration_ms =
        (uint32_t)( observation->ended_ms - observation->started_ms );
    if( 0U != previous_end_ms )
    {
        observation->gap_ms = (uint32_t)( observation->started_ms - previous_end_ms );
    }
}

static void finalize_episode( tracked_fingerprint_t *a_entry )
{
    if( ( NULL == a_entry ) || !a_entry->episode_active )
    {
        return;
    }

    append_history( a_entry );
    a_entry->episode_active = false;
}

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

    return last_low_khz + (uint32_t)( ( (uint64_t)( last_high_khz - last_low_khz ) * a_bin ) /
                                      ( CC1101_SPECTRUM_BINS - 1U ) );
}

static bool in_common_ism_region( uint32_t a_frequency_khz )
{
    return ( ( a_frequency_khz >= 314000U ) && ( a_frequency_khz <= 316000U ) ) ||
           ( ( a_frequency_khz >= 432000U ) && ( a_frequency_khz <= 435000U ) ) ||
           ( ( a_frequency_khz >= 863000U ) && ( a_frequency_khz <= 870000U ) ) ||
           ( ( a_frequency_khz >= 902000U ) && ( a_frequency_khz <= 928000U ) );
}

static bool bin_is_currently_active( size_t a_index )
{
    return current_level[ a_index ] >= CC1101_ACTIVE_THRESHOLD;
}

static size_t collect_segments( activity_segment_t *a_segments, size_t a_capacity )
{
    size_t count = 0U;
    size_t index = 0U;

    while( ( index < CC1101_SPECTRUM_BINS ) && ( count < a_capacity ) )
    {
        if( !bin_is_currently_active( index ) )
        {
            ++index;
            continue;
        }

        activity_segment_t *segment = &a_segments[ count ];
        memset( segment, 0, sizeof( *segment ) );
        segment->start = index;
        segment->peak_rssi = -127;

        while( ( index < CC1101_SPECTRUM_BINS ) && bin_is_currently_active( index ) )
        {
            if( current_level[ index ] > segment->max_current )
            {
                segment->max_current = current_level[ index ];
            }

            const uint8_t history = history_recent[ index ] > history_older[ index ]
                                        ? history_recent[ index ]
                                        : history_older[ index ];
            if( history > segment->max_history )
            {
                segment->max_history = history;
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

static void classify_segment( const activity_segment_t *a_segment, cc1101_fingerprint_t *a_output )
{
    memset( a_output, 0, sizeof( *a_output ) );

    const uint32_t start_khz = frequency_for_bin( a_segment->start );
    const uint32_t end_khz = frequency_for_bin( a_segment->end );
    const uint32_t center_khz = start_khz + ( end_khz - start_khz ) / 2U;
    const uint32_t bandwidth_khz = end_khz - start_khz;
    const bool narrow = ( a_segment->end - a_segment->start ) <= 4U;
    const bool recently_stronger =
        ( a_segment->max_history > a_segment->max_current ) &&
        ( (uint8_t)( a_segment->max_history - a_segment->max_current ) >= CC1101_BURST_MARGIN );

    a_output->center_khz = center_khz;
    a_output->bandwidth_khz = bandwidth_khz;
    a_output->peak_rssi = a_segment->peak_rssi;

    if( narrow && in_common_ism_region( center_khz ) && recently_stronger )
    {
        a_output->type = CC1101_FP_REMOTE_SENSOR_LIKE;
        a_output->confidence = 62U;
    }
    else if( narrow && ( a_segment->max_current >= CC1101_STRONG_THRESHOLD ) &&
             ( a_segment->max_history <= a_segment->max_current + 12U ) )
    {
        a_output->type = CC1101_FP_CONTINUOUS_CARRIER;
        a_output->confidence = 58U;
    }
    else if( narrow && recently_stronger )
    {
        a_output->type = CC1101_FP_BURSTY_SIGNAL;
        a_output->confidence = 52U;
    }
    else if( narrow )
    {
        a_output->type = CC1101_FP_NARROW_ISM;
        a_output->confidence = 48U;
    }
    else
    {
        a_output->type = CC1101_FP_WIDEBAND_ACTIVITY;
        a_output->confidence = 40U;
    }

    snprintf( a_output->label,
              sizeof( a_output->label ),
              "%s",
              cc1101_activity_fingerprint_name( a_output->type ) );
}

static int find_tracked_match( const cc1101_fingerprint_t *a_candidate )
{
    for( size_t i = 0U; i < CC1101_FINGERPRINT_MAX; ++i )
    {
        if( 0U == tracked[ i ].seen_streak )
        {
            continue;
        }

        if( ( tracked[ i ].fingerprint.type == a_candidate->type ) &&
            ( frequency_delta( tracked[ i ].fingerprint.center_khz, a_candidate->center_khz ) <=
              CC1101_FP_MATCH_TOLERANCE_KHZ ) )
        {
            return (int)i;
        }
    }

    return -1;
}

static int find_free_or_oldest_slot( void )
{
    size_t oldest = 0U;
    uint8_t most_missed = 0U;

    for( size_t i = 0U; i < CC1101_FINGERPRINT_MAX; ++i )
    {
        if( 0U == tracked[ i ].seen_streak )
        {
            return (int)i;
        }
        if( tracked[ i ].missed_sweeps >= most_missed )
        {
            most_missed = tracked[ i ].missed_sweeps;
            oldest = i;
        }
    }

    return (int)oldest;
}

static void record_confirmation( tracked_fingerprint_t *a_entry )
{
    if( a_entry->confirmed || ( a_entry->fingerprint.type <= CC1101_FP_UNKNOWN ) ||
        ( a_entry->fingerprint.type >= CC1101_FP_TYPE_COUNT ) )
    {
        return;
    }

    a_entry->confirmed = true;
    uint16_t *count = &fingerprint_counts[ a_entry->fingerprint.type ];
    if( *count < UINT16_MAX )
    {
        ++( *count );
    }
}

static void update_tracked_fingerprints( uint32_t a_now_ms )
{
    for( size_t i = 0U; i < CC1101_FINGERPRINT_MAX; ++i )
    {
        tracked[ i ].matched_this_sweep = false;
    }

    activity_segment_t segments[ CC1101_MAX_SEGMENTS ];
    const size_t segment_count = collect_segments( segments, CC1101_MAX_SEGMENTS );

    for( size_t i = 0U; i < segment_count; ++i )
    {
        cc1101_fingerprint_t candidate;
        classify_segment( &segments[ i ], &candidate );

        int slot = find_tracked_match( &candidate );
        if( slot < 0 )
        {
            slot = find_free_or_oldest_slot();
            finalize_episode( &tracked[ slot ] );
            memset( &tracked[ slot ], 0, sizeof( tracked[ slot ] ) );
        }

        tracked_fingerprint_t *entry = &tracked[ slot ];
        const bool was_confirmed = entry->confirmed;
        entry->fingerprint = candidate;
        entry->confirmed = was_confirmed;
        entry->matched_this_sweep = true;
        entry->missed_sweeps = 0U;
        if( !entry->episode_active )
        {
            entry->episode_active = true;
            entry->episode_started_ms = a_now_ms;
        }
        entry->last_seen_ms = a_now_ms;
        if( entry->seen_streak < 255U )
        {
            ++entry->seen_streak;
        }
        if( entry->seen_streak >= CC1101_FP_CONFIRM_SWEEPS )
        {
            record_confirmation( entry );
        }
    }

    for( size_t i = 0U; i < CC1101_FINGERPRINT_MAX; ++i )
    {
        tracked_fingerprint_t *entry = &tracked[ i ];
        if( ( 0U == entry->seen_streak ) || entry->matched_this_sweep )
        {
            continue;
        }

        finalize_episode( entry );
        if( entry->missed_sweeps < 255U )
        {
            ++entry->missed_sweeps;
        }

        if( entry->missed_sweeps >= CC1101_FP_HOLD_SWEEPS )
        {
            memset( entry, 0, sizeof( *entry ) );
        }
    }
}

void cc1101_activity_reset( void )
{
    synchronize_history_session();
    for( size_t i = 0U; i < CC1101_FINGERPRINT_MAX; ++i )
    {
        finalize_episode( &tracked[ i ] );
    }

    memset( current_level, 0, sizeof( current_level ) );
    memset( history_recent, 0, sizeof( history_recent ) );
    memset( history_older, 0, sizeof( history_older ) );
    memset( last_rssi, -120, sizeof( last_rssi ) );
    memset( tracked, 0, sizeof( tracked ) );
    last_low_khz = 0U;
    last_high_khz = 0U;
    have_sweep = false;
    classifier_warmup_sweeps = CC1101_CLASSIFIER_WARMUP_SWEEPS;
}

void cc1101_activity_reset_counts( void )
{
    synchronize_history_session();
    for( size_t i = 0U; i < CC1101_FINGERPRINT_MAX; ++i )
    {
        finalize_episode( &tracked[ i ] );
    }

    memset( fingerprint_counts, 0, sizeof( fingerprint_counts ) );
    memset( tracked, 0, sizeof( tracked ) );
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

    synchronize_history_session();
    const uint32_t now_ms = current_time_ms();
    const size_t count = a_count < CC1101_SPECTRUM_BINS ? a_count : CC1101_SPECTRUM_BINS;
    last_low_khz = a_low_khz;
    last_high_khz = a_high_khz;
    have_sweep = true;

    for( size_t i = 0U; i < CC1101_SPECTRUM_BINS; ++i )
    {
        const int8_t rssi = ( i < count ) ? a_rssi[ i ] : -120;
        const uint8_t strength = strength_from_rssi( rssi );
        history_older[ i ] = history_recent[ i ];
        history_recent[ i ] = current_level[ i ];
        last_rssi[ i ] = rssi;
        current_level[ i ] = strength;
    }

    if( classifier_warmup_sweeps > 0U )
    {
        --classifier_warmup_sweeps;
        memset( tracked, 0, sizeof( tracked ) );
        if( 0U == classifier_warmup_sweeps )
        {
            memset( history_recent, 0, sizeof( history_recent ) );
            memset( history_older, 0, sizeof( history_older ) );
        }
        return;
    }

    update_tracked_fingerprints( now_ms );
}

size_t cc1101_activity_levels( uint8_t *a_current, size_t a_capacity )
{
    if( ( NULL == a_current ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    const size_t count = a_capacity < CC1101_SPECTRUM_BINS ? a_capacity : CC1101_SPECTRUM_BINS;
    memcpy( a_current, current_level, count );
    return count;
}

size_t cc1101_activity_fingerprints( cc1101_fingerprint_t *a_output, size_t a_capacity )
{
    if( ( NULL == a_output ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    static const cc1101_fingerprint_type_t rows[] = { CC1101_FP_REMOTE_SENSOR_LIKE,
                                                      CC1101_FP_BURSTY_SIGNAL,
                                                      CC1101_FP_CONTINUOUS_CARRIER,
                                                      CC1101_FP_NARROW_ISM,
                                                      CC1101_FP_WIDEBAND_ACTIVITY };

    const size_t row_count = sizeof( rows ) / sizeof( rows[ 0 ] );
    const size_t count = a_capacity < row_count ? a_capacity : row_count;

    for( size_t row = 0U; row < count; ++row )
    {
        cc1101_fingerprint_t *output = &a_output[ row ];
        memset( output, 0, sizeof( *output ) );
        output->type = rows[ row ];
        output->count = fingerprint_counts[ output->type ];
        snprintf( output->label,
                  sizeof( output->label ),
                  "%s",
                  cc1101_activity_fingerprint_name( output->type ) );

        for( size_t i = 0U; i < CC1101_FINGERPRINT_MAX; ++i )
        {
            if( tracked[ i ].confirmed && ( tracked[ i ].fingerprint.type == output->type ) )
            {
                output->active = true;
                if( tracked[ i ].fingerprint.confidence > output->confidence )
                {
                    output->confidence = tracked[ i ].fingerprint.confidence;
                    output->center_khz = tracked[ i ].fingerprint.center_khz;
                    output->bandwidth_khz = tracked[ i ].fingerprint.bandwidth_khz;
                    output->peak_rssi = tracked[ i ].fingerprint.peak_rssi;
                }
            }
        }
    }

    return count;
}

size_t cc1101_activity_history( cc1101_activity_observation_t *a_output, size_t a_capacity )
{
    if( ( NULL == a_output ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    synchronize_history_session();
    const size_t count = a_capacity < history_count ? a_capacity : history_count;
    for( size_t index = 0U; index < count; ++index )
    {
        a_output[ index ] = history[ history_count - 1U - index ];
    }
    return count;
}

size_t cc1101_activity_history_count( void )
{
    synchronize_history_session();
    return history_count;
}

uint32_t cc1101_activity_history_dropped( void )
{
    synchronize_history_session();
    return history_dropped;
}

const char *cc1101_activity_fingerprint_name( cc1101_fingerprint_type_t a_type )
{
    switch( a_type )
    {
        case CC1101_FP_NARROW_ISM:
            return "Narrowband";
        case CC1101_FP_REMOTE_SENSOR_LIKE:
            return "Remote/sensor";
        case CC1101_FP_CONTINUOUS_CARRIER:
            return "Carrier";
        case CC1101_FP_BURSTY_SIGNAL:
            return "Bursty RF";
        case CC1101_FP_WIDEBAND_ACTIVITY:
            return "Wide RF";
        case CC1101_FP_UNKNOWN:
        case CC1101_FP_TYPE_COUNT:
        default:
            return "Unknown RF";
    }
}