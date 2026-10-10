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
    /** True after a checksum-valid supported NMEA sentence proves the receiver is responding. */
    bool available;
    /** True only while a recently received RMC/GGA position fix remains fresh. */
    bool location_valid;
    /** True only while a recently received fix-valid UTC timestamp remains fresh. */
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
/** @brief Stop the GPS UART and release its pins for a mutually exclusive radio. */
esp_err_t gps_stop( void );
/** @brief True once checksum-valid RMC/GGA has identified a receiver this boot. */
bool gps_detected( void );
/** @brief True after the initial two full UART baud-rate detection cycles. */
bool gps_probe_complete( void );
/** @brief Drain and parse pending NMEA input; non-blocking. */
void gps_poll( void );
/** @brief Copy the latest detected-receiver state including explicit location/time validity. */
bool gps_snapshot_get( rfscout_gps_snapshot_t *a_snapshot );
/** @brief Return whether a GPS UART provider is active, independent of receiver detection/fix state. */
bool gps_available( void );

#endif // _RFSCOUT_GPS_H_
