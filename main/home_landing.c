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
#include "ui_layout.h"

#include <stddef.h>
#include <stdint.h>

#define HOME_COLOR_BACKGROUND 0x0000U
#define HOME_COLOR_ORANGE 0xFC00U
#define HOME_COLOR_WHITE 0xFFFFU
#define HOME_COLOR_DIM 0x7BEFU
#define HOME_LOGO_X ( ( RFSCOUT_DISPLAY_WIDTH - HOME_LOGO_WIDTH ) / 2 )
#define HOME_LOGO_Y 27

/** @brief Expand and draw the packed 2-bit brand bitmap one scanline at a time. */
static void draw_home_bitmap( void )
{
    static const uint16_t palette[] = {
        HOME_COLOR_BACKGROUND,
        HOME_COLOR_ORANGE,
        HOME_COLOR_WHITE,
        HOME_COLOR_BACKGROUND,
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
        (void)display_write_rect( HOME_LOGO_X, HOME_LOGO_Y + y, HOME_LOGO_WIDTH, 1, row, HOME_LOGO_WIDTH );
    }
}

void device_view_landing( void )
{
    display_clear( HOME_COLOR_BACKGROUND );
    ui_chrome_draw( "HOME", HOME_COLOR_DIM );
    draw_home_bitmap();
    display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "TURN: APPS  CENTER: SETTINGS", HOME_COLOR_DIM, 1U );
    (void)display_present();
}
