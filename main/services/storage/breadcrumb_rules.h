/**
    breadcrumb_rules.h : Stateless UTC filename, retention and velocity sampling rules
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_BREADCRUMB_RULES_H_
#define _RFSCOUT_BREADCRUMB_RULES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/** @brief Use the record's GPS UTC date, not the recorder's current wall clock. */
static inline bool breadcrumb_daily_filename( time_t a_epoch, char *a_name, size_t a_capacity )
{
    if( NULL == a_name || 0U == a_capacity )
    {
        return false;
    }
    struct tm utc = { 0 };
    if( NULL == gmtime_r( &a_epoch, &utc ) )
    {
        return false;
    }
    const int length = snprintf( a_name,
                                 a_capacity,
                                 "breadcrumbs-%04d-%02d-%02d.jsonl",
                                 utc.tm_year + 1900,
                                 utc.tm_mon + 1,
                                 utc.tm_mday );
    return length > 0 && (size_t)length < a_capacity;
}

/** @brief Interpolated 60-to-1-second minimum capture cadence, fastest by 10 m/s. */
static inline int64_t breadcrumb_capture_interval_us( float a_speed_mps )
{
    static const float speeds[] = { 0.0f, 0.5f, 1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 10.0f };
    static const float seconds[] = { 60.0f, 45.0f, 30.0f, 12.0f, 6.0f, 3.0f, 2.0f, 1.0f };
    if( !( a_speed_mps >= 0.0f ) )
    {
        a_speed_mps = 0.0f;
    }
    for( size_t index = 1U; index < sizeof( speeds ) / sizeof( speeds[ 0 ] ); ++index )
    {
        if( a_speed_mps < speeds[ index ] )
        {
            const float part =
                ( a_speed_mps - speeds[ index - 1U ] ) / ( speeds[ index ] - speeds[ index - 1U ] );
            const float interval =
                seconds[ index - 1U ] + part * ( seconds[ index ] - seconds[ index - 1U ] );
            return (int64_t)( interval * 1000000.0f );
        }
    }
    return 1000000LL;
}

/**
 * @brief Capture only after the speed interval and a movement event or 60-second heartbeat.
 * @details a_moved is calculated against a copy of the last queued movement gate.
 */
static inline bool
breadcrumb_capture_due( int64_t a_now_us, int64_t a_last_us, float a_speed_mps, bool a_moved )
{
    if( 0 == a_last_us )
    {
        return true;
    }
    if( a_now_us < a_last_us )
    {
        return false;
    }
    const int64_t elapsed = a_now_us - a_last_us;
    return elapsed >= breadcrumb_capture_interval_us( a_speed_mps ) &&
           ( a_moved || elapsed >= 60000000LL );
}

/** @brief Only date-formatted files are subject to automatic retention. */
static inline bool breadcrumb_filename_date_valid( const char *a_name )
{
    if( NULL == a_name || strlen( a_name ) != sizeof( "breadcrumbs-2026-10-25.jsonl" ) - 1U ||
        0 != strncmp( a_name, "breadcrumbs-", 12U ) || a_name[ 16 ] != '-' || a_name[ 19 ] != '-' ||
        0 != strcmp( &a_name[ 22 ], ".jsonl" ) )
    {
        return false;
    }
    static const unsigned positions[] = { 12U, 13U, 14U, 15U, 17U, 18U, 20U, 21U };
    for( size_t index = 0U; index < sizeof( positions ) / sizeof( positions[ 0 ] ); ++index )
    {
        const char digit = a_name[ positions[ index ] ];
        if( digit < '0' || digit > '9' )
        {
            return false;
        }
    }
    const unsigned year = (unsigned)( a_name[ 12 ] - '0' ) * 1000U +
                          (unsigned)( a_name[ 13 ] - '0' ) * 100U +
                          (unsigned)( a_name[ 14 ] - '0' ) * 10U + (unsigned)( a_name[ 15 ] - '0' );
    const unsigned month =
        (unsigned)( a_name[ 17 ] - '0' ) * 10U + (unsigned)( a_name[ 18 ] - '0' );
    const unsigned day = (unsigned)( a_name[ 20 ] - '0' ) * 10U + (unsigned)( a_name[ 21 ] - '0' );
    if( year < 1970U || month < 1U || month > 12U )
    {
        return false;
    }
    static const unsigned days[] = { 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U };
    const unsigned max_day = days[ month - 1U ] + ( month == 2U && year % 4U == 0U &&
                                                    ( year % 100U != 0U || year % 400U == 0U ) );
    return day >= 1U && day <= max_day;
}

/**
 * @brief Eligible only if the file's complete UTC date precedes the cutoff date.
 * @details Keeps the cutoff day, even if only a part of that day remains in policy.
 *          Future, unknown or legacy names are never deleted.
 */
static inline bool breadcrumb_filename_expired( const char *a_name, time_t a_cutoff )
{
    char cutoff[ 64 ];
    return breadcrumb_filename_date_valid( a_name ) &&
           breadcrumb_daily_filename( a_cutoff, cutoff, sizeof( cutoff ) ) &&
           strcmp( a_name, cutoff ) < 0;
}

#endif // _RFSCOUT_BREADCRUMB_RULES_H_
