/**
    cc1101_activity.h : CC1101 direct RSSI levels and passive fingerprints
    Description: Declares spectrum history and conservative sub-GHz likely-device classification.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_CC1101_ACTIVITY_H_
#define _RFSCOUT_CC1101_ACTIVITY_H_

#include "cc1101.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CC1101_FINGERPRINT_MAX 5U
#define CC1101_FINGERPRINT_LABEL_MAX 24U

/** @brief Conservative CC1101 signal-shape fingerprint categories. */
typedef enum
{
    CC1101_FP_UNKNOWN = 0,
    CC1101_FP_NARROW_ISM,
    CC1101_FP_REMOTE_SENSOR_LIKE,
    CC1101_FP_CONTINUOUS_CARRIER,
    CC1101_FP_BURSTY_SIGNAL,
    CC1101_FP_WIDEBAND_ACTIVITY,
    CC1101_FP_TYPE_COUNT
} cc1101_fingerprint_type_t;

/** @brief Accumulated evidence for one CC1101 likely-device category. */
typedef struct
{
    cc1101_fingerprint_type_t type;
    uint8_t confidence;
    uint32_t center_khz;
    uint32_t bandwidth_khz;
    int8_t peak_rssi;
    uint16_t count;
    bool active;
    char label[ CC1101_FINGERPRINT_LABEL_MAX ];
} cc1101_fingerprint_t;

/** @brief Completed sweep-derived CC1101 activity observation retained for this app session. */
typedef struct
{
    cc1101_fingerprint_type_t type;
    uint8_t confidence;
    uint32_t center_khz;
    uint32_t bandwidth_khz;
    int8_t peak_rssi;
    uint32_t started_ms;
    uint32_t ended_ms;
    uint32_t duration_ms;
    uint32_t gap_ms;
} cc1101_activity_observation_t;

/** @brief Clear spectrum history, classifier state, and counts. */
void cc1101_activity_reset( void );

/** @brief Clear accumulated likely-device counts without clearing spectrum history. */
void cc1101_activity_reset_counts( void );

/** @brief End active observations before an interval in which sweeps will not be consumed. */
void cc1101_activity_pause( void );

/**
 * @brief Add one completed RSSI sweep to the activity model.
 * @param a_rssi Per-bin signed RSSI values.
 * @param a_count Number of values in a_rssi.
 * @param a_low_khz Lower edge of the scanned range in kHz.
 * @param a_high_khz Upper edge of the scanned range in kHz.
 */
void cc1101_activity_observe_sweep( const int8_t *a_rssi,
                                    size_t a_count,
                                    uint32_t a_low_khz,
                                    uint32_t a_high_khz );

/**
 * @brief Copy current direct RSSI levels.
 * @param a_current Destination for current normalized signal values.
 * @param a_capacity Destination array capacity.
 * @return Number of values copied.
 */
size_t cc1101_activity_levels( uint8_t *a_current, size_t a_capacity );

/**
 * @brief Copy the fixed likely-device rows and their cumulative counts.
 * @param a_output Destination fingerprint array.
 * @param a_capacity Destination capacity.
 * @return Number of fingerprint rows copied.
 */
size_t cc1101_activity_fingerprints( cc1101_fingerprint_t *a_output, size_t a_capacity );

/**
 * @brief Copy newest completed activity observations first.
 * @param a_output Destination observation array.
 * @param a_capacity Destination capacity.
 * @return Number of observations copied.
 */
size_t cc1101_activity_history( cc1101_activity_observation_t *a_output, size_t a_capacity );

/** @brief Return the number of completed observations retained in app-session PSRAM. */
size_t cc1101_activity_history_count( void );

/** @brief Return the number of completed observations dropped because history growth failed. */
uint32_t cc1101_activity_history_dropped( void );

/** @brief Return a stable display name for a CC1101 fingerprint category. */
const char *cc1101_activity_fingerprint_name( cc1101_fingerprint_type_t a_type );

#endif // _RFSCOUT_CC1101_ACTIVITY_H_
