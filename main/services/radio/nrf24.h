/**
    nrf24.h : RFScout nRF24 receive-only channel activity scanner
    Description: Declares nRF24 scan configuration, lifecycle control, fingerprints, and spectrum access.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_NRF24_H_
#define _RFSCOUT_NRF24_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NRF24_CHANNEL_COUNT 126U
#define NRF24_FINGERPRINT_MAX 6U
#define NRF24_FINGERPRINT_LABEL_MAX 24U

/** @brief Active nRF24 channel sweep configuration. */
typedef struct
{
    uint8_t low_channel;
    uint8_t high_channel;
    uint16_t dwell_us;
} nrf24_scan_config_t;

/** @brief Conservative passive nRF24/2.4 GHz fingerprint categories. */
typedef enum
{
    NRF24_FP_UNKNOWN = 0,
    NRF24_FP_ESB,
    NRF24_FP_INPUT,
    NRF24_FP_SENSOR,
    NRF24_FP_WIFI_LIKE,
    NRF24_FP_BLE_LIKE,
    NRF24_FP_LOGITECH_LIKE
} nrf24_fingerprint_type_t;

/** @brief Accumulated evidence for a likely nRF24 device category. */
typedef struct
{
    nrf24_fingerprint_type_t type;
    uint8_t confidence;
    uint8_t channel;
    uint8_t payload_len;
    uint8_t bitrate;
    uint8_t address[ 5 ];
    uint8_t address_len;
    uint32_t packets;
    char label[ NRF24_FINGERPRINT_LABEL_MAX ];
} nrf24_fingerprint_t;

/** @brief Probe the nRF24 identity and leave the radio inactive. Safe to retry. */
esp_err_t nrf24_probe( void );

/** @brief Initialize reusable nRF24 service state after a successful probe. */
esp_err_t nrf24_init( void );

/** @brief Start asynchronous receive-only nRF24 channel acquisition. */
esp_err_t nrf24_start( void );

/** @brief Stop acquisition and place the nRF24 in its inactive state. */
bool nrf24_stop( void );

/** @brief Reserved non-blocking service hook for nRF24 application polling. */
void nrf24_poll( void );

/** @brief Return true when the nRF24 probe succeeded. */
bool nrf24_is_detected( void );

/** @brief Return the current channel sweep configuration. */
const nrf24_scan_config_t *nrf24_config( void );

/** @brief Adjust the lower channel by encoder delta. */
void nrf24_adjust_low( int a_delta );

/** @brief Adjust the upper channel by encoder delta. */
void nrf24_adjust_high( int a_delta );

/** @brief Adjust the channel dwell time by encoder delta. */
void nrf24_adjust_dwell( int a_delta );

/** @brief Consume the notification that a complete sweep is ready to render. */
bool nrf24_take_frame_ready( void );

/**
 * @brief Copy the most recently completed nRF24 activity frame.
 * @param a_output Destination for channel activity values.
 * @param a_capacity Destination capacity in channels.
 * @return Number of channel values copied.
 */
size_t nrf24_spectrum( uint8_t *a_output, size_t a_capacity );

#endif // _RFSCOUT_NRF24_H_
