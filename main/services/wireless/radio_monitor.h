/**
    radio_monitor.h : RFScout passive Wi-Fi observation interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_RADIO_MONITOR_H_
#define _RFSCOUT_RADIO_MONITOR_H_

#include "esp_err.h"
#include "rf_observation.h"

#include <stdbool.h>

typedef void ( *radio_observation_fn )( rf_device_t *a_observation );

typedef enum
{
    RF_WIFI_REGION_US = 0,
    RF_WIFI_REGION_ETSI,
    RF_WIFI_REGION_JAPAN,
    RF_WIFI_REGION_EXTENDED_RX
} rf_wifi_region_t;

/** @brief Initialize passive Wi-Fi monitoring and start the initial scan. */
esp_err_t radio_monitor_init( radio_observation_fn a_callback );

/** @brief Start Wi-Fi hardware and promiscuous reception after app entry. */
esp_err_t radio_monitor_start( void );

/** @brief Stop promiscuous reception and Wi-Fi hardware while app is inactive. */
esp_err_t radio_monitor_stop( void );

/** @brief Return whether the app currently requests Wi-Fi observation delivery. */
bool radio_monitor_is_running( void );

/** @brief Advance the channel-hopping state machine when required. */
void radio_monitor_step( void );

/** @brief Select the configured 2.4 GHz channel region. */
esp_err_t radio_monitor_set_region( rf_wifi_region_t a_region );

/** @brief Return the current Wi-Fi region setting. */
rf_wifi_region_t radio_monitor_get_region( void );

/** @brief Return a human-readable region name. */
const char *radio_monitor_region_name( rf_wifi_region_t a_region );

#endif // _RFSCOUT_RADIO_MONITOR_H_
