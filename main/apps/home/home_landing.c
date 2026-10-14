/**
    home_landing.c : RFScout branded Home landing screen
    Description: Renders the Pixel Quest title screen or the canonical compact LCD logo.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_view.h"

#include "display.h"
#include "home_logo_bitmap.h"
#include "ui_chrome.h"
#include "ui_theme.h"
#include "ui_game.h"

#include <stddef.h>
#include <stdint.h>

#define HOME_LOGO_X ( ( RFSCOUT_DISPLAY_WIDTH - HOME_LOGO_WIDTH ) / 2 )
#define HOME_LOGO_Y 27
#define HOME_CONTROL_ICON_X 12
#define HOME_CONTROL_TEXT_X 48
#define HOME_CONTROL_APPS_Y 43
#define HOME_CONTROL_SETTINGS_Y 70

/** @brief Draw solid opposing triangles representing rotary application navigation. */
static void draw_apps_icon( int a_x, int a_y )
{
    const uint16_t color = ui_theme_get()->logo_primary;

    /* Left-pointing triangle. */
    for( int row = 0; row < 9; ++row )
    {
        const int half_width = ( row <= 4 ) ? row : 8 - row;
        display_fill_rect( a_x + 6 - half_width, a_y + row * 2, half_width + 1, 2, color );
    }

    /* Right-pointing triangle. */
    for( int row = 0; row < 9; ++row )
    {
        const int half_width = ( row <= 4 ) ? row : 8 - row;
        display_fill_rect( a_x + 20, a_y + row * 2, half_width + 1, 2, color );
    }
}

/** @brief Draw a small round filled circle representing the encoder center push button. */
static void draw_center_button_icon( int a_x, int a_y )
{
    const uint16_t color = ui_theme_get()->logo_primary;
    static const uint8_t widths[] = { 4U, 8U, 10U, 12U, 12U, 12U, 12U, 12U, 12U, 10U, 8U, 4U };

    for( size_t row = 0U; row < sizeof( widths ); ++row )
    {
        const int width = widths[ row ];
        display_fill_rect( a_x + ( 12 - width ) / 2, a_y + (int)row, width, 1, color );
    }
}

/** @brief Draw the two compact Home-screen control affordances above the logo detail. */
static void draw_home_controls( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    draw_apps_icon( HOME_CONTROL_ICON_X, HOME_CONTROL_APPS_Y - 6 );
    display_draw_text(
        HOME_CONTROL_TEXT_X, HOME_CONTROL_APPS_Y, "APPS", theme->logo_secondary, 1U );

    /* Keep the smaller Settings circle centered on the same icon column and its text row. */
    draw_center_button_icon( HOME_CONTROL_ICON_X + 7, HOME_CONTROL_SETTINGS_Y - 2 );
    display_draw_text(
        HOME_CONTROL_TEXT_X, HOME_CONTROL_SETTINGS_Y, "SETTINGS", theme->logo_secondary, 1U );
}

/** @brief Expand and draw the packed 2-bit brand bitmap one scanline at a time. */
static void draw_home_bitmap( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const uint16_t palette[] = {
        theme->background,
        theme->logo_primary,
        theme->logo_secondary,
        theme->background,
    };
    uint16_t row[ HOME_LOGO_WIDTH ];

    for( int y = 0; y < HOME_LOGO_HEIGHT; ++y )
    {
        const uint8_t *packed = &home_logo_bitmap[ y * HOME_LOGO_BYTES_PER_ROW ];
        for( int x = 0; x < HOME_LOGO_WIDTH; ++x )
        {
            const uint8_t value = ( packed[ x / 4 ] >> ( ( x % 4 ) * 2 ) ) & 0x03U;
            row[ x ] = palette[ value ];
        }
        (void)display_write_rect(
            HOME_LOGO_X, HOME_LOGO_Y + y, HOME_LOGO_WIDTH, 1, row, HOME_LOGO_WIDTH );
    }
}

void device_view_landing( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "HOME", theme->secondary );
    if( ui_game_active() )
    {
        ui_game_draw_home();
    }
    else
    {
        draw_home_bitmap();
        draw_home_controls();
    }
    (void)display_present();
}
