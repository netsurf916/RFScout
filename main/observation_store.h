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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_DEVICE_CAPACITY 1000
#define RFSCOUT_DEVICE_TIMEOUT_MS 20000U
#define RFSCOUT_VENDOR_NAME_MAX 32
#define RFSCOUT_DEVICE_NAME_MAX 32
#define RFSCOUT_MFG_DATA_MAX 24
#define RFSCOUT_BLE_SERVICE_UUID_MAX 12

typedef enum
{
    RF_SOURCE_WIFI = 1,
    RF_SOURCE_BLE = 2,
    RF_SOURCE_SUBGHZ = 3,
    RF_SOURCE_NRF24 = 4,
    RF_SOURCE_NFC = 5,
    RF_SOURCE_IR = 6
} rf_source_t;

#define RF_SOURCE_BIT( a_source ) ( 1U << ( (unsigned)( a_source ) - 1U ) )

typedef enum
{
    SURVEILLANCE_NONE = 0,
    SURVEILLANCE_FLOCK,
    SURVEILLANCE_BODY_CAMERA,
    SURVEILLANCE_DRONE,
    SURVEILLANCE_CAMERA,
    SURVEILLANCE_SMART_GLASSES,
    SURVEILLANCE_AUDIO,
    SURVEILLANCE_OTHER
} surveillance_class_t;

typedef enum
{
    SURVEILLANCE_SIGNATURE_NONE = 0,
    SURVEILLANCE_SIGNATURE_FLOCK,
    SURVEILLANCE_SIGNATURE_VIGILANT,
    SURVEILLANCE_SIGNATURE_RAVEN,
    SURVEILLANCE_SIGNATURE_AXON,
    SURVEILLANCE_SIGNATURE_RING,
    SURVEILLANCE_SIGNATURE_VERKADA,
    SURVEILLANCE_SIGNATURE_AXIS,
    SURVEILLANCE_SIGNATURE_HIKVISION,
    SURVEILLANCE_SIGNATURE_DAHUA,
    SURVEILLANCE_SIGNATURE_REMOTE_ID,
    SURVEILLANCE_SIGNATURE_META,
    SURVEILLANCE_SIGNATURE_COUNT
} surveillance_signature_id_t;

typedef enum
{
    SURVEILLANCE_EVIDENCE_OUI = 1U << 0,
    SURVEILLANCE_EVIDENCE_NAME = 1U << 1,
    SURVEILLANCE_EVIDENCE_COMPANY_ID = 1U << 2,
    SURVEILLANCE_EVIDENCE_SERVICE_UUID = 1U << 3,
    SURVEILLANCE_EVIDENCE_REMOTE_ID = 1U << 4,
    SURVEILLANCE_EVIDENCE_PAYLOAD = 1U << 5
} surveillance_evidence_t;

typedef struct
{
    uint8_t address[ 6 ];
    uint8_t address_type;
    rf_source_t last_source;
    uint32_t source_mask;
    int8_t rssi;
    uint16_t channel;
    uint32_t first_seen_ms;
    uint32_t last_seen_ms;
    surveillance_class_t surveillance;
    surveillance_signature_id_t surveillance_signature;
    uint8_t surveillance_score;
    uint8_t surveillance_evidence;
    char vendor[ RFSCOUT_VENDOR_NAME_MAX ];
    char name[ RFSCOUT_DEVICE_NAME_MAX ];
    bool has_name;
    uint16_t company_id;
    uint8_t manufacturer_data[ RFSCOUT_MFG_DATA_MAX ];
    uint8_t manufacturer_data_len;
    bool has_manufacturer_data;
    uint16_t service_uuids[ RFSCOUT_BLE_SERVICE_UUID_MAX ];
    uint8_t service_uuid_count;
    bool has_remote_id;
} rf_device_t;

typedef enum
{
    OBSERVATION_STORE_EVENT_ADDED = 0,
    OBSERVATION_STORE_EVENT_DROPPED_TIMEOUT,
    OBSERVATION_STORE_EVENT_DROPPED_CAPACITY
} observation_store_event_t;

typedef void ( *observation_store_event_callback_t )( observation_store_event_t a_event,
                                                      const rf_device_t *a_device,
                                                      void *a_context );

/** @brief Initialize the observation store in external PSRAM. */
esp_err_t observation_store_init( void );

/** @brief Register the live-list lifecycle callback. */
void observation_store_set_event_callback( observation_store_event_callback_t a_callback,
                                           void *a_context );

/** @brief Add or refresh one normalized device observation. */
bool observation_store_observe( const rf_device_t *a_observation, bool *a_is_new );

/** @brief Clear all live observations while retaining the allocated store and callback. */
void observation_store_reset( void );

/**
 * @brief Expire stale observations without requiring active radio collection or UI rendering.
 * @details This keeps timeout lifecycle events timely while another app owns the screen and radios.
 */
void observation_store_housekeeping( void );

/** @brief Return the number of currently active observations. */
size_t observation_store_count( void );

/** @brief Return the number of currently active observations classified as surveillance. */
size_t observation_store_surveillance_count( void );

/** @brief Copy a consistent snapshot of the currently active observation store. */
size_t observation_store_snapshot( rf_device_t *a_output, size_t a_capacity );

#endif // _RFSCOUT_OBSERVATION_STORE_H_
