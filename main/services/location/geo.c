/**
    geo.c : Shared geographic helpers
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "geo.h"

#include <math.h>
#include <stddef.h>

#define GEO_EARTH_RADIUS_M 6371000.0
#define GEO_RADIANS_PER_DEGREE 0.017453292519943295

double
geo_distance_m( double a_latitude1, double a_longitude1, double a_latitude2, double a_longitude2 )
{
    const double lat1 = a_latitude1 * GEO_RADIANS_PER_DEGREE;
    const double lat2 = a_latitude2 * GEO_RADIANS_PER_DEGREE;
    const double half_lat = ( lat2 - lat1 ) / 2.0;
    const double half_lon = ( a_longitude2 - a_longitude1 ) * GEO_RADIANS_PER_DEGREE / 2.0;
    const double haversine = sin( half_lat ) * sin( half_lat ) +
                             cos( lat1 ) * cos( lat2 ) * sin( half_lon ) * sin( half_lon );
    return 2.0 * GEO_EARTH_RADIUS_M * asin( sqrt( fmin( 1.0, fmax( 0.0, haversine ) ) ) );
}

/** Degrees travelled eastward from a_from to a_to, in [0, 360). */
static double eastward_degrees( double a_from, double a_to )
{
    double delta = a_to - a_from;
    if( delta < 0.0 )
    {
        delta += 360.0;
    }
    return delta;
}

static bool longitude_inside( const geo_bounds_t *a_bounds, double a_longitude )
{
    return eastward_degrees( a_bounds->min_longitude, a_longitude ) <=
           eastward_degrees( a_bounds->min_longitude, a_bounds->max_longitude );
}

static void extend_longitude( geo_bounds_t *a_bounds, double a_longitude )
{
    if( longitude_inside( a_bounds, a_longitude ) )
    {
        return;
    }
    if( eastward_degrees( a_bounds->max_longitude, a_longitude ) <=
        eastward_degrees( a_longitude, a_bounds->min_longitude ) )
    {
        a_bounds->max_longitude = a_longitude;
    }
    else
    {
        a_bounds->min_longitude = a_longitude;
    }
}

void geo_bounds_extend( geo_bounds_t *a_bounds, double a_latitude, double a_longitude )
{
    if( NULL == a_bounds )
    {
        return;
    }
    if( !a_bounds->valid )
    {
        *a_bounds = ( geo_bounds_t ){
            .valid = true,
            .min_latitude = a_latitude,
            .max_latitude = a_latitude,
            .min_longitude = a_longitude,
            .max_longitude = a_longitude,
            .samples = 1U,
        };
        return;
    }
    a_bounds->min_latitude = fmin( a_bounds->min_latitude, a_latitude );
    a_bounds->max_latitude = fmax( a_bounds->max_latitude, a_latitude );
    extend_longitude( a_bounds, a_longitude );
    ++a_bounds->samples;
}

void geo_bounds_merge( geo_bounds_t *a_bounds, const geo_bounds_t *a_other )
{
    if( NULL == a_bounds || NULL == a_other || !a_other->valid )
    {
        return;
    }
    if( !a_bounds->valid )
    {
        *a_bounds = *a_other;
        return;
    }
    a_bounds->min_latitude = fmin( a_bounds->min_latitude, a_other->min_latitude );
    a_bounds->max_latitude = fmax( a_bounds->max_latitude, a_other->max_latitude );
    extend_longitude( a_bounds, a_other->min_longitude );
    extend_longitude( a_bounds, a_other->max_longitude );
    a_bounds->samples += a_other->samples;
}

bool geo_bounds_center( const geo_bounds_t *a_bounds, double *a_latitude, double *a_longitude )
{
    if( NULL == a_bounds || !a_bounds->valid || NULL == a_latitude || NULL == a_longitude )
    {
        return false;
    }
    *a_latitude = ( a_bounds->min_latitude + a_bounds->max_latitude ) / 2.0;
    double longitude = a_bounds->min_longitude +
                       eastward_degrees( a_bounds->min_longitude, a_bounds->max_longitude ) / 2.0;
    if( longitude > 180.0 )
    {
        longitude -= 360.0;
    }
    *a_longitude = longitude;
    return true;
}

double geo_bounds_distance_m( const geo_bounds_t *a_bounds, double a_latitude, double a_longitude )
{
    if( NULL == a_bounds || !a_bounds->valid )
    {
        return -1.0;
    }
    const double latitude =
        fmin( fmax( a_latitude, a_bounds->min_latitude ), a_bounds->max_latitude );
    double longitude = a_longitude;
    if( !longitude_inside( a_bounds, a_longitude ) )
    {
        longitude = eastward_degrees( a_longitude, a_bounds->min_longitude ) <=
                            eastward_degrees( a_bounds->max_longitude, a_longitude )
                        ? a_bounds->min_longitude
                        : a_bounds->max_longitude;
    }
    return geo_distance_m( a_latitude, a_longitude, latitude, longitude );
}
