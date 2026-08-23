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

/** @brief Initialize continuous passive BLE advertisement scanning. */
esp_err_t ble_monitor_init( ble_observation_fn a_callback );

#endif // _RFSCOUT_BLE_MONITOR_H_
