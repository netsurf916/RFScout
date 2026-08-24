/**
    cc1101.h : RFScout CC1101 receive-only spectrum scanner
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_CC1101_H_
#define _RFSCOUT_CC1101_H_
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CC1101_SPECTRUM_BINS 128U
typedef enum { CC1101_PROFILE_433=0, CC1101_PROFILE_315, CC1101_PROFILE_868, CC1101_PROFILE_915, CC1101_PROFILE_MANUAL, CC1101_PROFILE_COUNT } cc1101_profile_t;
typedef struct { cc1101_profile_t profile; uint32_t low_khz; uint32_t high_khz; uint32_t step_khz; } cc1101_scan_config_t;
esp_err_t cc1101_init( void );
esp_err_t cc1101_start( void );
void cc1101_stop( void );
void cc1101_poll( void );
bool cc1101_is_detected( void );
const cc1101_scan_config_t *cc1101_config( void );
void cc1101_set_profile( cc1101_profile_t a_profile );
void cc1101_adjust_low( int a_delta );
void cc1101_adjust_high( int a_delta );
void cc1101_adjust_step( int a_delta );
const char *cc1101_profile_name( cc1101_profile_t a_profile );
size_t cc1101_spectrum( int8_t *a_output, size_t a_capacity );
#endif
