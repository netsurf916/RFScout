/**
    status_bar.c : RFScout top status bar implementation
    Description: Renders compact right-aligned state and error icons.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "status_bar.h"

#include "display.h"
#include "log_writer.h"
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

    /* Card body with the familiar clipped upper-right corner. */
    display_fill_rect( a_x, y + 2, 1, 11, a_color );
    display_fill_rect( a_x + 1, y + 1, 8, 1, a_color );
    display_fill_rect( a_x + 9, y + 2, 2, 1, a_color );
    display_fill_rect( a_x + 11, y + 3, 1, 10, a_color );
    display_fill_rect( a_x, y + 12, 12, 1, a_color );

    /* Three contacts distinguish the tiny glyph from a generic document. */
    display_fill_rect( a_x + 2, y + 4, 2, 3, a_color );
    display_fill_rect( a_x + 5, y + 4, 2, 3, a_color );
    display_fill_rect( a_x + 8, y + 4, 2, 3, a_color );

    if( log_writer_has_error() )
    {
        /* A dark center keeps the red error mark readable inside the outline. */
        display_fill_rect( a_x + 3,
                           y + 8,
                           6,
                           3,
                           STATUS_BAR_COLOR_BACKGROUND );
        display_fill_rect( a_x + 5, y + 8, 2, 2, a_color );
        display_fill_rect( a_x + 5, y + 11, 2, 1, a_color );
    }
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

    /* Future status icons consume x from right to left here. */
    (void)x;
}
