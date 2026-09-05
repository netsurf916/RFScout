/**
    ui.h : RFScout situational-awareness user interface
    Description: Renders compact live RF census data to the LCD.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_UI_H_
#define _RFSCOUT_UI_H_

#include "esp_err.h"

/**
 * @brief Initialize the LCD-backed RFScout UI.
 * @return ESP_OK on success; an ESP-IDF error code on failure.
 */
esp_err_t ui_init( void );

/**
 * @brief Refresh the display when the UI update interval has elapsed.
 */
void ui_poll( void );

#endif // _RFSCOUT_UI_H_
