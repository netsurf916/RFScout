/**
    observation_metadata.h : Shared receive-time metadata for normalized observations
    Description: Adds optional trusted UTC and fresh receiver location away from radio callbacks.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_OBSERVATION_METADATA_H_
#define _RFSCOUT_OBSERVATION_METADATA_H_

#include "gps.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Capture time is monotonic; UTC/location may be unavailable. */
typedef struct
{
    uint32_t seen_ms;
    int64_t utc_epoch;
    bool location_valid;
    double latitude;
    double longitude;
} rfscout_observation_metadata_t;

/**
 * @brief Enrich an already timestamped receive event on an application task.
 * @details UTC comes from the trusted device clock, not an unvalidated NMEA sentence.
 *          Only a fresh GPS fix close to the received event is associated. Never call
 *          from a radio receive callback. Does not journal or make admission decisions.
 */
void observation_metadata_enrich( uint32_t a_seen_ms,
                                  const rfscout_gps_snapshot_t *a_gps,
                                  rfscout_observation_metadata_t *a_metadata );

#endif // _RFSCOUT_OBSERVATION_METADATA_H_
