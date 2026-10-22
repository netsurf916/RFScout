/**
    geo.h : Shared geographic helpers
    Description: Receiver-location bounding boxes and great-circle distances in degrees/metres.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_GEO_H_
#define _RFSCOUT_GEO_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Latitude/longitude range covering every sample added to it.
 * @details Longitude ranges may wrap the antimeridian (min_longitude > max_longitude).
 *          A zero-initialized value is empty.
 */
typedef struct
{
    bool valid;
    double min_latitude;
    double max_latitude;
    double min_longitude;
    double max_longitude;
    uint32_t samples;
} geo_bounds_t;

/** @brief Great-circle (haversine) distance between two points in metres. */
double
geo_distance_m( double a_latitude1, double a_longitude1, double a_latitude2, double a_longitude2 );

/** @brief Grow the range to include one point, choosing the shorter longitude extension. */
void geo_bounds_extend( geo_bounds_t *a_bounds, double a_latitude, double a_longitude );

/** @brief Grow the range to include another range (for example a compacted summary). */
void geo_bounds_merge( geo_bounds_t *a_bounds, const geo_bounds_t *a_other );

/** @brief Midpoint of the range; false when empty. */
bool geo_bounds_center( const geo_bounds_t *a_bounds, double *a_latitude, double *a_longitude );

/** @brief Distance from a point to the nearest point of the range (0 inside); negative when empty. */
double geo_bounds_distance_m( const geo_bounds_t *a_bounds, double a_latitude, double a_longitude );

#endif // _RFSCOUT_GEO_H_
