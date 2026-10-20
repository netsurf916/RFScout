/**
    ui_icon.c : Shared allocation-free RFScout icon artwork
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui_icon.h"

#include "display.h"
#include "ui_theme.h"

/* One byte per row, MSB at the left. All silhouettes are original RFScout art. */
static const uint8_t icons[ UI_GAME_ICON_COUNT ][ 8 ] = {
    [UI_GAME_ICON_SCOUT] = { 0x18, 0x3C, 0x7E, 0x5A, 0x7E, 0x3C, 0x66, 0x42 },
    [UI_GAME_ICON_RADIO] = { 0x81, 0x5A, 0x24, 0x18, 0x18, 0x3C, 0x3C, 0x7E },
    [UI_GAME_ICON_NETWORK] = { 0x7E, 0x81, 0x3C, 0x42, 0x18, 0x24, 0x00, 0x18 },
    [UI_GAME_ICON_TAPE] = { 0x7E, 0xFF, 0xA5, 0xA5, 0xFF, 0xBD, 0xC3, 0x7E },
    [UI_GAME_ICON_THERMAL] = { 0x18, 0x24, 0x24, 0x24, 0x3C, 0x7E, 0x7E, 0x3C },
    [UI_GAME_ICON_AUDIO] = { 0x00, 0x08, 0x28, 0xAA, 0xAA, 0xAA, 0xAA, 0xFF },
    [UI_GAME_ICON_COMPASS] = { 0x3C, 0x42, 0x99, 0x95, 0xA9, 0x99, 0x42, 0x3C },
    [UI_GAME_ICON_DRIVE] = { 0x00, 0x3C, 0x42, 0x7E, 0xFF, 0xBD, 0xFF, 0x66 },
    [UI_GAME_ICON_EVENTS] = { 0x7E, 0x42, 0x5A, 0x42, 0x5A, 0x42, 0x7E, 0x00 },
    [UI_GAME_ICON_SETTINGS] = { 0x24, 0x7E, 0xDB, 0xA5, 0xA5, 0xDB, 0x7E, 0x24 },
    [UI_GAME_ICON_STAR] = { 0x18, 0x18, 0xFF, 0x7E, 0x3C, 0x7E, 0x66, 0x42 },
};

void ui_icon_draw_pixel(
    ui_game_icon_t a_icon, int a_x, int a_y, unsigned a_scale, uint16_t a_color )
{
    if( ( (unsigned)a_icon >= UI_GAME_ICON_COUNT ) || ( 0U == a_scale ) || ( a_scale > 8U ) )
    {
        return;
    }
    for( unsigned row = 0U; row < 8U; ++row )
    {
        for( unsigned column = 0U; column < 8U; ++column )
        {
            if( 0U != ( icons[ a_icon ][ row ] & ( 0x80U >> column ) ) )
            {
                display_fill_rect_raw( a_x + (int)( column * a_scale ),
                                       a_y + (int)( row * a_scale ),
                                       (int)a_scale,
                                       (int)a_scale,
                                       a_color );
            }
        }
    }
}

void ui_icon_draw_gps( int a_x, int a_y, uint16_t a_color )
{
    display_fill_rect( a_x + 4, a_y + 5, 4, 5, a_color );
    display_fill_rect( a_x, a_y + 4, 3, 7, a_color );
    display_fill_rect( a_x + 9, a_y + 4, 3, 7, a_color );
    display_fill_rect( a_x + 3, a_y + 7, 1, 1, a_color );
    display_fill_rect( a_x + 8, a_y + 7, 1, 1, a_color );
    display_fill_rect( a_x + 5, a_y + 3, 1, 2, a_color );
    display_fill_rect( a_x + 6, a_y + 2, 2, 1, a_color );
    display_fill_rect( a_x + 5, a_y + 10, 2, 2, a_color );
}

void ui_icon_draw_lock( int a_x, int a_y, bool a_application_lock, uint16_t a_color )
{
    display_fill_rect( a_x + 3, a_y + 1, 6, 1, a_color );
    display_fill_rect( a_x + 2, a_y + 2, 1, 4, a_color );
    display_fill_rect( a_x + 9, a_y + 2, 1, 4, a_color );
    display_fill_rect( a_x + 1, a_y + 6, 10, 7, a_color );
    if( a_application_lock )
    {
        display_draw_text( a_x + 3, a_y + 7, "A", ui_theme_get()->background, 1U );
    }
    else
    {
        display_fill_rect( a_x + 5, a_y + 8, 2, 3, ui_theme_get()->background );
    }
}

void ui_icon_draw_sd( int a_x, int a_y, uint16_t a_color, bool a_error )
{
    display_fill_rect( a_x, a_y + 2, 1, 11, a_color );
    display_fill_rect( a_x + 1, a_y + 1, 8, 1, a_color );
    display_fill_rect( a_x + 9, a_y + 2, 2, 1, a_color );
    display_fill_rect( a_x + 11, a_y + 3, 1, 10, a_color );
    display_fill_rect( a_x, a_y + 12, 12, 1, a_color );
    display_fill_rect( a_x + 2, a_y + 4, 2, 3, a_color );
    display_fill_rect( a_x + 5, a_y + 4, 2, 3, a_color );
    display_fill_rect( a_x + 8, a_y + 4, 2, 3, a_color );
    if( a_error )
    {
        display_fill_rect( a_x + 3, a_y + 8, 6, 3, ui_theme_get()->background );
        display_fill_rect( a_x + 5, a_y + 8, 2, 2, a_color );
        display_fill_rect( a_x + 5, a_y + 11, 2, 1, a_color );
    }
}

void ui_icon_draw_thermal( int a_x, int a_y, uint16_t a_color )
{
    display_fill_rect( a_x + 1, a_y + 3, 11, 8, a_color );
    display_fill_rect( a_x + 3, a_y + 1, 5, 2, a_color );
    display_fill_rect( a_x + 3, a_y + 5, 7, 4, ui_theme_get()->background );
    display_fill_rect( a_x + 5, a_y + 5, 3, 4, a_color );
}
