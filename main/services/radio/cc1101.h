/**
    cc1101.h : RFScout CC1101 receive-only radio service
    Description: Declares discovery scanning, fixed-frequency capture, and bounded capture leases.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_CC1101_H_
#define _RFSCOUT_CC1101_H_

#include "subghz_capture.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CC1101_SPECTRUM_BINS 128U
#define CC1101_MAX_SWEEP_SAMPLES 128U

/** @brief High-level acquisition role of the CC1101 service. */
typedef enum
{
    CC1101_ACQUISITION_DISCOVERY = 0,
    CC1101_ACQUISITION_RECEIVE,
} cc1101_acquisition_mode_t;

/** @brief Preset and manual CC1101 discovery scan profiles. */
typedef enum
{
    CC1101_PROFILE_433 = 0,
    CC1101_PROFILE_315,
    CC1101_PROFILE_868,
    CC1101_PROFILE_915,
    CC1101_PROFILE_MANUAL,
    CC1101_PROFILE_COUNT
} cc1101_profile_t;

/** @brief Active CC1101 discovery sweep configuration. */
typedef struct
{
    cc1101_profile_t profile;
    uint32_t low_khz;
    uint32_t high_khz;
    uint32_t step_khz;
} cc1101_scan_config_t;

/** @brief Fixed-frequency asynchronous receive presets. */
typedef enum
{
    CC1101_RECEIVE_OOK_650 = 0,
    CC1101_RECEIVE_OOK_270,
    CC1101_RECEIVE_PRESET_COUNT
} cc1101_receive_preset_t;

/** @brief Fixed-frequency receive configuration. */
typedef struct
{
    uint32_t frequency_khz;
    cc1101_receive_preset_t preset;
} cc1101_receive_config_t;

/** @brief Capture-pool/service diagnostics copied atomically for UI/logging. */
typedef struct
{
    uint32_t captures_completed;
    uint32_t captures_truncated;
    uint32_t capture_overruns;
    uint32_t capture_errors;
} cc1101_capture_stats_t;

/** @brief Probe the CC1101 identity and leave the radio inactive. Safe to retry. */
esp_err_t cc1101_probe( void );

/** @brief Initialize reusable CC1101 service state after a successful probe. */
esp_err_t cc1101_init( void );

/** @brief Activate the already initialized CC1101 in the selected acquisition mode. */
esp_err_t cc1101_start( void );

/** @brief Quiesce discovery/RMT acquisition, release capture resources, and power down the radio. */
bool cc1101_stop( void );

/** @brief Return whether the CC1101 platform service is currently active. */
bool cc1101_is_active( void );

/** @brief Service foreground bookkeeping, including stalled-buffer rearm. */
void cc1101_poll( void );

/** @brief Return true when a valid CC1101 part/version probe succeeded. */
bool cc1101_is_detected( void );

/** @brief Return the selected discovery/receive role. */
cc1101_acquisition_mode_t cc1101_acquisition_mode( void );

/** @brief Switch between discovery scanning and fixed-frequency capture. */
esp_err_t cc1101_set_acquisition_mode( cc1101_acquisition_mode_t a_mode );

/** @brief Copy the current discovery configuration. */
void cc1101_scan_config( cc1101_scan_config_t *a_output );

/** @brief Apply a preset discovery scan profile or select manual configuration. */
void cc1101_set_profile( cc1101_profile_t a_profile );

/** @brief Adjust the lower discovery bound by encoder delta. */
bool cc1101_adjust_low( int a_delta );

/** @brief Adjust the upper discovery bound by encoder delta. */
bool cc1101_adjust_high( int a_delta );

/** @brief Adjust the discovery step using the supported step table. */
bool cc1101_adjust_step( int a_delta );

/** @brief Return the number of RF samples represented by the current discovery sweep. */
size_t cc1101_sweep_count( void );

/** @brief Return a stable display name for a discovery profile. */
const char *cc1101_profile_name( cc1101_profile_t a_profile );

/**
 * @brief Copy the most recently completed discovery spectrum frame.
 * @param a_output Destination for signed RSSI values.
 * @param a_frequency_khz Destination for the exact sampled frequency represented by each bin.
 * @param a_capacity Destination capacity in bins.
 * @return Number of bins copied.
 */
size_t cc1101_spectrum( int8_t *a_output, uint32_t *a_frequency_khz, size_t a_capacity );

/** @brief Consume the notification that a complete discovery sweep is ready. */
bool cc1101_take_frame_ready( void );

/** @brief Copy the current fixed-frequency receive configuration. */
void cc1101_receive_config( cc1101_receive_config_t *a_output );

/** @brief Select the fixed receive frequency. */
esp_err_t cc1101_set_receive_frequency( uint32_t a_frequency_khz );

/** @brief Select an asynchronous receive preset. */
esp_err_t cc1101_set_receive_preset( cc1101_receive_preset_t a_preset );

/** @brief Return a stable display name for an asynchronous receive preset. */
const char *cc1101_receive_preset_name( cc1101_receive_preset_t a_preset );

/**
 * @brief Lease the oldest completed raw capture for read-only processing.
 * @param a_capture Destination capture descriptor. Its symbols pointer is borrowed.
 * @param a_lease Destination opaque pool slot id required by cc1101_capture_release().
 * @return true when a READY capture was moved to PROCESSING.
 */
bool cc1101_capture_acquire( rfscout_subghz_capture_t *a_capture, uint8_t *a_lease );

/** @brief Release a PROCESSING capture lease so its DMA pool entry can be reused. */
void cc1101_capture_release( uint8_t a_lease );

/** @brief Copy capture-pool diagnostics. */
void cc1101_capture_stats( cc1101_capture_stats_t *a_output );

#endif // _RFSCOUT_CC1101_H_
