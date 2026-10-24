/**
    observation_retention.h : reusable observation-lifetime calculations
    Description: Shared arithmetic and geometry, not an application eviction policy engine.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_OBSERVATION_RETENTION_H_
#define _RFSCOUT_OBSERVATION_RETENTION_H_

#include "geo.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief True if the observation is outside its time window.
 * @param a_excluded_ms Deliberate suspension within the interval (e.g. TLS radio pause).
 * @details Uses unsigned millisecond arithmetic across the 32-bit uptime wrap.
 *          Callers must keep observed intervals below 2^31 ms and own source-level semantics.
 *          Zero timeout disables expiration.
 */
bool observation_retention_expired( uint32_t a_now_ms,
                                    uint32_t a_last_seen_ms,
                                    uint32_t a_timeout_ms,
                                    uint32_t a_excluded_ms );

/**
 * @brief True when the receiver is beyond the observed coverage circle plus a margin.
 * @details Caller establishes fresh GPS and whether the record is currently being observed.
 *          Zero margin, invalid receiver fix or empty bounds disable this criterion.
 */
bool observation_retention_outside_coverage( const geo_bounds_t *a_bounds,
                                             double a_receiver_latitude,
                                             double a_receiver_longitude,
                                             uint32_t a_margin_m,
                                             bool a_gps_fresh,
                                             bool a_currently_visible );

/** @brief Smooth a received RSSI with a fixed 1/4 weight on the latest reading. */
int8_t observation_retention_rssi_ema( int8_t a_previous, int8_t a_latest );

/**
 * @brief Whether admitting a new allocation exceeds a chosen byte budget.
 * @details Limits are app-owned goals. A true result asks the app to expire/evict and
 *          retry, not to grow beyond the budget. Zero budget means unlimited.
 */
bool observation_retention_over_budget( size_t a_used_bytes,
                                        size_t a_admission_bytes,
                                        size_t a_budget_bytes );

#endif // _RFSCOUT_OBSERVATION_RETENTION_H_
