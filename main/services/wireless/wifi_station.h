/**
    wifi_station.h : RFScout temporary infrastructure Wi-Fi service
    Description: Provides passive AP discovery and explicit app-requested station connections.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_WIFI_STATION_H_
#define _RFSCOUT_WIFI_STATION_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH 32U
#define RFSCOUT_WIFI_STATION_PASSWORD_MAX_LENGTH 64U
#define RFSCOUT_WIFI_STATION_SCAN_MAX_RESULTS 24U

typedef enum
{
    RFSCOUT_WIFI_STATION_OFF = 0,
    RFSCOUT_WIFI_STATION_IDLE,
    RFSCOUT_WIFI_STATION_SCANNING,
    RFSCOUT_WIFI_STATION_SEARCHING,
    RFSCOUT_WIFI_STATION_CONNECTING,
    RFSCOUT_WIFI_STATION_CONNECTED,
    RFSCOUT_WIFI_STATION_FAILED
} rfscout_wifi_station_state_t;

typedef struct
{
    char ssid[ RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U ];
    int8_t rssi;
    uint8_t channel;
    bool open;
} rfscout_wifi_station_ap_t;

typedef struct
{
    rfscout_wifi_station_state_t state;
    uint32_t scan_generation;
    int32_t disconnect_reason;
} rfscout_wifi_station_status_t;

/** @brief Attach the infrastructure station netif to the already initialized Wi-Fi driver. */
esp_err_t rfscout_wifi_station_init( void );

/**
 * @brief Start one all-channel passive AP scan without probe requests.
 * @details Starting the STA control block is a platform detail; this operation does not associate
 *          with an AP. Results replace the previous scan snapshot when the async scan completes.
 */
esp_err_t rfscout_wifi_station_passive_scan_start( void );

/** @brief Copy the current strongest-per-SSID passive scan snapshot. */
size_t rfscout_wifi_station_scan_results( rfscout_wifi_station_ap_t *a_results, size_t a_capacity );

/**
 * @brief Establish one explicitly requested station connection using caller-owned credentials.
 * @details If the target was absent from the latest passive scan snapshot, the service first
 *          performs an SSID-targeted active scan before association.
 */
esp_err_t rfscout_wifi_station_connect( const char *a_ssid, const char *a_password );

/** @brief Disconnect the current station while leaving the OTA session able to scan again. */
esp_err_t rfscout_wifi_station_disconnect( void );

/**
 * @brief Tear down all STA work and clear transient driver configuration/results.
 * @details Used by the app-session lifecycle when the owning application exits.
 */
esp_err_t rfscout_wifi_station_stop( void );

/** @brief Return whether the infrastructure Wi-Fi driver session is currently started. */
bool rfscout_wifi_station_is_active( void );

/** @brief Snapshot connection/scan state without exposing ESP-IDF driver objects. */
rfscout_wifi_station_status_t rfscout_wifi_station_status( void );

#endif // _RFSCOUT_WIFI_STATION_H_
