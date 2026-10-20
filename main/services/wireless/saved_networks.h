/**
    saved_networks.h : Shared access to RFScout's existing saved infrastructure networks
    Description: One credential store for Home/OTA and Hotspots; no additional persistence key.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_SAVED_NETWORKS_H_
#define _RFSCOUT_SAVED_NETWORKS_H_

#include "ota_networks.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Copy a saved credential for a known SSID into caller-owned memory.
 * @details Loads the same persistent "networks" record used by Home/OTA. An absent SSID
 *          returns ESP_ERR_NOT_FOUND; a storage error is propagated. The caller must erase
 *          the password buffer as soon as association ends. Never log its contents.
 */
/** Resolve only an already associated BSSID with the exact observed auth mode. */
esp_err_t saved_networks_password_for_bss( const uint8_t a_bssid[ 6 ],
                                           const char *a_ssid,
                                          int32_t a_auth_mode,
                                           char *a_password,
                                          size_t a_capacity );

/** Non-secret candidate identity; callers select a profile by stable ID. */
typedef struct
{
    uint32_t profile_id;
    int32_t auth_mode;
    bool known_bssid;
} saved_network_candidate_t;

/** List profiles sharing an SSID without exposing password material. */
esp_err_t saved_networks_candidates( const char *a_ssid,
                                     const uint8_t a_bssid[ 6 ],
                                     int32_t a_auth_mode,
                                     saved_network_candidate_t *a_results,
                                     size_t a_capacity,
                                     size_t *a_count );
/** Resolve a previously associated BSSID without loading any password. */
esp_err_t saved_networks_known_bss_profile( const char *a_ssid,
                                             const uint8_t a_bssid[ 6 ],
                                             int32_t a_auth_mode,
                                             uint32_t *a_profile_id );
/** Retrieve an explicitly chosen profile's password. */
esp_err_t saved_networks_password_for_profile( uint32_t a_profile_id,
                                                char *a_password,
                                               size_t a_capacity );

esp_err_t
saved_networks_password_for_ssid( const char *a_ssid, char *a_password, size_t a_capacity );

/**
 * @brief Transactionally save or replace a network using Home/OTA's existing record.
 * @details Writes storage before returning success; no independent Hotspots credential cache.
 *          This operation does not create a separate profile database.
 */
/** Explicitly create a separate profile; legacy SSID upsert stays supported. */
esp_err_t
saved_networks_create_profile( const char *a_ssid, const char *a_password, uint32_t *a_profile_id );
/** Associate an explicitly chosen profile after successful authentication. */
esp_err_t saved_networks_associate_profile_bssid( uint32_t a_profile_id,
                                                 const uint8_t a_bssid[ 6 ],
                                                 int32_t a_auth_mode );

esp_err_t saved_networks_save( const char *a_ssid, const char *a_password );

/** @brief Remove one SSID from the authoritative saved networks record. */
esp_err_t saved_networks_forget( const char *a_ssid );

#endif // _RFSCOUT_SAVED_NETWORKS_H_
