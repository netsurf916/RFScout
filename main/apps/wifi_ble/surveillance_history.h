/**
    surveillance_history.h : Append-only receiver observation journal
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once

#include "rf_observation.h"

/** @brief Reset pending journal state on app entry. */
void surveillance_history_open( void );

/** @brief Capture one GPS-qualified packet observation without SD I/O. */
void surveillance_history_record( const rf_device_t *a_device,
                                  double a_latitude,
                                  double a_longitude,
                                  int64_t a_utc_epoch );

/** @brief Append pending packet observations from the app poll, never from radio callbacks. */
void surveillance_history_flush( void );
