/**
    gps.h : RFScout GNSS location/time service
    Description: Exposes validated GPS location and UTC data without leaking board UART details to apps.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_GPS_H_
#define _RFSCOUT_GPS_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct
{
    bool available;
    bool location_valid;
    bool time_valid;
    double latitude_deg;
    double longitude_deg;
    float altitude_m;
    float speed_mps;
    uint8_t satellites;
    time_t utc_epoch;
    uint32_t location_age_ms;
    uint32_t time_age_ms;
} rfscout_gps_snapshot_t;

/** @brief Initialize the runtime GPS provider when the active board advertises one. */
esp_err_t gps_init( void );
/** @brief Drain and parse pending NMEA input; non-blocking. */
void gps_poll( void );
/** @brief Copy the latest GPS state including explicit location/time validity. */
bool gps_snapshot_get( rfscout_gps_snapshot_t *a_snapshot );
/** @brief Return whether a GPS provider is active on the selected board. */
bool gps_available( void );

#endif // _RFSCOUT_GPS_H_
