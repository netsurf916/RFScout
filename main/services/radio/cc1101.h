/**
    cc1101.h : RFScout CC1101 receive-only spectrum scanner
    Description: Declares sub-GHz scan configuration, lifecycle control, and completed spectrum access.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_CC1101_H_
#define _RFSCOUT_CC1101_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CC1101_SPECTRUM_BINS 128U
#define CC1101_MAX_SWEEP_SAMPLES 128U

/** @brief Preset and manual CC1101 scan profiles. */
typedef enum
{
    CC1101_PROFILE_433 = 0,
    CC1101_PROFILE_315,
    CC1101_PROFILE_868,
    CC1101_PROFILE_915,
    CC1101_PROFILE_MANUAL,
    CC1101_PROFILE_COUNT
} cc1101_profile_t;

/** @brief Active CC1101 sweep configuration. */
typedef struct
{
    cc1101_profile_t profile;
    uint32_t low_khz;
    uint32_t high_khz;
    uint32_t step_khz;
} cc1101_scan_config_t;

/** @brief Probe and initialize the CC1101 SPI device and worker task. */
esp_err_t cc1101_init( void );

/** @brief Start asynchronous receive-only CC1101 spectrum acquisition. */
esp_err_t cc1101_start( void );

/** @brief Stop acquisition, power down the CC1101, and disable its RF path. */
bool cc1101_stop( void );

/** @brief Reserved non-blocking service hook for CC1101 application polling. */
void cc1101_poll( void );

/** @brief Return true when a valid CC1101 part/version probe succeeded. */
bool cc1101_is_detected( void );

/** @brief Return the current scan configuration. */
const cc1101_scan_config_t *cc1101_config( void );

/** @brief Apply a preset scan profile or select manual configuration. */
void cc1101_set_profile( cc1101_profile_t a_profile );

/** @brief Adjust the lower scan bound by encoder delta. */
bool cc1101_adjust_low( int a_delta );

/** @brief Adjust the upper scan bound by encoder delta. */
bool cc1101_adjust_high( int a_delta );

/** @brief Adjust the scan step using the supported step table. */
bool cc1101_adjust_step( int a_delta );

/** @brief Return the number of RF samples represented by the current sweep. */
size_t cc1101_sweep_count( void );

/** @brief Return a stable display name for a CC1101 profile. */
const char *cc1101_profile_name( cc1101_profile_t a_profile );

/**
 * @brief Copy the most recently completed spectrum frame.
 * @param a_output Destination for signed RSSI values.
 * @param a_capacity Destination capacity in bins.
 * @return Number of bins copied.
 */
size_t cc1101_spectrum( int8_t *a_output, size_t a_capacity );

/** @brief Consume the notification that a complete sweep is ready to render. */
bool cc1101_take_frame_ready( void );

#endif // _RFSCOUT_CC1101_H_
