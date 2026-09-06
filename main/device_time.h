/**
    device_time.h : RFScout authoritative wall-clock service
    Description: Owns runtime wall time, timezone application, and formatting.
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

/** @brief Initialize timezone for the authoritative RTC-backed system wall clock. */
esp_err_t device_time_init( void );
/** @brief Set the running RTC-backed system wall clock to one UTC epoch. */
esp_err_t device_time_set_epoch( time_t a_epoch );
/** @brief Return the current RTC-backed system epoch. */
bool device_time_now( time_t *a_epoch );
/** @brief Apply the timezone currently selected in device configuration. */
esp_err_t device_time_apply_timezone( void );
/** @brief Format current local time for the shared status bar. */
bool device_time_format_clock( char *a_buffer, size_t a_capacity );
/** @brief Format an epoch as UTC ISO-8601 for logs. */
bool device_time_format_iso8601_utc( time_t a_epoch, char *a_buffer, size_t a_capacity );
/** @brief Compatibility helper that only flushes dirty preferences; it does not checkpoint time. */
esp_err_t device_time_checkpoint( void );

#endif // _RFSCOUT_DEVICE_TIME_H_
