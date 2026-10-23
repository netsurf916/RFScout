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
#include <stddef.h>
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

/** @brief Last accepted point of a movement-triggered log; zero-initialized is empty. */
typedef struct
{
    bool valid;
    double latitude;
    double longitude;
} geo_movement_gate_t;

/** @brief Great-circle (haversine) distance between two points in metres. */
double
geo_distance_m( double a_latitude1, double a_longitude1, double a_latitude2, double a_longitude2 );

/** @brief Initial great-circle bearing from point 1 to point 2, clockwise from north [0, 360). */
double
geo_bearing_deg( double a_latitude1, double a_longitude1, double a_latitude2, double a_longitude2 );

/** @brief Eight-point compass sector of a bearing: 0 is N, 1 is NE, ... 7 is NW. */
unsigned geo_compass_sector( double a_degrees );

/** @brief Eight-point compass label ("N" ... "NW") of a bearing. */
const char *geo_compass_label( double a_degrees );

/** @brief Format a distance for small displays: "850m", "1.2km" or "12km". */
void geo_format_distance( double a_meters, char *a_output, size_t a_capacity );

/** @brief Grow the range to include one point, choosing the shorter longitude extension. */
void geo_bounds_extend( geo_bounds_t *a_bounds, double a_latitude, double a_longitude );

/** @brief Grow the range to include another range (for example a compacted summary). */
void geo_bounds_merge( geo_bounds_t *a_bounds, const geo_bounds_t *a_other );

/** @brief Midpoint of the range; false when empty. */
bool geo_bounds_center( const geo_bounds_t *a_bounds, double *a_latitude, double *a_longitude );

/** @brief Distance from a point to the nearest point of the range (0 inside); negative when empty. */
double geo_bounds_distance_m( const geo_bounds_t *a_bounds, double a_latitude, double a_longitude );

/**
 * @brief Accept a point when nothing was accepted yet or it is at least a_meters away.
 * @details An accepted point becomes the new reference. Callers apply this only to fresh
 *          GPS fixes, so every movement-triggered log shares one rule.
 */
bool geo_movement_gate_accept( geo_movement_gate_t *a_gate,
                               double a_latitude,
                               double a_longitude,
                               double a_meters );

#endif // _RFSCOUT_GEO_H_
