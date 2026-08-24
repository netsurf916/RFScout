/**
    cc1101_activity.h : CC1101 direct RSSI levels, peak hold, and passive fingerprints
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_CC1101_ACTIVITY_H_
#define _RFSCOUT_CC1101_ACTIVITY_H_

#include "cc1101.h"

#include <stddef.h>
#include <stdint.h>

#define CC1101_FINGERPRINT_MAX 5U
#define CC1101_FINGERPRINT_LABEL_MAX 24U

typedef enum
{
    CC1101_FP_UNKNOWN = 0,
    CC1101_FP_NARROW_ISM,
    CC1101_FP_REMOTE_SENSOR_LIKE,
    CC1101_FP_CONTINUOUS_CARRIER,
    CC1101_FP_BURSTY_SIGNAL,
    CC1101_FP_WIDEBAND_ACTIVITY
} cc1101_fingerprint_type_t;

typedef struct
{
    cc1101_fingerprint_type_t type;
    uint8_t confidence;
    uint32_t center_khz;
    uint32_t bandwidth_khz;
    int8_t peak_rssi;
    char label[ CC1101_FINGERPRINT_LABEL_MAX ];
} cc1101_fingerprint_t;

void cc1101_activity_reset( void );
void cc1101_activity_observe_sweep( const int8_t *a_rssi,
                                    size_t a_count,
                                    uint32_t a_low_khz,
                                    uint32_t a_high_khz );
/** @brief Copy current direct RSSI levels plus slow-decay peak-hold levels. */
size_t cc1101_activity_levels( uint8_t *a_current,
                               uint8_t *a_peak,
                               size_t a_capacity );
size_t cc1101_activity_fingerprints( cc1101_fingerprint_t *a_output,
                                     size_t a_capacity );
const char *cc1101_activity_fingerprint_name( cc1101_fingerprint_type_t a_type );

#endif
