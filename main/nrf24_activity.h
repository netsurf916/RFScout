/** nrf24_activity.h : adaptive nRF24 activity histogram and passive fingerprints */
#pragma once
#ifndef _RFSCOUT_NRF24_ACTIVITY_H_
#define _RFSCOUT_NRF24_ACTIVITY_H_
#include "nrf24.h"
void nrf24_activity_reset( void );
void nrf24_activity_observe_sweep( const uint8_t *a_hits, size_t a_count );
size_t nrf24_activity_histogram( uint8_t *a_fast, uint8_t *a_slow, size_t a_capacity );
size_t nrf24_activity_fingerprints( nrf24_fingerprint_t *a_output, size_t a_capacity );
const char *nrf24_activity_fingerprint_name( nrf24_fingerprint_type_t a_type );
#endif
