/**
    device_time.h : RFScout authoritative wall-clock service
    Description: Owns runtime UTC wall time, source validity, synchronization policy, and timezone formatting.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DEVICE_TIME_H_
#define _RFSCOUT_DEVICE_TIME_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef enum
{
    RFSCOUT_TIME_SOURCE_INVALID = 0,
    RFSCOUT_TIME_SOURCE_RETAINED,
    RFSCOUT_TIME_SOURCE_MANUAL,
    RFSCOUT_TIME_SOURCE_GPS,
    RFSCOUT_TIME_SOURCE_NTP,
} rfscout_time_source_t;

typedef enum
{
    RFSCOUT_TIME_MODE_AUTO = 0,
    RFSCOUT_TIME_MODE_MANUAL,
} rfscout_time_mode_t;

typedef struct
{
    bool valid;
    rfscout_time_source_t source;
    rfscout_time_mode_t mode;
    time_t epoch;
} rfscout_time_status_t;

/** @brief Initialize timezone, synchronization policy, and wall-clock validity. */
esp_err_t device_time_init( void );
/** @brief Set the wall clock manually; this also selects and persists Manual mode. */
esp_err_t device_time_set_epoch( time_t a_epoch );
/** @brief Offer a fix-valid GPS UTC epoch; accepted only while Auto mode is selected. */
esp_err_t device_time_offer_gps( time_t a_epoch );
/** @brief Apply an explicit network time synchronization without changing the user's time mode. */
esp_err_t device_time_offer_ntp( time_t a_epoch );
/** @brief Change and persist the automatic/manual synchronization policy. */
esp_err_t device_time_set_mode( rfscout_time_mode_t a_mode );
/** @brief Return the current synchronization policy. */
rfscout_time_mode_t device_time_mode( void );
/** @brief Return the current epoch only when RFScout considers wall time valid. */
bool device_time_now( time_t *a_epoch );
/** @brief Copy current wall-clock validity/source/policy state. */
void device_time_status( rfscout_time_status_t *a_status );
/** @brief Apply the timezone currently selected in device configuration. */
esp_err_t device_time_apply_timezone( void );
/** @brief Format current local time for the shared status bar when time is valid. */
bool device_time_format_clock( char *a_buffer, size_t a_capacity );
/** @brief Format an epoch as UTC ISO-8601 for logs. */
bool device_time_format_iso8601_utc( time_t a_epoch, char *a_buffer, size_t a_capacity );

#endif // _RFSCOUT_DEVICE_TIME_H_
