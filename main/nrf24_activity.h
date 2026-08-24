/** nrf24_activity.h : adaptive nRF24 activity histogram and passive fingerprints */
#pragma once
#ifndef _RFSCOUT_NRF24_ACTIVITY_H_
#define _RFSCOUT_NRF24_ACTIVITY_H_
#include "nrf24.h"

typedef struct
{
    uint8_t channel;
    uint8_t bitrate;
    uint8_t address[ 5 ];
    uint8_t address_len;
    uint8_t payload_len;
    uint8_t pid;
    bool no_ack;
    bool crc_valid;
    uint32_t timestamp_ms;
} nrf24_packet_observation_t;

void nrf24_activity_reset( void );
void nrf24_activity_observe_sweep( const uint8_t *a_hits, size_t a_count );
void nrf24_activity_observe_packet( const nrf24_packet_observation_t *a_packet );
size_t nrf24_activity_histogram( uint8_t *a_fast, uint8_t *a_slow, size_t a_capacity );
size_t nrf24_activity_fingerprints( nrf24_fingerprint_t *a_output, size_t a_capacity );
const char *nrf24_activity_fingerprint_name( nrf24_fingerprint_type_t a_type );
#endif
