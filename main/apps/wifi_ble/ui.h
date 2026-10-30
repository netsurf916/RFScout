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

/** @brief Request a redraw when the visible WiFi/BLE summary changes. */
void ui_poll( void );
/** @brief Draw the current WiFi/BLE dashboard without flushing it. */
void ui_render( void );
/** @brief Reset cached display state on entry to a new app session. */
void ui_reset( void );

#endif // _RFSCOUT_UI_H_
