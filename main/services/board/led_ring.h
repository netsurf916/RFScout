/**
    led_ring.h : Application-facing indexed RGB LED ring service
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_LED_RING_H_
#define _RFSCOUT_LED_RING_H_

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define RFSCOUT_LED_RING_COUNT 8U

typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} rfscout_led_color_t;

/** Initialize on-demand; unsupported boards return ESP_ERR_NOT_SUPPORTED. */
esp_err_t led_ring_init( void );
/** Blank the ring and release the RMT channel and encoder at application exit. */
esp_err_t led_ring_deinit( void );
/** Replace a single LED without changing the other cached pixels. */
esp_err_t led_ring_set( unsigned a_index, rfscout_led_color_t a_color );
/** Set global intensity (0..100 percent), then repaint the cached frame. */
esp_err_t led_ring_set_brightness( uint8_t a_percent );
/** Commit the eight cached colors as an atomic GRB WS2812 frame. */
esp_err_t led_ring_show( void );
/** Turn all LEDs off and update the cached frame. */
esp_err_t led_ring_clear( void );
bool led_ring_available( void );

#endif // _RFSCOUT_LED_RING_H_
