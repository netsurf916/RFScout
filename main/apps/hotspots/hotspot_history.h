/**
    apps/hotspots/hotspot_history.h : Hotspots durable characterization history
    Description: App-session index of every BSSID that passed a green/yellow check, replayed from
                 the app's SD journal and extended by new results and movement-triggered
                 location samples. No credentials or client addresses are journaled.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_HOTSPOT_HISTORY_H_
#define _RFSCOUT_HOTSPOT_HISTORY_H_

#include "geo.h"
#include "gps.h"
#include "hotspot_table.h"
#include "wifi_station.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Network evidence that defines a configuration epoch; client IP is deliberately excluded. */
typedef struct
{
    uint32_t gateway; /**< lwIP network byte order */
    uint32_t netmask;
    uint32_t dns_primary;
    uint32_t dns_backup;
    bool gateway_mac_valid;
    uint8_t gateway_mac[ 6 ];
} hotspot_history_network_t;

/** Durable knowledge about one BSSID within its current configuration epoch. */
typedef struct
{
    uint8_t bssid[ 6 ];
    char ssid[ RFSCOUT_WIFI_STATION_SSID_MAX_LENGTH + 1U ];
    int32_t auth_mode;
    hotspot_status_t status;  /**< GREEN or YELLOW; CAM is a display annotation. */
    uint32_t camera_evidence; /**< Optional gateway service evidence for the latest check. */
    int64_t validated_utc;    /**< latest successful check; 0 when the time was unknown */
    uint32_t successes;
    int8_t rssi; /**< RSSI when the latest successful check started */
    hotspot_history_network_t network;
    char public_ip[ HOTSPOT_PUBLIC_IP_MAX_LENGTH + 1U ];
    char colo[ HOTSPOT_COLO_MAX_LENGTH + 1U ];
    geo_bounds_t bounds;      /**< receiver positions where this BSSID was characterized/seen */
    geo_movement_gate_t gate; /**< last logged receiver position (runtime only) */
} hotspot_history_entry_t;

/** @brief Bind the journal for this app session and start replay; no SD I/O happens here. */
esp_err_t hotspot_history_open( void );

/** @brief Final flush, then drop every pointer into app-session memory. */
void hotspot_history_close( void );

/**
 * @brief Advance replay, compaction and journal writes from the app poll.
 * @details Replays a bounded number of lines per call, so the app keeps scanning and drawing
 *          while history loads. Appends are attempted at most once per second.
 */
void hotspot_history_poll( int64_t a_now_us );

/** @brief Whether replay finished (with or without media); checks may start only after it. */
bool hotspot_history_ready( void );

/** @brief Replay progress 0-100 while loading. */
uint8_t hotspot_history_progress( void );

/** @brief Whether the latest journal access failed and records are only held in memory. */
bool hotspot_history_degraded( void );

/** @brief Malformed or unreadable journal lines skipped by the latest replay. */
size_t hotspot_history_skipped( void );

/** @brief Request compaction of the journal into one summary record per BSSID. */
void hotspot_history_request_compaction( void );
bool hotspot_history_compacting( void );

size_t hotspot_history_count( void );
const hotspot_history_entry_t *hotspot_history_at( size_t a_index );
/** @brief History for a BSSID whose SSID and auth mode still match, or NULL. */
const hotspot_history_entry_t *hotspot_history_find( const rfscout_wifi_station_bss_t *a_bss );

/** @brief Record a finished green/yellow check; other outcomes are ignored. */
void hotspot_history_record_result( const rfscout_wifi_station_bss_t *a_bss,
                                    const hotspot_result_t *a_result,
                                    const rfscout_gps_snapshot_t *a_gps );

/** @brief Log a movement-triggered location sample for a visible BSSID with history. */
void hotspot_history_observe( const rfscout_wifi_station_bss_t *a_bss,
                              const rfscout_gps_snapshot_t *a_gps );

#endif // _RFSCOUT_HOTSPOT_HISTORY_H_
