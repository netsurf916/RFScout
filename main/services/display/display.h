/**
    display.h : RFScout display service
    Description: Provides framebuffer drawing and runtime metadata for the selected display provider.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DISPLAY_H_
#define _RFSCOUT_DISPLAY_H_

#include "board.h"
#include "view.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_DISPLAY_WIDTH RFSCOUT_BOARD_DISPLAY_WIDTH
#define RFSCOUT_DISPLAY_HEIGHT RFSCOUT_BOARD_DISPLAY_HEIGHT

typedef enum
{
    RFSCOUT_DISPLAY_PROVIDER_NONE = 0,
    RFSCOUT_DISPLAY_PROVIDER_ST7789
} rfscout_display_provider_t;

typedef enum
{
    RFSCOUT_DISPLAY_CAP_BRIGHTNESS = 1U << 0,
    RFSCOUT_DISPLAY_CAP_LOW_POWER = 1U << 1
} rfscout_display_capability_t;

typedef struct
{
    rfscout_display_provider_t provider;
    const char *name;
    uint16_t width;
    uint16_t height;
    uint32_t capabilities;
} rfscout_display_info_t;

/** @brief Return runtime metadata for the display provider selected by the board profile. */
const rfscout_display_info_t *display_info( void );
/** @brief Return the rectangle available to the active application after global chrome reservations. */
rfscout_view_t display_view( void );
/** @brief Return the system-owned global navigation rectangle, or an empty view when not reserved. */
rfscout_view_t display_navigation_view( void );

/** @brief Initialize the board-selected display provider and framebuffer. */
esp_err_t display_init( void );
/** @brief Set display brightness from 0 through 100 percent. Zero is reserved for idle blanking. */
void display_set_brightness( uint8_t a_percent );
/** @brief Enable or disable display output without changing the application framebuffer. */
esp_err_t display_set_output_enabled( bool a_enabled );
/** @brief Return whether display output transfers are currently enabled. */
bool display_output_enabled( void );
/** @brief Disable output and enter the selected provider's supported low-power state. */
esp_err_t display_enter_low_power( void );
/** @brief Exit the selected provider's low-power state and restore output. */
esp_err_t display_exit_low_power( void );
/** @brief Return whether the selected display provider is currently in low-power mode. */
bool display_low_power_active( void );
/** @brief Mark the composed display state dirty even when framebuffer pixels did not change. */
void display_invalidate( void );
/** @brief Return whether the display needs another presentation. */
bool display_is_dirty( void );
/** @brief Mark the current composed presentation clean after transient overlay restoration. */
void display_mark_clean( void );

void display_clear( uint16_t a_color );
void display_draw_text( int a_x, int a_y, const char *a_text, uint16_t a_color, unsigned a_scale );
void display_fill_rect( int a_x, int a_y, int a_width, int a_height, uint16_t a_color );
void display_fill_rect_raw( int a_x, int a_y, int a_width, int a_height, uint16_t a_color );
esp_err_t display_draw_mask( int a_x,
                             int a_y,
                             int a_width,
                             int a_height,
                             const uint8_t *a_mask,
                             size_t a_mask_size,
                             uint16_t a_color );
esp_err_t display_read_rect(
    int a_x, int a_y, int a_width, int a_height, uint16_t *a_pixels, size_t a_pixel_capacity );
esp_err_t display_write_rect(
    int a_x, int a_y, int a_width, int a_height, const uint16_t *a_pixels, size_t a_pixel_count );
esp_err_t display_flush( void );
esp_err_t display_present( void );

#endif // _RFSCOUT_DISPLAY_H_
