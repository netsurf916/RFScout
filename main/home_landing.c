/**
    home_landing.c : RFScout branded Home landing screen
    Description: Renders the canonical RFScout logo from the generated compact LCD bitmap.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_view.h"

#include "display.h"
#include "home_logo_bitmap.h"
#include "ui_chrome.h"
#include "ui_theme.h"

#include <stddef.h>
#include <stdint.h>

#define HOME_LOGO_X ( ( RFSCOUT_DISPLAY_WIDTH - HOME_LOGO_WIDTH ) / 2 )
#define HOME_LOGO_Y 27
#define HOME_CONTROL_ICON_X 12
#define HOME_CONTROL_TEXT_X 48
#define HOME_CONTROL_APPS_Y 59
#define HOME_CONTROL_SETTINGS_Y 101

/** @brief Draw a small outlined rectangle using the display fill primitive. */
static void draw_outline_rect( int a_x, int a_y, int a_width, int a_height, uint16_t a_color )
{
    display_fill_rect( a_x, a_y, a_width, 2, a_color );
    display_fill_rect( a_x, a_y + a_height - 2, a_width, 2, a_color );
    display_fill_rect( a_x, a_y, 2, a_height, a_color );
    display_fill_rect( a_x + a_width - 2, a_y, 2, a_height, a_color );
}

/** @brief Draw a compact rotary-encoder glyph for application navigation. */
static void draw_rotate_icon( int a_x, int a_y )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    draw_outline_rect( a_x + 8, a_y + 6, 14, 14, theme->logo_primary );
    display_fill_rect( a_x + 14, a_y + 8, 2, 5, theme->logo_secondary );

    display_fill_rect( a_x, a_y + 12, 6, 2, theme->logo_primary );
    display_fill_rect( a_x, a_y + 10, 2, 6, theme->logo_primary );
    display_fill_rect( a_x + 24, a_y + 12, 6, 2, theme->logo_primary );
    display_fill_rect( a_x + 28, a_y + 10, 2, 6, theme->logo_primary );
}

/** @brief Draw a compact center-button glyph for entering Device Settings. */
static void draw_center_button_icon( int a_x, int a_y )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    draw_outline_rect( a_x + 6, a_y + 4, 18, 18, theme->logo_primary );
    display_fill_rect( a_x + 11, a_y + 9, 8, 8, theme->logo_secondary );
}

/** @brief Draw the two Home-screen control affordances over the left side of the main area. */
static void draw_home_controls( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    draw_rotate_icon( HOME_CONTROL_ICON_X, HOME_CONTROL_APPS_Y - 7 );
    display_draw_text(
        HOME_CONTROL_TEXT_X, HOME_CONTROL_APPS_Y, "APPS", theme->logo_secondary, 1U );

    draw_center_button_icon( HOME_CONTROL_ICON_X, HOME_CONTROL_SETTINGS_Y - 7 );
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
    draw_home_bitmap();
    draw_home_controls();
    (void)display_present();
}
