/**
    ble_monitor.h : RFScout passive BLE observation interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BLE_MONITOR_H_
#define _RFSCOUT_BLE_MONITOR_H_

#include "esp_err.h"
#include "observation_store.h"

typedef void ( *ble_observation_fn )( rf_device_t *a_observation );

/** @brief Initialize NimBLE and start the initial passive scan. */
esp_err_t ble_monitor_init( ble_observation_fn a_callback );

/** @brief Start passive BLE scanning when the WiFi/BT app is active. */
esp_err_t ble_monitor_start( void );

/** @brief Stop BLE scanning while another app is active. */
esp_err_t ble_monitor_stop( void );

#endif // _RFSCOUT_BLE_MONITOR_H_
