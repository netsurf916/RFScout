/**
    radio_app_view.h : RFScout radio application views
    Description: Declares CC1101 and nRF24 spectrum and settings renderers.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_RADIO_APP_VIEW_H_
#define _RFSCOUT_RADIO_APP_VIEW_H_

#include "cc1101_activity.h"
#include "nrf24.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Render the CC1101 spectrum and likely-device pane. */
void radio_app_view_cc1101_spectrum( const uint8_t *a_current,
                                     size_t a_count,
                                     uint32_t a_low_khz,
                                     uint32_t a_high_khz,
                                     bool a_detected,
                                     const cc1101_fingerprint_t *a_devices,
                                     size_t a_device_count );

/** @brief Render the CC1101 settings menu including its app-scoped notification gate. */
void radio_app_view_cc1101_menu( unsigned a_selected,
                                 bool a_editing,
                                 const char *a_profile,
                                 uint32_t a_low_khz,
                                 uint32_t a_high_khz,
                                 uint32_t a_step_khz,
                                 size_t a_samples,
                                 bool a_limit_reached,
                                 bool a_notifications_enabled );

/** @brief Render the nRF24 activity spectrum and likely-device pane. */
void radio_app_view_nrf24_spectrum( const uint8_t *a_fast,
                                    size_t a_count,
                                    uint8_t a_low_channel,
                                    uint8_t a_high_channel,
                                    bool a_detected,
                                    const nrf24_fingerprint_t *a_devices,
                                    size_t a_device_count );

/** @brief Render the nRF24 settings menu including its app-scoped notification gate. */
void radio_app_view_nrf24_menu( unsigned a_selected,
                                bool a_editing,
                                uint8_t a_low_channel,
                                uint8_t a_high_channel,
                                uint16_t a_dwell_us,
                                bool a_notifications_enabled );

#endif // _RFSCOUT_RADIO_APP_VIEW_H_
