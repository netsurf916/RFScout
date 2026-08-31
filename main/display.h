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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_DISPLAY_WIDTH 320
#define RFSCOUT_DISPLAY_HEIGHT 170

/**
 * @brief Initialize the T-Embed ST7789 display.
 * @return ESP_OK on success; an ESP-IDF error code on failure.
 */
esp_err_t display_init( void );

/** @brief Set LCD backlight brightness from 0 (off) through 100 percent. */
void display_set_brightness( uint8_t a_percent );

/** @brief Mark the composed LCD state dirty even when framebuffer pixels did not change. */
void display_invalidate( void );

/** @brief Return whether the LCD needs another presentation. */
bool display_is_dirty( void );

/**
 * @brief Mark the current composed presentation clean after transient overlay restoration.
 * @note Intended for the shared presentation layer after a successful LCD flush.
 */
void display_mark_clean( void );

/**
 * @brief Fill the local framebuffer with one themed RGB565 color and mark it dirty.
 *
 * Historical RFScout UI constants are translated through the active semantic theme. Use
 * display_write_rect() or display_fill_rect_raw() for data imagery whose pixel values must remain
 * literal.
 */
void display_clear( uint16_t a_color );

/**
 * @brief Draw text using RFScout's compact 5x7 font and mark the framebuffer dirty.
 * @param a_x Left coordinate in pixels.
 * @param a_y Top coordinate in pixels.
 * @param a_text NUL-terminated text.
 * @param a_color RGB565 foreground color or historical RFScout UI color token.
 * @param a_scale Integer glyph scale; values below one are treated as one.
 */
void display_draw_text( int a_x, int a_y, const char *a_text, uint16_t a_color, unsigned a_scale );

/** @brief Draw a theme-aware filled rectangle and mark the framebuffer dirty. */
void display_fill_rect( int a_x, int a_y, int a_width, int a_height, uint16_t a_color );

/**
 * @brief Draw a literal RGB565 rectangle without semantic theme translation and mark it dirty.
 *
 * Intended for sensor/data imagery such as thermal maps, not application chrome.
 */
void display_fill_rect_raw( int a_x, int a_y, int a_width, int a_height, uint16_t a_color );

/**
 * @brief Draw a theme-aware one-bit glyph without touching framebuffer pixels under zero bits.
 *
 * Mask rows are packed MSB-first with a stride of `(a_width + 7) / 8` bytes. Set bits write the
 * resolved foreground color; clear bits are transparent and leave the existing framebuffer intact.
 * This is intended for small overlay glyphs rather than full-screen imagery.
 *
 * @param a_x Left coordinate in pixels.
 * @param a_y Top coordinate in pixels.
 * @param a_width Mask width in pixels.
 * @param a_height Mask height in pixels.
 * @param a_mask Packed one-bit row-major mask.
 * @param a_mask_size Number of bytes available in @p a_mask.
 * @param a_color RGB565 foreground color or historical RFScout UI color token.
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG for invalid dimensions or mask storage.
 */
esp_err_t display_draw_mask( int a_x,
                             int a_y,
                             int a_width,
                             int a_height,
                             const uint8_t *a_mask,
                             size_t a_mask_size,
                             uint16_t a_color );

/**
 * @brief Copy a framebuffer rectangle into a caller-provided RGB565 buffer.
 * @param a_x Left coordinate.
 * @param a_y Top coordinate.
 * @param a_width Rectangle width.
 * @param a_height Rectangle height.
 * @param a_pixels Destination buffer in row-major order.
 * @param a_pixel_capacity Number of RGB565 pixels available in @p a_pixels.
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG for an invalid rectangle or buffer.
 */
esp_err_t display_read_rect(
    int a_x, int a_y, int a_width, int a_height, uint16_t *a_pixels, size_t a_pixel_capacity );

/**
 * @brief Write literal RGB565 pixels into the framebuffer and mark it dirty.
 * @param a_x Left coordinate.
 * @param a_y Top coordinate.
 * @param a_width Rectangle width.
 * @param a_height Rectangle height.
 * @param a_pixels Source buffer in row-major order.
 * @param a_pixel_count Number of RGB565 pixels available in @p a_pixels.
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG for an invalid rectangle or buffer.
 */
esp_err_t display_write_rect(
    int a_x, int a_y, int a_width, int a_height, const uint16_t *a_pixels, size_t a_pixel_count );

/**
 * @brief Transfer the framebuffer to the LCD only when its composed state is dirty.
 * @return ESP_OK when already clean or after a successful transfer; an ESP-IDF error on failure.
 */
esp_err_t display_flush( void );

/**
 * @brief Present the framebuffer with RFScout's global UI overlays.
 *
 * Implemented by the shared UI presentation layer so application views can use one periodic
 * presentation entry point. Unchanged frames are skipped by the display dirty-state machinery.
 * The global display-idle policy may also defer LCD transfer while preserving the dirty logical
 * framebuffer for presentation on wake.
 *
 * @return ESP_OK when already clean or presentation succeeds; an ESP-IDF error otherwise.
 */
esp_err_t display_present( void );

#endif // _RFSCOUT_DISPLAY_H_
