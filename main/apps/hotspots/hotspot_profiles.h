/**
    apps/hotspots/hotspot_profiles.h : Hotspots saved-profile selection
    Description: Decides which saved profile, if any, automatic checks may use for a secured BSSID
                 from the profile snapshot, the live table, history bounds and a GPS fix. Pure
                 model; no radio, storage or UI access.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_HOTSPOT_PROFILES_H_
#define _RFSCOUT_HOTSPOT_PROFILES_H_

#include "geo.h"
#include "gps.h"
#include "hotspot_table.h"
#include "saved_networks.h"

#include <stddef.h>
#include <stdint.h>

/** @brief History bounds recorded for a BSSID, or NULL when it has no history. */
typedef const geo_bounds_t *( *hotspot_profiles_bounds_fn )( const uint8_t a_bssid[ 6 ] );

/** Everything one resolution pass reads; all pointers are borrowed for the call. */
typedef struct
{
    const saved_network_profile_t *profiles;
    size_t profile_count;
    const hotspot_table_t *table; /**< latest completed scan: same-scan overlap evidence */
    hotspot_profiles_bounds_fn history_bounds;
    const rfscout_gps_snapshot_t *gps;
    uint16_t match_distance_m; /**< history evidence radius; 0 is Off */
} hotspot_profiles_input_t;

/**
 * @brief Resolve which saved profile, if any, automatic checks may use for a secured BSSID.
 * @details An exact associated BSSID wins. Otherwise a same-SSID, auth-compatible profile that
 *          this BSSID has not rejected qualifies only with evidence: one of its known BSSIDs is
 *          in the same scan, or has history within the match distance of a fresh fix. Several
 *          qualifying profiles are ambiguous and need the user's choice.
 * @param a_profile_id Set to the profile for KNOWN and INFERRED; 0 otherwise.
 * @return KNOWN, INFERRED, AMBIGUOUS, UNMATCHED, REJECTED or NONE; never OPEN or UNSUPPORTED.
 */
hotspot_credential_t hotspot_profiles_resolve( const hotspot_profiles_input_t *a_input,
                                               const hotspot_entry_t *a_entry,
                                               uint32_t *a_profile_id );

#endif // _RFSCOUT_HOTSPOT_PROFILES_H_
