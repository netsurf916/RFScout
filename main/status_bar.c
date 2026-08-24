/**
    status_bar.c : RFScout top status bar implementation
    Description: Renders compact right-aligned state and error icons.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "status_bar.h"

#include "display.h"
#include "log_writer.h"
#include "mlx90640.h"
#include "storage.h"

#include <stdbool.h>
#include <stdint.h>

#define STATUS_BAR_RIGHT_X 235
#define STATUS_BAR_ICON_Y 4
#define STATUS_BAR_ICON_WIDTH 14
#define STATUS_BAR_ICON_HEIGHT 14
#define STATUS_BAR_ICON_GAP 4

#define STATUS_BAR_COLOR_NORMAL 0x03F0U
#define STATUS_BAR_COLOR_ERROR 0xF800U
#define STATUS_BAR_COLOR_BACKGROUND 0x0000U

/** @brief Draw a compact SD-card glyph using the supplied subdued/error color. */
static void draw_sd_icon( int a_x, uint16_t a_color )
{
    const int y = STATUS_BAR_ICON_Y;

    display_fill_rect( a_x, y + 2, 1, 11, a_color );
    display_fill_rect( a_x + 1, y + 1, 8, 1, a_color );
    display_fill_rect( a_x + 9, y + 2, 2, 1, a_color );
    display_fill_rect( a_x + 11, y + 3, 1, 10, a_color );
    display_fill_rect( a_x, y + 12, 12, 1, a_color );
    display_fill_rect( a_x + 2, y + 4, 2, 3, a_color );
    display_fill_rect( a_x + 5, y + 4, 2, 3, a_color );
    display_fill_rect( a_x + 8, y + 4, 2, 3, a_color );

    if( log_writer_has_error() )
    {
        display_fill_rect( a_x + 3,
                           y + 8,
                           6,
                           3,
                           STATUS_BAR_COLOR_BACKGROUND );
        display_fill_rect( a_x + 5, y + 8, 2, 2, a_color );
        display_fill_rect( a_x + 5, y + 11, 2, 1, a_color );
    }
}

/** @brief Draw a subdued thermal-camera glyph when the MLX90640 is available. */
static void draw_thermal_icon( int a_x )
{
    const int y = STATUS_BAR_ICON_Y;
    const uint16_t color = STATUS_BAR_COLOR_NORMAL;

    display_fill_rect( a_x + 1, y + 3, 11, 8, color );
    display_fill_rect( a_x + 3, y + 1, 5, 2, color );
    display_fill_rect( a_x + 3, y + 5, 7, 4, STATUS_BAR_COLOR_BACKGROUND );
    display_fill_rect( a_x + 5, y + 5, 3, 4, color );
}

void status_bar_draw( void )
{
    int x = STATUS_BAR_RIGHT_X - STATUS_BAR_ICON_WIDTH;

    if( storage_is_available() )
    {
        const uint16_t color = log_writer_has_error()
                                   ? STATUS_BAR_COLOR_ERROR
                                   : STATUS_BAR_COLOR_NORMAL;
        draw_sd_icon( x, color );
        x -= STATUS_BAR_ICON_WIDTH + STATUS_BAR_ICON_GAP;
    }

    if( mlx90640_is_detected() )
    {
        draw_thermal_icon( x );
        x -= STATUS_BAR_ICON_WIDTH + STATUS_BAR_ICON_GAP;
    }

    (void)x;
}
