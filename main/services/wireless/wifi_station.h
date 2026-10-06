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
/** Maximum BSSIDs retained from one passive scan for the per-BSSID projection. */
#define RFSCOUT_WIFI_STATION_BSS_MAX_RESULTS 32U

typedef enum
{
    RFSCOUT_WIFI_STATION_OFF = 0,
    RFSCOUT_WIFI_STATION_IDLE,
    RFSCOUT_WIFI_STATION_SCANNING,
    RFSCOUT_WIFI_STATION_SEARCHING,
    RFSCOUT_WIFI_STATION_CONNECTING,
    RFSCOUT_WIFI_STATION_DISCONNECTING,
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

/** @brief Security class of one scanned BSSID as relevant to credential-free association. */
typedef enum
{
    RFSCOUT_WIFI_STATION_SECURITY_OPEN = 0,
    RFSCOUT_WIFI_STATION_SECURITY_OWE,
    RFSCOUT_WIFI_STATION_SECURITY_SECURED
} rfscout_wifi_station_security_t;

/** @brief One BSSID from the latest completed passive scan. */
typedef struct
{
    uint8_t bssid[ 6 ];
    char ssid[ RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U ];
    int8_t rssi;
    uint8_t channel;
    rfscout_wifi_station_security_t security;
} rfscout_wifi_station_bss_t;

/** @brief IPv4 configuration of the current station connection, in network byte order. */
typedef struct
{
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
} rfscout_wifi_station_ip_info_t;

typedef struct
{
    rfscout_wifi_station_state_t state;
    uint32_t scan_generation;
    int32_t disconnect_reason;
    char ssid[ RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U ];
    bool ntp_syncing;
    bool ntp_synced;
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
 * @brief Copy the latest completed passive scan as one record per visible BSSID.
 * @details Hidden SSIDs are not reported by the passive scan. Records keep driver scan order.
 * @return Number of records written to caller-owned storage.
 */
size_t rfscout_wifi_station_bss_results( rfscout_wifi_station_bss_t *a_results, size_t a_capacity );

/**
 * @brief Establish one explicitly requested station connection using caller-owned credentials.
 * @details If the target was absent from the latest passive scan snapshot, the service first
 *          performs an SSID-targeted active scan before association. When a_randomize_mac is true,
 *          the station restarts with a fresh random locally administered MAC before associating.
 *          NTP is not started automatically; the framework owns time synchronization policy.
 */
esp_err_t
rfscout_wifi_station_connect( const char *a_ssid, const char *a_password, bool a_randomize_mac );

/**
 * @brief Associate with one Open or OWE BSSID without credentials.
 * @details Association is pinned to the BSSID and channel, requires OWE when the record is OWE,
 *          and does not start NTP automatically; call rfscout_wifi_station_start_time_sync() after
 *          the station reports CONNECTED if wall-clock synchronization is wanted. When
 *          a_randomize_mac is true the station restarts with a fresh random locally administered
 *          MAC before associating; the factory MAC is restored by rfscout_wifi_station_stop().
 */
esp_err_t rfscout_wifi_station_connect_open_bss( const rfscout_wifi_station_bss_t *a_bss,
                                                 bool a_randomize_mac );

/**
 * @brief Start SNTP on the current connection.
 * @details Progress is reported through rfscout_wifi_station_status() ntp_syncing / ntp_synced. A
 *          successful sync is offered to device_time.
 */
esp_err_t rfscout_wifi_station_start_time_sync( void );

/**
 * @brief Abandon any connection attempt or connection and return to IDLE.
 * @details Usable from SEARCHING, CONNECTING, CONNECTED, or FAILED. Stops NTP and clears the
 *          transient station configuration while keeping the station started for scanning.
 */
esp_err_t rfscout_wifi_station_abort( void );

/** @brief Copy the current IPv4 configuration; ESP_ERR_INVALID_STATE when not connected. */
esp_err_t rfscout_wifi_station_ip_info( rfscout_wifi_station_ip_info_t *a_info );

/** @brief Disconnect the current station while leaving the Home session able to scan again. */
esp_err_t rfscout_wifi_station_disconnect( void );

/** @brief Cancel an active NTP synchronization attempt while preserving the Wi-Fi connection. */
esp_err_t rfscout_wifi_station_cancel_time_sync( void );

/**
 * @brief Tear down all STA work and clear transient driver configuration/results.
 * @details Used by the app-session lifecycle when the owning application exits. Restores the
 *          factory station MAC if a connection randomized it.
 */
esp_err_t rfscout_wifi_station_stop( void );

/** @brief Return whether the infrastructure Wi-Fi driver session is currently started. */
bool rfscout_wifi_station_is_active( void );

/** @brief Snapshot connection/scan state into caller-owned storage. */
void rfscout_wifi_station_status( rfscout_wifi_station_status_t *a_status );

#endif // _RFSCOUT_WIFI_STATION_H_
