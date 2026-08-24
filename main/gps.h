/**
    gps.h : RFScout UART GPS interface
    Description: Detects an attached NMEA GPS receiver and tracks fix state.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_GPS_H_
#define _RFSCOUT_GPS_H_

#include "esp_err.h"

#include <stdbool.h>

/** @brief Cached GPS receiver state exposed to the rest of RFScout. */
typedef struct
{
    bool detected;
    bool has_fix;
} gps_status_t;

/**
 * @brief Initialize the expansion UART used by an optional GPS receiver.
 * @return ESP_OK when the UART is available or the underlying driver error.
 * @note A missing GPS receiver is not an initialization error.
 */
esp_err_t gps_init( void );

/** @brief Poll the UART and update receiver/fix state from NMEA sentences. */
void gps_poll( void );

/**
 * @brief Copy the current GPS receiver state.
 * @param a_status Destination for the cached state.
 */
void gps_get_status( gps_status_t *a_status );

#endif // _RFSCOUT_GPS_H_
