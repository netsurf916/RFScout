/**
    subghz_decode.h : RFScout bounded Sub-GHz analysis and protocol decode framework
    Description: Declares compact derived results produced from immutable native timing captures.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SUBGHZ_DECODE_H_
#define _RFSCOUT_SUBGHZ_DECODE_H_

#include "subghz_capture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_SUBGHZ_TIMING_BUCKETS 8U
#define RFSCOUT_SUBGHZ_PROTOCOL_NAME_MAX 24U

typedef struct
{
    uint32_t duration_us;
    uint16_t high_count;
    uint16_t low_count;
} rfscout_subghz_timing_bucket_t;

typedef enum
{
    RFSCOUT_SUBGHZ_DECODE_NONE = 0,
    RFSCOUT_SUBGHZ_DECODE_MATCH,
} rfscout_subghz_decode_status_t;

typedef struct
{
    rfscout_subghz_decode_status_t status;
    char protocol[ RFSCOUT_SUBGHZ_PROTOCOL_NAME_MAX ];
    uint16_t bit_count;
    uint64_t data;
    uint32_t symbol_period_us;
    uint16_t repeats;
} rfscout_subghz_protocol_result_t;

/** @brief Compact analysis retained after the raw capture lease is released. */
typedef struct
{
    uint32_t sequence;
    uint32_t frequency_hz;
    uint32_t resolution_hz;
    size_t symbol_count;
    size_t timed_level_count;
    uint32_t total_duration_us;
    uint32_t min_duration_us;
    uint32_t max_duration_us;
    int8_t peak_rssi_dbm;
    bool truncated;
    rfscout_subghz_timing_bucket_t timing[ RFSCOUT_SUBGHZ_TIMING_BUCKETS ];
    size_t timing_count;
    rfscout_subghz_protocol_result_t protocol;
} rfscout_subghz_analysis_t;

/** @brief Analyze one immutable raw capture and invoke the registered protocol decoders. */
void rfscout_subghz_decode( const rfscout_subghz_capture_t *a_capture,
                            rfscout_subghz_analysis_t *a_output );

#endif // _RFSCOUT_SUBGHZ_DECODE_H_
