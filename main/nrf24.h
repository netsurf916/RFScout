/**
    nrf24.h : RFScout nRF24 receive-only channel activity scanner
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
typedef struct
{
    uint8_t low_channel;
    uint8_t high_channel;
    uint16_t dwell_us;
} nrf24_scan_config_t;
esp_err_t nrf24_init( void );
esp_err_t nrf24_start( void );
void nrf24_stop( void );
void nrf24_poll( void );
bool nrf24_is_detected( void );
const nrf24_scan_config_t *nrf24_config( void );
void nrf24_adjust_low( int a_delta );
void nrf24_adjust_high( int a_delta );
void nrf24_adjust_dwell( int a_delta );
bool nrf24_take_frame_ready( void );
size_t nrf24_spectrum( uint8_t *a_output, size_t a_capacity );
#endif
