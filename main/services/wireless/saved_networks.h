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

/**
 * @brief Copy a saved credential for a known SSID into caller-owned memory.
 * @details Loads the same persistent "networks" record used by Home/OTA. An absent SSID
 *          returns ESP_ERR_NOT_FOUND; a storage error is propagated. The caller must erase
 *          the password buffer as soon as association ends. Never log its contents.
 */
esp_err_t
saved_networks_password_for_ssid( const char *a_ssid, char *a_password, size_t a_capacity );

/**
 * @brief Transactionally save or replace a network using Home/OTA's existing record.
 * @details Writes storage before returning success; no independent Hotspots credential cache.
 *          This operation does not create a separate profile database.
 */
esp_err_t saved_networks_save( const char *a_ssid, const char *a_password );

/** @brief Remove one SSID from the authoritative saved networks record. */
esp_err_t saved_networks_forget( const char *a_ssid );

#endif // _RFSCOUT_SAVED_NETWORKS_H_
