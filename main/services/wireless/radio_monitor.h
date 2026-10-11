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
#include <stddef.h>
#include <stdint.h>

/** @brief Raw 802.11 frame view; valid only for callback duration. */
typedef void ( *radio_raw_frame_fn )( const uint8_t *a_bytes,
                                      size_t a_length,
                                      int8_t a_rssi,
                                      uint8_t a_channel );
void radio_monitor_set_raw_observer( radio_raw_frame_fn a_callback );
/** @brief Configure uniform capture hopping or a fixed channel; reset when leaving capture. */
void radio_monitor_set_capture_policy( bool a_enabled,
                                       uint8_t a_fixed_channel,
                                       uint32_t a_dwell_ms );

typedef void ( *radio_observation_fn )( rf_device_t *a_observation );

/** @brief Bind the platform observation sink used while monitoring is active. */
void radio_monitor_set_observer( radio_observation_fn a_callback );

/** @brief Initialize reusable passive Wi-Fi monitor state without starting reception. */
esp_err_t radio_monitor_init( void );

/** @brief Activate Wi-Fi hardware and promiscuous reception after framework initialization. */
esp_err_t radio_monitor_start( void );

/** @brief Stop promiscuous reception and Wi-Fi hardware while app is inactive. */
esp_err_t radio_monitor_stop( void );

/** @brief Return whether the app currently requests Wi-Fi observation delivery. */
bool radio_monitor_is_active( void );
/** @brief Last configured Wi-Fi channel, for capture UI. */
uint8_t radio_monitor_channel( void );

/**
 * @brief Supply the canonical-store projection of channels with retained AP/BSSID evidence.
 * @param a_channel_mask Bit 0 represents channel 1 through bit 13 for channel 14.
 */
void radio_monitor_set_active_ap_channels( uint16_t a_channel_mask );

/** @brief Advance the adaptive channel-hopping state machine when required. */
void radio_monitor_step( void );

#endif // _RFSCOUT_RADIO_MONITOR_H_
