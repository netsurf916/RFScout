/**
    nrf24_activity.h : Adaptive nRF24 activity histogram and passive fingerprints
    Description: Declares sweep accumulation, packet observations, and likely-device summaries.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_NRF24_ACTIVITY_H_
#define _RFSCOUT_NRF24_ACTIVITY_H_

#include "nrf24.h"

#define NRF24_RECENT_PACKET_MAX 5U

/** @brief Metadata extracted from a candidate Enhanced ShockBurst packet. */
typedef struct
{
    uint8_t channel;
    uint8_t bitrate;
    uint8_t address[ 5 ];
    uint8_t address_len;
    uint8_t payload_len;
    uint8_t payload[ 32 ];
    uint8_t pid;
    bool no_ack;
    bool crc_valid;
    uint32_t timestamp_ms;
} nrf24_packet_observation_t;

/** @brief Display-safe copy of one recently validated Enhanced ShockBurst packet. */
typedef struct
{
    uint8_t channel;
    uint8_t address[ 5 ];
    uint8_t address_len;
    uint8_t payload[ 32 ];
    uint8_t payload_len;
    uint32_t timestamp_ms;
} nrf24_recent_packet_t;

/** @brief Clear histogram, classifier, peak, and accumulated count state. */
void nrf24_activity_reset( void );

/** @brief Clear detection counts without changing scanner configuration or recent packets. */
void nrf24_activity_reset_counts( void );

/**
 * @brief Add one completed channel sweep to the adaptive activity history.
 * @param a_hits Per-channel energy-detection results.
 * @param a_count Number of channel entries in a_hits.
 */
void nrf24_activity_observe_sweep( const uint8_t *a_hits, size_t a_count );

/** @brief Add validated packet metadata to passive fingerprint classification. */
void nrf24_activity_observe_packet( const nrf24_packet_observation_t *a_packet );

/** @brief Copy recently validated packets in newest-first order. */
size_t nrf24_activity_recent_packets( nrf24_recent_packet_t *a_output, size_t a_capacity );

/**
 * @brief Copy fast activity and slow peak-hold histograms.
 * @param a_fast Destination for fast-decay activity.
 * @param a_slow Destination for slow-decay peak history.
 * @param a_capacity Capacity of each destination array.
 * @return Number of channel values copied.
 */
size_t nrf24_activity_histogram( uint8_t *a_fast, uint8_t *a_slow, size_t a_capacity );

/** @brief Copy the fixed detection-category rows and accumulated counts. */
size_t nrf24_activity_fingerprints( nrf24_fingerprint_t *a_output, size_t a_capacity );

/** @brief Return the raw accumulated count for one nRF24 detection category. */
uint32_t nrf24_activity_category_count( nrf24_fingerprint_type_t a_type );

/** @brief Return a stable display name for an nRF24 fingerprint category. */
const char *nrf24_activity_fingerprint_name( nrf24_fingerprint_type_t a_type );

#endif // _RFSCOUT_NRF24_ACTIVITY_H_
