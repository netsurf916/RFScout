/**
    ble_monitor.h : RFScout passive BLE observation interface
    Description: Declares BLE scanner lifecycle control and observation delivery callbacks.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BLE_MONITOR_H_
#define _RFSCOUT_BLE_MONITOR_H_

#include "esp_err.h"
#include "rf_observation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Callback invoked when passive BLE scanning produces an observation.
 * @param a_observation Mutable observation populated from the received BLE advertisement.
 */
/** @brief NimBLE advertising-report view; bytes valid only during the callback. */
typedef void ( *ble_raw_adv_fn )( const uint8_t *a_bytes,
                                  size_t a_length,
                                  const uint8_t a_address[ 6 ],
                                  uint8_t a_address_type,
                                  uint8_t a_event_type,
                                  int8_t a_rssi );
void ble_monitor_set_raw_observer( ble_raw_adv_fn a_callback );

typedef void ( *ble_observation_fn )( rf_device_t *a_observation );

/** @brief Bind the platform observation sink used while scanning is active. */
void ble_monitor_set_observer( ble_observation_fn a_callback );

/** @brief Initialize reusable BLE monitor state without starting a scan. */
esp_err_t ble_monitor_init( void );

/** @brief Activate passive BLE scanning after framework initialization. */
/** @brief Configure scan behavior before starting the app-owned BLE session. */
esp_err_t ble_monitor_set_scan_preferences( bool a_active,
                                            bool a_filter_duplicates,
                                            uint16_t a_interval,
                                            uint16_t a_window );
esp_err_t ble_monitor_start( void );

/** @brief Stop BLE scanning while another app is active. */
esp_err_t ble_monitor_stop( void );

/** @brief Return whether the app currently requests BLE observation delivery. */
bool ble_monitor_is_active( void );

#endif // _RFSCOUT_BLE_MONITOR_H_
