/**
    rf_observation.h : RFScout normalized RF observation contract
    Description: Defines normalized observations shared by wireless collectors and applications.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_RF_OBSERVATION_H_
#define _RFSCOUT_RF_OBSERVATION_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    SURVEILLANCE_ALPR,
    SURVEILLANCE_BODY_CAMERA,
    SURVEILLANCE_DRONE,
    SURVEILLANCE_CAMERA,
    SURVEILLANCE_SMART_GLASSES,
    SURVEILLANCE_AUDIO,
    SURVEILLANCE_OTHER
} surveillance_class_t;

typedef uint8_t surveillance_signature_id_t;

#define SURVEILLANCE_SIGNATURE_NONE 0U
#define RFSCOUT_SURVEILLANCE_SIGNATURE_MAX 32U
#define RFSCOUT_SURVEILLANCE_EVIDENCE_MAX 8U

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
    uint32_t wifi_last_seen_ms;
    uint32_t ble_last_seen_ms;
    surveillance_class_t surveillance;
    surveillance_signature_id_t surveillance_signature;
    uint8_t surveillance_evidence;
    uint8_t surveillance_score;
    uint8_t surveillance_evidence_count;
    bool surveillance_evidence_overflow;
    uint16_t surveillance_evidence_ids[ RFSCOUT_SURVEILLANCE_EVIDENCE_MAX ];
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

/**
 * @brief Return whether an observation uses a randomized/private link-layer address.
 * @details Wi-Fi uses the IEEE locally-administered bit. NimBLE address types use bit 0 to
 *          distinguish random/random-identity addresses from public/public-identity addresses.
 */
static inline bool rf_device_address_is_randomized( const rf_device_t *a_device )
{
    if( NULL == a_device )
    {
        return false;
    }

    const bool wifi_randomized =
        ( 0U != ( a_device->source_mask & RF_SOURCE_BIT( RF_SOURCE_WIFI ) ) ) &&
        ( 0U != ( a_device->address[ 0 ] & 0x02U ) );
    const bool ble_randomized =
        ( 0U != ( a_device->source_mask & RF_SOURCE_BIT( RF_SOURCE_BLE ) ) ) &&
        ( 0U != ( a_device->address_type & 0x01U ) );
    return wifi_randomized || ble_randomized;
}

#endif // _RFSCOUT_RF_OBSERVATION_H_
