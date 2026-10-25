/**
    hotspot_sightings.h : Bounded Hotspots observation journal
    Description: Durable, unvalidated sightings independent of Live and characterized History.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_HOTSPOT_SIGHTINGS_H_
#define _RFSCOUT_HOTSPOT_SIGHTINGS_H_

#include "observation_metadata.h"
#include "wifi_station.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Start an app-session sightings journal and a bounded per-visit de-duplication cache. */
esp_err_t hotspot_sightings_open( void );

/**
 * @brief Record a qualifying sighting before Live admission, if first this visit or moved.
 * @details a_eligible is the application's security/profile/history decision. Unqualified
 *          observations never enter the journal. No network association is implied.
 */
void hotspot_sightings_observe( const rfscout_wifi_station_bss_t *a_bss,
                                const rfscout_observation_metadata_t *a_metadata,
                                bool a_eligible );

/** @brief Flush queued records, at most once each second unless the buffer is full. */
void hotspot_sightings_poll( int64_t a_now_us );

/** @brief Flush remaining records and release app-session pointers. */
void hotspot_sightings_close( void );

#endif // _RFSCOUT_HOTSPOT_SIGHTINGS_H_
