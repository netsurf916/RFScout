/**
    apps/hotspots/hotspot_table.h : Hotspots observation and characterization model
    Description: Session-scoped per-BSSID table with observation sessions, result rules, and
                 automatic-check candidate selection. Pure model; no radio or UI access.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_HOTSPOT_TABLE_H_
#define _RFSCOUT_HOTSPOT_TABLE_H_

#include "wifi_station.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Weakest RSSI at which an automatic check may start. */
#define HOTSPOT_AUTO_RSSI_FLOOR_DBM ( -80 )
/** RSSI improvement over the last failed attempt required before an automatic retry. */
#define HOTSPOT_RETRY_IMPROVEMENT_DB 4
/** Consecutive missed completed scans that end a BSSID's observation session. */
#define HOTSPOT_MISSED_SCAN_LIMIT 3U
#define HOTSPOT_PUBLIC_IP_MAX_LENGTH 45U
#define HOTSPOT_COLO_MAX_LENGTH 15U

/** @brief Connectivity status of one BSSID within its observation session. */
typedef enum
{
    HOTSPOT_STATUS_UNCHECKED = 0,
    HOTSPOT_STATUS_GREEN,
    HOTSPOT_STATUS_YELLOW,
    HOTSPOT_STATUS_RED
} hotspot_status_t;

/** @brief Stage of a connectivity check, used for progress and for the failing stage. */
typedef enum
{
    HOTSPOT_STAGE_NONE = 0,
    HOTSPOT_STAGE_JOIN,
    HOTSPOT_STAGE_NTP,
    HOTSPOT_STAGE_HTTPS
} hotspot_stage_t;

/** @brief Outcome of one finished connectivity check. */
typedef struct
{
    hotspot_status_t outcome;
    hotspot_stage_t failed_stage;
    bool manual;
    bool ntp_ok;
    int8_t start_rssi;
    int http_status;
    esp_err_t error;
    int32_t disconnect_reason;
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_primary;
    uint32_t dns_backup;
    char public_ip[ HOTSPOT_PUBLIC_IP_MAX_LENGTH + 1U ];
    char colo[ HOTSPOT_COLO_MAX_LENGTH + 1U ];
} hotspot_result_t;

/** @brief One observed eligible BSSID and its current-session characterization. */
typedef struct
{
    rfscout_wifi_station_bss_t bss;
    bool visible;
    bool credential_available; /**< open/OWE, or secured with a resolved saved profile */
    uint32_t profile_id;       /**< saved profile used for a secured check; 0 when none */
    uint8_t missed_scans;
    hotspot_status_t status;
    bool retry_gate_active;
    int8_t retry_gate_rssi;
    bool manual_requested;
    uint16_t attempts;
    bool has_result;
    hotspot_result_t last; /**< Most recent attempt, including a later failure. */
    bool has_success;
    hotspot_result_t success; /**< Last validated green/yellow result, retained after failure. */
} hotspot_entry_t;

/** @brief Growable app-session table of eligible BSSIDs plus last-scan environment counts. */
typedef struct
{
    hotspot_entry_t *entries;
    size_t count;
    size_t capacity;
    size_t open_count;
    size_t owe_count;
    size_t secured_count;
    uint32_t scans_completed;
} hotspot_table_t;

/** @brief Return whether a scanned BSSID has a usable named SSID for observation. */
bool hotspot_bss_eligible( const rfscout_wifi_station_bss_t *a_bss );

/** @brief Empty the table; storage is app-session memory reclaimed with the session. */
void hotspot_table_reset( hotspot_table_t *a_table );

/**
 * @brief Apply one completed passive scan.
 * @details Refreshes visible entries, appends newly seen eligible BSSIDs, advances missed-scan
 *          counters, and removes entries whose observation session ended. Counts are recomputed.
 * @return ESP_ERR_NO_MEM when growth failed; existing entries remain valid and are updated.
 */
esp_err_t hotspot_table_apply_scan( hotspot_table_t *a_table,
                                    const rfscout_wifi_station_bss_t *a_records,
                                    size_t a_count );

/** @brief Find an entry by BSSID; the pointer is valid until the next table mutation. */
hotspot_entry_t *hotspot_table_find( hotspot_table_t *a_table, const uint8_t a_bssid[ 6 ] );

/** @brief Return whether an entry would currently pass the automatic RSSI gates. */
bool hotspot_auto_gate_open( const hotspot_entry_t *a_entry );

/** @brief Return whether automatic checking still applies to an entry this session. */
bool hotspot_auto_pending( const hotspot_entry_t *a_entry );

/**
 * @brief Choose the next check: a visible manual request first, otherwise (when enabled) the
 *        strongest visible entry that passes the automatic gates.
 * @return The chosen entry, or NULL when nothing should be checked now.
 */
hotspot_entry_t *hotspot_table_next_candidate( hotspot_table_t *a_table, bool a_auto_enabled );

/** @brief Return whether any visible entry has a pending manual request. */
bool hotspot_table_manual_pending( const hotspot_table_t *a_table );

/** @brief Record one finished check against an entry using the session status rules. */
void hotspot_entry_record_result( hotspot_entry_t *a_entry, const hotspot_result_t *a_result );

/**
 * @brief Write entry indices in presentation order: visible first, strongest RSSI first, then
 *        not-visible entries; ties keep table order.
 * @return Number of indices written.
 */
size_t hotspot_table_order( const hotspot_table_t *a_table, size_t *a_indices, size_t a_capacity );

#endif // _RFSCOUT_HOTSPOT_TABLE_H_
