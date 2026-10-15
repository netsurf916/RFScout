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
    /** Per-sentence freshness: GGA altitude, RMC speed and GGA satellites used. */
    bool altitude_valid;
    bool speed_valid;
    /** True when fresh RMC course over ground is available (not compass orientation). */
    bool track_valid;
    float track_deg;
    bool satellites_valid;
    double latitude_deg;
    double longitude_deg;
    float altitude_m;
    float speed_mps;
    /** Distance accumulated from qualified RMC fixes since the last explicit reset. */
    double trip_distance_m;
    uint8_t satellites;
    time_t utc_epoch;
    uint32_t location_age_ms;
    uint32_t time_age_ms;
} rfscout_gps_snapshot_t;

/** @brief Initialize the runtime GPS provider when the active board advertises one. */
esp_err_t gps_init( void );
/** @brief Stop the GPS UART and release its pins for a mutually exclusive radio. */
esp_err_t gps_stop( void );
/** @brief True while checksum-valid RMC/GGA traffic is fresh; presence expires after inactivity. */
bool gps_detected( void );
/** @brief True after the current UART startup detection window finishes. */
bool gps_probe_complete( void );
/** @brief Drain and parse pending NMEA input; non-blocking. */
void gps_poll( void );
/** @brief Copy the latest detected-receiver state including explicit location/time validity. */
bool gps_snapshot_get( rfscout_gps_snapshot_t *a_snapshot );
/** @brief Return whether a GPS UART provider is active, independent of receiver detection/fix state. */
bool gps_available( void );
/** @brief Return the current trip odometer even when no receiver is present. */
double gps_trip_distance_m( void );
/** @brief Clear the trip odometer and persist the reset in internal flash. */
esp_err_t gps_trip_reset( void );
/** Apply the persisted receiver timepulse preference when CASIC is ready. */
void gps_timepulse_apply( void );

/** @brief Fresh filtered speed and current/target update intervals for diagnostic breadcrumbs.
 * @return false when filtered speed is unavailable or stale.
 */
bool gps_rate_diagnostics_get( float *a_filtered_speed_mps,
                               int *a_current_interval_ms,
                               int *a_target_interval_ms );

#endif // _RFSCOUT_GPS_H_
