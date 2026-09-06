/**
    device_time.h : RFScout authoritative wall-clock service
    Description: Owns runtime wall time, timezone application, source diagnostics, and formatting.
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
    RFSCOUT_TIME_SOURCE_RTC = 0,
    RFSCOUT_TIME_SOURCE_MANUAL,
    RFSCOUT_TIME_SOURCE_GPS,
    RFSCOUT_TIME_SOURCE_NTP
} rfscout_time_source_t;

/** @brief Initialize timezone and treat the current system/RTC wall clock as authoritative. */
esp_err_t device_time_init( void );

/**
 * @brief Set runtime wall time from a supplied epoch and source.
 * @param a_epoch UTC epoch seconds.
 * @param a_source Source that supplied the adjustment.
 * @param a_detail Optional source detail, such as `rtc:ds3231`.
 */
esp_err_t
device_time_set_epoch( time_t a_epoch, rfscout_time_source_t a_source, const char *a_detail );

/** @brief Return the current RTC-backed system epoch. */
bool device_time_now( time_t *a_epoch );

/** @brief Return the source most recently used to adjust wall time during this boot. */
rfscout_time_source_t device_time_source( void );

/** @brief Return transient source detail for the current boot. */
const char *device_time_source_detail( void );

/** @brief Return a display label for the current boot's most recent time source. */
const char *device_time_source_label( void );

/** @brief Apply the timezone currently selected in device configuration. */
esp_err_t device_time_apply_timezone( void );

/** @brief Format current local time for the shared status bar. */
bool device_time_format_clock( char *a_buffer, size_t a_capacity );

/** @brief Format an epoch as UTC ISO-8601 for logs. */
bool device_time_format_iso8601_utc( time_t a_epoch, char *a_buffer, size_t a_capacity );

/** @brief Flush dirty user preferences before an orderly hard shutdown. */
esp_err_t device_time_checkpoint( void );

#endif // _RFSCOUT_DEVICE_TIME_H_
