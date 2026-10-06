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

/**
 * @brief Callback invoked when passive BLE scanning produces an observation.
 * @param a_observation Mutable observation populated from the received BLE advertisement.
 */
typedef void ( *ble_observation_fn )( rf_device_t *a_observation );

/** @brief Bind the platform observation sink used while scanning is active. */
void ble_monitor_set_observer( ble_observation_fn a_callback );

/** @brief Initialize reusable BLE monitor state without starting a scan. */
esp_err_t ble_monitor_init( void );

/** @brief Activate passive BLE scanning after framework initialization. */
esp_err_t ble_monitor_start( void );

/** @brief Stop BLE scanning while another app is active. */
esp_err_t ble_monitor_stop( void );

/** @brief Return whether the app currently requests BLE observation delivery. */
bool ble_monitor_is_running( void );

#endif // _RFSCOUT_BLE_MONITOR_H_
