/**
    ui.h : RFScout situational-awareness user interface
    Description: Renders compact live RF census data to the LCD.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_UI_H_
#define _RFSCOUT_UI_H_

#include <stdbool.h>

/** @brief Switch between summary and live surveillance list. */
void ui_set_surveillance_page( bool a_enabled );

/** @brief Return whether the surveillance list is displayed. */
bool ui_surveillance_page_active( void );

/** @brief Move the selected surveillance list row by one step. */
void ui_surveillance_navigate( int a_direction );

/** @brief Show whether the rotary scrolls the list or switches views. */
void ui_surveillance_set_focused( bool a_focused );

/** @brief Release application-owned surveillance display buffers. */
void ui_surveillance_release( void );

/** @brief Refresh the WiFi/BLE display when its update interval has elapsed. */
void ui_poll( void );

#endif // _RFSCOUT_UI_H_
