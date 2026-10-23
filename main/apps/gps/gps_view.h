/**
    gps_view.h : GPS diagnostic display contract
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_GPS_VIEW_H_
#define _RFSCOUT_GPS_VIEW_H_

#include <stdbool.h>
#include <stdint.h>

/** GPS Settings rows, in display order. */
typedef enum
{
    GPS_SETTING_BACK = 0,
    GPS_SETTING_RESET_TRIP,
    GPS_SETTING_LED,
    GPS_SETTING_LED_BRIGHTNESS,
    GPS_SETTING_LED_COLOR,
    GPS_SETTING_KEEP_AWAKE,
    GPS_SETTING_BREADCRUMBS,
    GPS_SETTING_BREADCRUMB_FILES,
    GPS_SETTING_MOVEMENT,
    GPS_SETTING_TIMEOUT,
    GPS_SETTING_TIMEPULSE,
    GPS_SETTING_COUNT
} gps_setting_t;

/** @brief Rows that act on receiver data are unavailable until a receiver is detected. */
bool gps_view_setting_available( unsigned a_setting );

/** @brief Next direction-LED color preset after a_rgb, wrapping. */
uint32_t gps_view_next_led_color( uint32_t a_rgb );

/** @brief Render available GPS receiver telemetry without owning the UART provider. */
void gps_view_render( bool a_menu_open, unsigned a_menu_item, bool a_menu_error );

#endif // _RFSCOUT_GPS_VIEW_H_
