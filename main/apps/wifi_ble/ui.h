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

/** @brief Refresh the WiFi/BLE display when its update interval has elapsed. */
void ui_poll( void );

#endif // _RFSCOUT_UI_H_
