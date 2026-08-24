/**
    surveillance_history.h : Retained RFScout surveillance detections
    Description: Tracks recently detected surveillance devices beyond the live RF window.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SURVEILLANCE_HISTORY_H_
#define _RFSCOUT_SURVEILLANCE_HISTORY_H_

#include "observation_store.h"

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_SURVEILLANCE_HISTORY_CAPACITY 64U
#define RFSCOUT_SURVEILLANCE_TIMEOUT_MULTIPLIER 3U
#define RFSCOUT_SURVEILLANCE_TIMEOUT_MS \
    ( RFSCOUT_DEVICE_TIMEOUT_MS * RFSCOUT_SURVEILLANCE_TIMEOUT_MULTIPLIER )
#define RFSCOUT_SURVEILLANCE_CLASS_COUNT \
    ( (size_t)SURVEILLANCE_OTHER + 1U )

esp_err_t surveillance_history_init( void );
bool surveillance_history_observe( const rf_device_t *a_observation );
size_t surveillance_history_counts( uint16_t *a_counts, size_t a_capacity );

/** @brief Count retained unique devices by specific type/brand signature. */
size_t surveillance_history_signature_counts( uint16_t *a_counts,
                                              size_t a_capacity );

#endif // _RFSCOUT_SURVEILLANCE_HISTORY_H_
