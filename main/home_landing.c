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
#define HOME_CONTROL_APPS_Y 43
#define HOME_CONTROL_SETTINGS_Y 70

/** @brief Draw a compact circular arrow pair for rotary application navigation. */
static void draw_rotate_icon( int a_x, int a_y )
{
    const uint16_t color = ui_theme_get()->logo_primary;

    /* Upper/right sweep and arrowhead. */
    display_fill_rect( a_x + 7, a_y + 2, 13, 2, color );
    display_fill_rect( a_x + 18, a_y + 4, 5, 2, color );
    display_fill_rect( a_x + 21, a_y + 6, 4, 2, color );
    display_fill_rect( a_x + 23, a_y + 8, 2, 5, color );
    display_fill_rect( a_x + 20, a_y + 8, 5, 2, color );
    display_fill_rect( a_x + 23, a_y + 8, 2, 5, color );

    /* Lower/left sweep and opposing arrowhead. */
    display_fill_rect( a_x + 5, a_y + 15, 2, 5, color );
    display_fill_rect( a_x + 7, a_y + 19, 5, 2, color );
    display_fill_rect( a_x + 10, a_y + 21, 13, 2, color );
    display_fill_rect( a_x + 3, a_y + 12, 2, 6, color );
    display_fill_rect( a_x + 3, a_y + 16, 5, 2, color );
}

/** @brief Draw concentric rings representing the encoder's center push button. */
static void draw_center_button_icon( int a_x, int a_y )
{
    const uint16_t outer = ui_theme_get()->logo_primary;
    const uint16_t inner = ui_theme_get()->logo_secondary;

    /* Pixel-friendly circular outlines rather than rectangular encoder artwork. */
    display_fill_rect( a_x + 8, a_y + 2, 12, 2, outer );
    display_fill_rect( a_x + 4, a_y + 4, 4, 2, outer );
    display_fill_rect( a_x + 20, a_y + 4, 4, 2, outer );
    display_fill_rect( a_x + 2, a_y + 6, 2, 16, outer );
    display_fill_rect( a_x + 24, a_y + 6, 2, 16, outer );
    display_fill_rect( a_x + 4, a_y + 22, 4, 2, outer );
    display_fill_rect( a_x + 20, a_y + 22, 4, 2, outer );
    display_fill_rect( a_x + 8, a_y + 24, 12, 2, outer );

    display_fill_rect( a_x + 10, a_y + 8, 8, 2, inner );
    display_fill_rect( a_x + 8, a_y + 10, 2, 8, inner );
    display_fill_rect( a_x + 18, a_y + 10, 2, 8, inner );
    display_fill_rect( a_x + 10, a_y + 18, 8, 2, inner );
}

/** @brief Draw the two compact Home-screen control affordances above the logo detail. */
static void draw_home_controls( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    draw_rotate_icon( HOME_CONTROL_ICON_X, HOME_CONTROL_APPS_Y - 7 );
    display_draw_text(
        HOME_CONTROL_TEXT_X, HOME_CONTROL_APPS_Y, "APPS", theme->logo_secondary, 1U );

    draw_center_button_icon( HOME_CONTROL_ICON_X, HOME_CONTROL_SETTINGS_Y - 8 );
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
