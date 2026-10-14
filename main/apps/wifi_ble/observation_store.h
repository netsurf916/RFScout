/**
    observation_store.h : RFScout normalized observation store
    Description: Defines device observations shared by collectors, classifiers, and UI.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OBSERVATION_STORE_H_
#define _RFSCOUT_OBSERVATION_STORE_H_

#include "esp_err.h"
#include "rf_observation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_OBSERVATION_INITIAL_CAPACITY 32U

/** @brief One vendor/count row returned by an observation summary. */
typedef struct
{
    char name[ RFSCOUT_VENDOR_NAME_MAX ];
    size_t count;
} observation_vendor_count_t;

/** @brief Aggregate counts for the current live observation collection. */
typedef struct
{
    size_t device_count;
    size_t wifi_count;
    size_t ble_count;
    size_t unknown_count;
    size_t random_count;
} observation_store_summary_t;

typedef struct
{
    surveillance_signature_id_t signature;
    uint16_t count;
    uint32_t last_seen_ms;
} observation_surveillance_signature_t;

typedef enum
{
    OBSERVATION_STORE_EVENT_ADDED = 0,
    OBSERVATION_STORE_EVENT_SURVEILLANCE_CHANGED,
    OBSERVATION_STORE_EVENT_DROPPED_TIMEOUT,
    OBSERVATION_STORE_EVENT_DROPPED_CAPACITY
} observation_store_event_t;

typedef void ( *observation_store_event_callback_t )( observation_store_event_t a_event,
                                                      const rf_device_t *a_device,
                                                      void *a_context );

/**
 * @brief Initialize synchronization for the WiFi/BLE observation store.
 * @details Retained observation payload memory is allocated lazily while the application is active.
 */
esp_err_t observation_store_init( void );

/** @brief Register the live-list lifecycle callback. */
void observation_store_set_event_callback( observation_store_event_callback_t a_callback,
                                           void *a_context );

/**
 * @brief Add or refresh one normalized device observation.
 * @param a_observation Borrowed mutable observation; receives the canonical case result.
 * @param a_is_new Optional output set when this identity is newly active.
 * @param a_surveillance_new Optional output set when this active identity first becomes surveillance.
 */
bool observation_store_observe( rf_device_t *a_observation,
                                bool *a_is_new,
                                bool *a_surveillance_new );

/**
 * @brief Reevaluate surveillance classification for all active cases after a setting change.
 * @param a_newly_qualified Optional per-classification counts for cases that newly qualify.
 * @param a_count_capacity Number of entries available in a_newly_qualified.
 */
void observation_store_reevaluate_surveillance( size_t *a_newly_qualified,
                                                size_t a_count_capacity );

/** @brief Immutable GPS-qualified packet observation for an app-owned persistence sink. */
typedef struct
{
    uint8_t address[ 6 ];
    uint8_t address_type;
    surveillance_class_t classification;
    double latitude;
    double longitude;
    int64_t utc_epoch;
    uint32_t seen_ms;
} observation_location_event_t;

/** @brief Borrowed packet event, valid only during callback invocation. */
typedef void ( *observation_location_sink_t )( const observation_location_event_t *a_event );

/** @brief Register the app-owned location journal sink; NULL disables delivery. */
void observation_store_set_location_sink( observation_location_sink_t a_sink );

/** @brief Compact identity snapshot for history; avoids copying full device records. */
typedef struct
{
    uint8_t address[ 6 ];
    uint8_t address_type;
    char vendor[ RFSCOUT_VENDOR_NAME_MAX ];
    surveillance_class_t surveillance;
    surveillance_signature_id_t signature;
    uint32_t last_seen_ms;
    bool location_valid;
    double min_latitude;
    double max_latitude;
    double min_longitude;
    double max_longitude;
} observation_history_identity_t;

/** @brief Copy only fields needed for surveillance history. */
size_t observation_store_surveillance_devices( observation_history_identity_t *a_devices,
                                               size_t a_capacity );

/** @brief Read a single live surveillance device by ordinal under the store lock. */
bool observation_store_surveillance_get( size_t a_index, observation_history_identity_t *a_device );

/** @brief Clear all live observations while retaining any current app-owned capacity. */
void observation_store_reset( void );

/**
 * @brief Expire stale source memberships without requiring active radio collection or UI rendering.
 * @details Wi-Fi and Bluetooth source membership age independently according to app configuration;
 *          a merged observation remains live while at least one source is still within its window.
 */
void observation_store_housekeeping( void );

/** @brief Remove observations from a disabled source, preserving other source memberships. */
void observation_store_clear_source( rf_source_t a_source );

/**
 * @brief Return a bit mask of Wi-Fi channels with retained AP/BSSID evidence.
 * @details Bit 0 represents channel 1. The projection follows canonical Wi-Fi source lifetime.
 */
uint16_t observation_store_wifi_ap_channel_mask( void );

/** @brief Copy a coherent UI summary while holding the observation-store lock once. */
typedef struct
{
    observation_store_summary_t summary;
    uint16_t ap_channel_mask;
    size_t vendor_count;
    size_t signature_count;
    size_t surveillance_count;
} observation_ui_snapshot_t;

void observation_store_ui_snapshot( observation_ui_snapshot_t *a_snapshot,
                                    observation_vendor_count_t *a_vendors,
                                    size_t a_vendor_capacity,
                                    observation_surveillance_signature_t *a_signatures,
                                    size_t a_signature_capacity );

/** @brief Return the number of currently active observations. */
size_t observation_store_count( void );

/** @brief Return the number of currently active observations classified as surveillance. */
size_t observation_store_surveillance_count( void );

/**
 * @brief Return active surveillance signatures derived from the canonical observation collection.
 * @return Total number of active signature groups, which may exceed a_capacity.
 */
size_t observation_store_active_surveillance( observation_surveillance_signature_t *a_rows,
                                              size_t a_capacity,
                                              size_t *a_total_devices );

/**
 * @brief Build current aggregate counts and the most common known vendors.
 * @param a_summary Destination for aggregate counts.
 * @param a_vendors Optional destination for vendor rows sorted by descending count.
 * @param a_vendor_capacity Number of vendor rows the caller can display.
 * @return Number of vendor rows written.
 */
size_t observation_store_summary( observation_store_summary_t *a_summary,
                                  observation_vendor_count_t *a_vendors,
                                  size_t a_vendor_capacity );

#endif // _RFSCOUT_OBSERVATION_STORE_H_
