/**
    subghz_decode.c : RFScout bounded Sub-GHz analysis and protocol decode framework
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "subghz_decode.h"

#include <limits.h>
#include <string.h>

#define HISTOGRAM_QUANTUM_US 25U
#define PRINCETON_FRAME_BITS 24U
#define PRINCETON_RESET_MIN_US 8000U
#define PRINCETON_SHORT_MIN_US 100U
#define PRINCETON_SHORT_MAX_US 1000U
#define PRINCETON_RATIO_MIN_X100 200U
#define PRINCETON_RATIO_MAX_X100 500U
#define PRINCETON_MAX_ROWS 8U

typedef struct
{
    uint64_t data;
    uint16_t bit_count;
    uint32_t short_sum_us;
    uint16_t short_count;
} pwm_row_t;

typedef struct
{
    pwm_row_t rows[ PRINCETON_MAX_ROWS ];
    size_t row_count;
} pwm_rows_t;

typedef bool ( *protocol_decoder_fn )( const rfscout_subghz_capture_t *a_capture,
                                       rfscout_subghz_protocol_result_t *a_result );

typedef struct
{
    const char *name;
    protocol_decoder_fn decode;
} protocol_decoder_t;

static uint32_t quantize_duration( uint32_t a_duration_us )
{
    return ( ( a_duration_us + ( HISTOGRAM_QUANTUM_US / 2U ) ) / HISTOGRAM_QUANTUM_US ) *
           HISTOGRAM_QUANTUM_US;
}

static void
histogram_add( rfscout_subghz_analysis_t *a_output, bool a_level, uint32_t a_duration_us )
{
    const uint32_t bucket_duration = quantize_duration( a_duration_us );
    size_t free_index = RFSCOUT_SUBGHZ_TIMING_BUCKETS;
    size_t weakest_index = 0U;
    uint32_t weakest_total = UINT32_MAX;

    for( size_t index = 0U; index < RFSCOUT_SUBGHZ_TIMING_BUCKETS; ++index )
    {
        rfscout_subghz_timing_bucket_t *bucket = &a_output->timing[ index ];
        const uint32_t total = (uint32_t)bucket->high_count + bucket->low_count;

        if( ( total > 0U ) && ( bucket->duration_us == bucket_duration ) )
        {
            if( a_level )
            {
                if( bucket->high_count < UINT16_MAX )
                {
                    ++bucket->high_count;
                }
            }
            else if( bucket->low_count < UINT16_MAX )
            {
                ++bucket->low_count;
            }
            return;
        }

        if( ( 0U == total ) && ( RFSCOUT_SUBGHZ_TIMING_BUCKETS == free_index ) )
        {
            free_index = index;
        }
        if( total < weakest_total )
        {
            weakest_total = total;
            weakest_index = index;
        }
    }

    const size_t target =
        ( RFSCOUT_SUBGHZ_TIMING_BUCKETS != free_index ) ? free_index : weakest_index;
    rfscout_subghz_timing_bucket_t *bucket = &a_output->timing[ target ];

    if( ( RFSCOUT_SUBGHZ_TIMING_BUCKETS == free_index ) && ( weakest_total > 1U ) )
    {
        if( bucket->high_count > 0U )
        {
            --bucket->high_count;
        }
        if( bucket->low_count > 0U )
        {
            --bucket->low_count;
        }
        return;
    }

    bucket->duration_us = bucket_duration;
    bucket->high_count = a_level ? 1U : 0U;
    bucket->low_count = a_level ? 0U : 1U;
}

static void histogram_finalize( rfscout_subghz_analysis_t *a_output )
{
    for( size_t left = 0U; left < RFSCOUT_SUBGHZ_TIMING_BUCKETS; ++left )
    {
        for( size_t right = left + 1U; right < RFSCOUT_SUBGHZ_TIMING_BUCKETS; ++right )
        {
            const uint32_t left_total =
                (uint32_t)a_output->timing[ left ].high_count + a_output->timing[ left ].low_count;
            const uint32_t right_total = (uint32_t)a_output->timing[ right ].high_count +
                                         a_output->timing[ right ].low_count;
            if( right_total > left_total )
            {
                const rfscout_subghz_timing_bucket_t temporary = a_output->timing[ left ];
                a_output->timing[ left ] = a_output->timing[ right ];
                a_output->timing[ right ] = temporary;
            }
        }
    }

    a_output->timing_count = 0U;
    while( ( a_output->timing_count < RFSCOUT_SUBGHZ_TIMING_BUCKETS ) &&
           ( ( a_output->timing[ a_output->timing_count ].high_count > 0U ) ||
             ( a_output->timing[ a_output->timing_count ].low_count > 0U ) ) )
    {
        ++a_output->timing_count;
    }
}

static void
analysis_add_level( rfscout_subghz_analysis_t *a_output, bool a_level, uint16_t a_ticks )
{
    if( 0U == a_ticks )
    {
        return;
    }

    const uint32_t duration_us = rfscout_subghz_ticks_to_us( a_ticks, a_output->resolution_hz );
    if( 0U == duration_us )
    {
        return;
    }

    ++a_output->timed_level_count;
    if( UINT32_MAX - a_output->total_duration_us < duration_us )
    {
        a_output->total_duration_us = UINT32_MAX;
    }
    else
    {
        a_output->total_duration_us += duration_us;
    }

    if( duration_us < a_output->min_duration_us )
    {
        a_output->min_duration_us = duration_us;
    }
    if( duration_us > a_output->max_duration_us )
    {
        a_output->max_duration_us = duration_us;
    }
    histogram_add( a_output, a_level, duration_us );
}

static bool
pwm_pair_to_bit( uint32_t a_high_us, uint32_t a_low_us, uint8_t *a_bit, uint32_t *a_short_us )
{
    if( ( 0U == a_high_us ) || ( 0U == a_low_us ) )
    {
        return false;
    }

    const uint32_t short_us = a_high_us < a_low_us ? a_high_us : a_low_us;
    const uint32_t long_us = a_high_us < a_low_us ? a_low_us : a_high_us;
    if( ( short_us < PRINCETON_SHORT_MIN_US ) || ( short_us > PRINCETON_SHORT_MAX_US ) )
    {
        return false;
    }

    const uint32_t ratio_x100 = ( long_us * 100U ) / short_us;
    if( ( ratio_x100 < PRINCETON_RATIO_MIN_X100 ) || ( ratio_x100 > PRINCETON_RATIO_MAX_X100 ) )
    {
        return false;
    }

    *a_bit = ( a_high_us > a_low_us ) ? 1U : 0U;
    *a_short_us = short_us;
    return true;
}

static void pwm_rows_commit( pwm_rows_t *a_rows, pwm_row_t *a_current )
{
    if( ( NULL == a_rows ) || ( NULL == a_current ) )
    {
        return;
    }

    if( ( PRINCETON_FRAME_BITS == a_current->bit_count ) &&
        ( a_rows->row_count < PRINCETON_MAX_ROWS ) )
    {
        a_rows->rows[ a_rows->row_count++ ] = *a_current;
    }
    memset( a_current, 0, sizeof( *a_current ) );
}

static void princeton_consume_pair( pwm_rows_t *a_rows,
                                    pwm_row_t *a_current,
                                    uint32_t a_high_us,
                                    uint32_t a_low_us )
{
    if( a_low_us >= PRINCETON_RESET_MIN_US )
    {
        pwm_rows_commit( a_rows, a_current );
        return;
    }

    if( a_current->bit_count >= PRINCETON_FRAME_BITS )
    {
        memset( a_current, 0, sizeof( *a_current ) );
        return;
    }

    uint8_t bit = 0U;
    uint32_t short_us = 0U;
    if( !pwm_pair_to_bit( a_high_us, a_low_us, &bit, &short_us ) )
    {
        memset( a_current, 0, sizeof( *a_current ) );
        return;
    }

    a_current->data = ( a_current->data << 1U ) | bit;
    ++a_current->bit_count;
    a_current->short_sum_us += short_us;
    ++a_current->short_count;
}

static bool decode_princeton( const rfscout_subghz_capture_t *a_capture,
                              rfscout_subghz_protocol_result_t *a_result )
{
    pwm_rows_t rows = { 0 };
    pwm_row_t current = { 0 };
    bool have_high = false;
    uint32_t high_us = 0U;

    for( size_t index = 0U; index < a_capture->symbol_count; ++index )
    {
        const rfscout_subghz_symbol_t symbol = a_capture->symbols[ index ];
        const uint16_t durations[ 2 ] = { rfscout_subghz_symbol_duration0( symbol ),
                                          rfscout_subghz_symbol_duration1( symbol ) };
        const bool levels[ 2 ] = { rfscout_subghz_symbol_level0( symbol ),
                                   rfscout_subghz_symbol_level1( symbol ) };

        for( size_t half = 0U; half < 2U; ++half )
        {
            if( 0U == durations[ half ] )
            {
                continue;
            }

            const uint32_t duration_us =
                rfscout_subghz_ticks_to_us( durations[ half ], a_capture->resolution_hz );
            if( levels[ half ] )
            {
                high_us = duration_us;
                have_high = true;
            }
            else if( have_high )
            {
                princeton_consume_pair( &rows, &current, high_us, duration_us );
                have_high = false;
            }
            else if( duration_us >= PRINCETON_RESET_MIN_US )
            {
                pwm_rows_commit( &rows, &current );
            }
        }
    }
    pwm_rows_commit( &rows, &current );

    for( size_t left = 0U; left < rows.row_count; ++left )
    {
        uint16_t repeats = 1U;
        uint32_t short_sum = rows.rows[ left ].short_sum_us;
        uint16_t short_count = rows.rows[ left ].short_count;

        for( size_t right = left + 1U; right < rows.row_count; ++right )
        {
            if( rows.rows[ right ].data == rows.rows[ left ].data )
            {
                ++repeats;
                short_sum += rows.rows[ right ].short_sum_us;
                short_count += rows.rows[ right ].short_count;
            }
        }

        if( repeats >= 2U )
        {
            memset( a_result, 0, sizeof( *a_result ) );
            a_result->status = RFSCOUT_SUBGHZ_DECODE_MATCH;
            memcpy( a_result->protocol, "Princeton", sizeof( "Princeton" ) );
            a_result->bit_count = PRINCETON_FRAME_BITS;
            a_result->data = rows.rows[ left ].data;
            a_result->repeats = repeats;
            if( short_count > 0U )
            {
                a_result->symbol_period_us = ( short_sum / short_count ) * 4U;
            }
            return true;
        }
    }

    return false;
}

static const protocol_decoder_t decoders[] = {
    { .name = "Princeton", .decode = decode_princeton },
};

void rfscout_subghz_decode( const rfscout_subghz_capture_t *a_capture,
                            rfscout_subghz_analysis_t *a_output )
{
    if( NULL == a_output )
    {
        return;
    }

    memset( a_output, 0, sizeof( *a_output ) );
    a_output->min_duration_us = UINT32_MAX;

    if( ( NULL == a_capture ) || ( NULL == a_capture->symbols ) ||
        ( 0U == a_capture->symbol_count ) )
    {
        a_output->min_duration_us = 0U;
        return;
    }

    a_output->sequence = a_capture->sequence;
    a_output->frequency_hz = a_capture->frequency_hz;
    a_output->resolution_hz = a_capture->resolution_hz;
    a_output->symbol_count = a_capture->symbol_count;
    a_output->peak_rssi_dbm = a_capture->peak_rssi_dbm;
    a_output->truncated = a_capture->truncated;

    for( size_t index = 0U; index < a_capture->symbol_count; ++index )
    {
        const rfscout_subghz_symbol_t symbol = a_capture->symbols[ index ];
        analysis_add_level( a_output,
                            rfscout_subghz_symbol_level0( symbol ),
                            rfscout_subghz_symbol_duration0( symbol ) );
        analysis_add_level( a_output,
                            rfscout_subghz_symbol_level1( symbol ),
                            rfscout_subghz_symbol_duration1( symbol ) );
    }

    if( UINT32_MAX == a_output->min_duration_us )
    {
        a_output->min_duration_us = 0U;
    }
    histogram_finalize( a_output );

    for( size_t index = 0U; index < ( sizeof( decoders ) / sizeof( decoders[ 0 ] ) ); ++index )
    {
        rfscout_subghz_protocol_result_t result = { 0 };
        if( decoders[ index ].decode( a_capture, &result ) )
        {
            a_output->protocol = result;
            break;
        }
    }
}
