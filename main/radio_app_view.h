/**
    radio_app_view.h : RFScout radio application views
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_RADIO_APP_VIEW_H_
#define _RFSCOUT_RADIO_APP_VIEW_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void radio_app_view_cc1101_spectrum( const int8_t *a_bins, size_t a_count, uint32_t a_low_khz, uint32_t a_high_khz, bool a_detected );
void radio_app_view_cc1101_menu( unsigned a_selected, bool a_editing, const char *a_profile, uint32_t a_low_khz, uint32_t a_high_khz, uint32_t a_step_khz );
void radio_app_view_nrf24_spectrum( const uint8_t *a_bins, size_t a_count, uint8_t a_low_channel, uint8_t a_high_channel, bool a_detected );
void radio_app_view_nrf24_menu( unsigned a_selected, bool a_editing, uint8_t a_low_channel, uint8_t a_high_channel, uint16_t a_dwell_us );
#endif
