/**
    apps/hotspots/hotspots_internal.h : Hotspots scheduler/screens boundary
    Description: What the Hotspots screens (hotspots_ui.c) read from and request of the scan and
                 check scheduler (app.c). Application task only.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_HOTSPOTS_INTERNAL_H_
#define _RFSCOUT_HOTSPOTS_INTERNAL_H_

#include "hotspot_table.h"
#include "network_credentials.h"
#include "saved_networks.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief The app-session table of observed BSSIDs. */
hotspot_table_t *hotspots_table( void );

/** @brief Stage of the running check when it targets this entry, else HOTSPOT_STAGE_NONE. */
hotspot_stage_t hotspots_attempt_stage( const hotspot_entry_t *a_entry );

/** @brief Scheduler phase, history state and network counts for a footer. */
void hotspots_format_status( char *a_footer, size_t a_capacity );

bool hotspots_auto_enabled( void );
void hotspots_set_auto_enabled( bool a_enabled );

/** @brief Distance within which a profile's known BSSIDs count as evidence; 0 is Off. */
uint16_t hotspots_match_distance_m( void );
/** @brief Step the match distance and save it; failures are returned, not hidden. */
esp_err_t hotspots_step_match_distance( int a_direction );

/** @brief Time without an observation before a Live BSSID expires (monitor time). */
uint16_t hotspots_retention_seconds( void );
/** @brief Logical uptime spent actively listening for APs (connection time excluded). */
uint32_t hotspots_monitor_clock_ms( void );
/** @brief Step the Live timeout and persist the change. */
esp_err_t hotspots_step_retention_seconds( int a_direction );
/** @brief Excess distance beyond Live estimated coverage; 0 disables geographic eviction. */
uint16_t hotspots_eviction_distance_m( void );
/** @brief Step the eviction distance in 100 m increments and persist the change. */
esp_err_t hotspots_step_eviction_distance( int a_direction );

/**
 * @brief Queue a manual check for a visible entry.
 * @param a_profile_id Profile chosen by the user for a secured entry; 0 uses the resolved one.
 * @return false when the entry cannot be checked now.
 */
bool hotspots_request_check( hotspot_entry_t *a_entry, uint32_t a_profile_id );

/** @brief Re-resolve credentials after the shared credential screens changed a profile. */
void hotspots_credentials_changed( const network_credentials_outcome_t *a_outcome );

/** @brief Non-secret snapshot of saved profiles taken after the latest scan or change. */
const saved_network_profile_t *hotspots_profiles( size_t *a_count );

#endif // _RFSCOUT_HOTSPOTS_INTERNAL_H_
