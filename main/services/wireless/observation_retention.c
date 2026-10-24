/**
    observation_retention.c : shared time, GPS and budget calculations
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "observation_retention.h"

#include <math.h>

bool observation_retention_expired( uint32_t a_now_ms,
                                    uint32_t a_last_seen_ms,
                                    uint32_t a_timeout_ms,
                                    uint32_t a_excluded_ms )
{
    if( 0U == a_timeout_ms )
    {
        return false;
    }
    const uint32_t elapsed = a_now_ms - a_last_seen_ms;
    const uint32_t effective = elapsed > a_excluded_ms ? elapsed - a_excluded_ms : 0U;
    return effective >= a_timeout_ms;
}

bool observation_retention_outside_coverage( const geo_bounds_t *a_bounds,
                                             double a_receiver_latitude,
                                             double a_receiver_longitude,
                                             uint32_t a_margin_m,
                                             bool a_gps_fresh,
                                             bool a_currently_visible )
{
    if( NULL == a_bounds || !a_bounds->valid || 0U == a_margin_m || !a_gps_fresh ||
        a_currently_visible )
    {
        return false;
    }
    double center_latitude = 0.0;
    double center_longitude = 0.0;
    if( !geo_bounds_center( a_bounds, &center_latitude, &center_longitude ) )
    {
        return false;
    }
    const double first_diagonal = geo_distance_m( a_bounds->min_latitude,
                                                   a_bounds->min_longitude,
                                                   a_bounds->max_latitude,
                                                   a_bounds->max_longitude );
    const double second_diagonal = geo_distance_m( a_bounds->min_latitude,
                                                    a_bounds->max_longitude,
                                                    a_bounds->max_latitude,
                                                    a_bounds->min_longitude );
    const double radius = fmax( first_diagonal, second_diagonal ) / 2.0;
    const double distance = geo_distance_m(
        center_latitude, center_longitude, a_receiver_latitude, a_receiver_longitude );
    return distance - radius > (double)a_margin_m;
}

int8_t observation_retention_rssi_ema( int8_t a_previous, int8_t a_latest )
{
    return (int8_t)( ( 3 * (int)a_previous + (int)a_latest ) / 4 );
}

bool observation_retention_over_budget( size_t a_used_bytes,
                                        size_t a_admission_bytes,
                                        size_t a_budget_bytes )
{
    return 0U != a_budget_bytes &&
           ( a_used_bytes > a_budget_bytes ||
             a_admission_bytes > a_budget_bytes - a_used_bytes );
}
