/**
    display.h : RFScout LCD interface
    Description: Provides a small framebuffer-backed ST7789 drawing interface.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DISPLAY_H_
#define _RFSCOUT_DISPLAY_H_

#include "esp_err.h"

#include <stdint.h>

#define RFSCOUT_DISPLAY_WIDTH   320
#define RFSCOUT_DISPLAY_HEIGHT  170

/**
 * @brief Initialize the T-Embed ST7789 display.
 * @return ESP_OK on success; an ESP-IDF error code on failure.
 */
esp_err_t display_init( void );

/**
 * @brief Fill the local framebuffer with one RGB565 color.
 * @param a_color RGB565 color value.
 */
void display_clear( uint16_t a_color );

/**
 * @brief Draw text using RFScout's compact 5x7 font.
 * @param a_x Left coordinate in pixels.
 * @param a_y Top coordinate in pixels.
 * @param a_text NUL-terminated text.
 * @param a_color RGB565 foreground color.
 * @param a_scale Integer glyph scale; values below one are treated as one.
 */
void display_draw_text( int a_x, int a_y, const char *a_text, uint16_t a_color, unsigned a_scale );

/**
 * @brief Draw a filled rectangle into the framebuffer.
 * @param a_x Left coordinate.
 * @param a_y Top coordinate.
 * @param a_width Rectangle width.
 * @param a_height Rectangle height.
 * @param a_color RGB565 color.
 */
void display_fill_rect( int a_x, int a_y, int a_width, int a_height, uint16_t a_color );

/**
 * @brief Transfer the complete framebuffer to the LCD.
 * @return ESP_OK on success; an ESP-IDF error code on failure.
 */
esp_err_t display_present( void );

#endif // _RFSCOUT_DISPLAY_H_
