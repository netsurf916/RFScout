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

#include "geo.h"
#include "gps.h"
#include "wifi_station.h"
#include "camera_probe_rules.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Weakest RSSI at which an automatic check may start. */
#define HOTSPOT_AUTO_RSSI_FLOOR_DBM ( -80 )
/** RSSI improvement over the last failed attempt required before an automatic retry. */
#define HOTSPOT_RETRY_IMPROVEMENT_DB 4
/** Default expiration after 60 seconds of actual monitor receive time. */
#define HOTSPOT_RETENTION_DEFAULT_SECONDS 60U
/** Fresh when heard within more than two 3.2-second sweeps of monitor receive time. */
#define HOTSPOT_VISIBILITY_TIMEOUT_MS 8000U
/** Provisional bounded live-list budget; validate against measured PSRAM/TLS headroom. */
#define HOTSPOT_TABLE_MAX_ENTRIES 128U
/** Distance beyond the observed coverage circle, not distance from its center. */
#define HOTSPOT_EVICTION_DISTANCE_DEFAULT_M 500U
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
    HOTSPOT_STAGE_HTTPS,
    HOTSPOT_STAGE_CAMERA
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
    uint32_t camera_evidence; /**< Local gateway HTTP metadata, not AP identity. */
    esp_err_t error;
    int32_t disconnect_reason;
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_primary;
    uint32_t dns_backup;
    char public_ip[ HOTSPOT_PUBLIC_IP_MAX_LENGTH + 1U ];
    char colo[ HOTSPOT_COLO_MAX_LENGTH + 1U ];
    bool gateway_mac_valid; /**< gateway hardware address read from the station ARP cache */
    uint8_t gateway_mac[ 6 ];
} hotspot_result_t;

/** @brief Whether a LIMITED network's gateway advertised a camera-like service.
 * @details A hint is not proof of ONVIF conformance, camera identity or authorization.
 */
static inline bool hotspot_result_camera_hint( const hotspot_result_t *a_result )
{
    return NULL != a_result && HOTSPOT_STATUS_YELLOW == a_result->outcome &&
           camera_probe_camera_hint( a_result->camera_evidence );
}

/** @brief How a BSSID's saved credentials were resolved after the latest scan. */
typedef enum
{
    HOTSPOT_CREDENTIAL_NONE = 0,    /**< secured, no saved profile for this SSID */
    HOTSPOT_CREDENTIAL_OPEN,        /**< open or OWE: no credentials needed */
    HOTSPOT_CREDENTIAL_KNOWN,       /**< profile already associated with this exact BSSID */
    HOTSPOT_CREDENTIAL_INFERRED,    /**< one profile's known BSSIDs are in range or nearby */
    HOTSPOT_CREDENTIAL_AMBIGUOUS,   /**< several profiles have evidence: user must choose */
    HOTSPOT_CREDENTIAL_UNMATCHED,   /**< same-SSID profiles without evidence: manual join only */
    HOTSPOT_CREDENTIAL_REJECTED,    /**< every same-SSID profile was refused by this BSSID */
    HOTSPOT_CREDENTIAL_UNSUPPORTED, /**< secured mode the station cannot join (WEP, WPA1, 802.1X) */
} hotspot_credential_t;

/** @brief One observed eligible BSSID and its current-session characterization. */
typedef struct
{
    rfscout_wifi_station_bss_t bss;
    bool visible;
    bool credential_available; /**< automatic checks may run: open/OWE, known or inferred */
    hotspot_credential_t credential;
    uint32_t profile_id;        /**< profile for automatic secured checks; 0 when none */
    uint32_t manual_profile_id; /**< profile the user chose with Join; used by the queued check */
    uint32_t last_seen_ms;      /**< Logical scanning clock, excludes association/TLS downtime. */
    uint32_t
        observed_sweep_id; /**< Current sweep: used for GPS/time expiration, not capacity immunity. */
    geo_bounds_t live_bounds; /**< Receiver positions seen in this Live observation session. */
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

/** @brief Configurable normal expiration; 0 distance disables GPS-based eviction. */
typedef struct
{
    uint32_t timeout_ms;           /**< Effective monitor receive time without observation. */
    uint16_t distance_threshold_m; /**< Metres beyond estimated coverage, in 100 m steps. */
} hotspot_retention_t;

/** @brief Bounded app-session table of eligible BSSIDs plus last-scan environment counts. */
typedef struct
{
    hotspot_entry_t *entries;
    size_t count;
    size_t capacity;
    size_t open_count;
    size_t owe_count;
    size_t secured_count;
    uint32_t sweep_id; /**< Admission generation, never used for expiration or UI freshness. */
} hotspot_table_t;

/** @brief Return whether a scanned BSSID has a usable named SSID for observation. */
bool hotspot_bss_eligible( const rfscout_wifi_station_bss_t *a_bss );

/** @brief Whether a check may start now: automatically resolved credentials or a manual pick. */
bool hotspot_entry_checkable( const hotspot_entry_t *a_entry );

/** @brief Empty the table and release its app-session storage immediately. */
void hotspot_table_reset( hotspot_table_t *a_table );

/** @brief Begin a monitor sweep without invalidating timestamp-based visibility. */
void hotspot_table_begin_sweep( hotspot_table_t *a_table );

/**
 * @brief Ingest one AP observation on the app task (never from the Wi-Fi RX callback).
 * @details Capacity admission uses app-owned credential tiers, oldest observation,
 *          RSSI and BSSID ordering. Queued manual work is protected; current-sweep
 *          sightings may yield under capacity pressure. Caller supplies the credential
 *          resolver's non-secret result for this observation.
 */
esp_err_t hotspot_table_observe( hotspot_table_t *a_table,
                                 const rfscout_wifi_station_bss_t *a_bss,
                                 uint32_t a_monitor_ms,
                                 const rfscout_gps_snapshot_t *a_gps,
                                 hotspot_credential_t a_credential,
                                 uint32_t a_profile_id );

/** @brief Recompute timestamp-based UI/check freshness and visible security counts. */
void hotspot_table_refresh_visibility( hotspot_table_t *a_table, uint32_t a_monitor_ms );

/** @brief Expire overdue/out-of-range observations, preserving current sightings and manual work. */
void hotspot_table_finish_sweep( hotspot_table_t *a_table,
                                 uint32_t a_monitor_ms,
                                 const hotspot_retention_t *a_retention,
                                 const rfscout_gps_snapshot_t *a_gps );

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
