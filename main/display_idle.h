/**
    display_idle.h : RFScout global display idle policy
    Description: Controls device-wide dim/off timing and wake-consume behavior without stopping apps.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DISPLAY_IDLE_H_
#define _RFSCOUT_DISPLAY_IDLE_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    RFSCOUT_DISPLAY_IDLE_ACTIVE = 0,
    RFSCOUT_DISPLAY_IDLE_DIMMED,
    RFSCOUT_DISPLAY_IDLE_OFF
} rfscout_display_idle_state_t;

/** @brief Initialize persisted global display-idle settings and activity timing. */
esp_err_t display_idle_init( void );

/** @brief Apply idle transitions and consume the first input used to wake a dim/off display. */
void display_idle_poll( void );

/** @brief Return whether the presentation layer may transfer the framebuffer to LCD GRAM. */
bool display_idle_presentation_enabled( void );

/** @brief Return the current global display-idle state. */
rfscout_display_idle_state_t display_idle_state( void );

/** @brief Return the configured inactivity interval before dimming, or zero when disabled. */
uint32_t display_idle_dim_timeout_seconds( void );

/** @brief Return the configured inactivity interval before display-off, or zero when disabled. */
uint32_t display_idle_off_timeout_seconds( void );

/** @brief Cycle the supported dim timeout values and persist the new value. */
esp_err_t display_idle_adjust_dim_timeout( int a_direction );

/** @brief Cycle the supported screen-off timeout values and persist the new value. */
esp_err_t display_idle_adjust_off_timeout( int a_direction );

/** @brief Format one supported timeout for Device Settings. */
const char *display_idle_timeout_label( uint32_t a_seconds );

#endif // _RFSCOUT_DISPLAY_IDLE_H_
