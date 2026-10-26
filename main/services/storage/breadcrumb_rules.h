/**
    breadcrumb_rules.h : Stateless UTC filename and velocity cadence helpers
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
#include <time.h>

/** @brief Use the record's GPS UTC date, not the recorder's current wall clock. */
static inline bool
breadcrumb_daily_filename( time_t a_epoch, char *a_name, size_t a_capacity )
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

/** @brief Speed-dependent sampling capped at the GPS UTC clock's second precision. */
static inline int64_t breadcrumb_capture_interval_us( float a_speed_mph )
{
    if( a_speed_mph >= 15.0f )
    {
        return 1000000LL;
    }
    return a_speed_mph >= 3.0f ? 2000000LL : 5000000LL;
}

#endif // _RFSCOUT_BREADCRUMB_RULES_H_
