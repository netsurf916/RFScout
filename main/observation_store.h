/**
    observation_store.h : RFScout normalized observation store
    Description: Defines device observations shared by collectors, classifiers, and UI.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_OBSERVATION_STORE_H_
#define _RFSCOUT_OBSERVATION_STORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_DEVICE_CAPACITY 1000
#define RFSCOUT_VENDOR_NAME_MAX 32
#define RFSCOUT_DEVICE_NAME_MAX 32
#define RFSCOUT_MFG_DATA_MAX 16

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
    SURVEILLANCE_OTHER
} surveillance_class_t;

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
    char vendor[ RFSCOUT_VENDOR_NAME_MAX ];
    char name[ RFSCOUT_DEVICE_NAME_MAX ];
    bool has_name;
    uint16_t company_id;
    uint8_t manufacturer_data[ RFSCOUT_MFG_DATA_MAX ];
    uint8_t manufacturer_data_len;
    bool has_manufacturer_data;
} rf_device_t;

/** @brief Reset the in-memory observation store. */
void observation_store_init( void );

/** @brief Add or refresh one normalized device observation. */
bool observation_store_observe( const rf_device_t *a_observation, bool *a_is_new );

/** @brief Return the number of stored unique observations. */
size_t observation_store_count( void );

/** @brief Return the number of observations classified as surveillance. */
size_t observation_store_surveillance_count( void );

/** @brief Copy a consistent snapshot of the observation store. */
size_t observation_store_snapshot( rf_device_t *a_output, size_t a_capacity );

#endif // _RFSCOUT_OBSERVATION_STORE_H_
