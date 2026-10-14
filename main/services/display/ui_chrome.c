/**
    ui_chrome.c : RFScout shared application chrome implementation
    Description: Draws the standard title, global status bar, and battery indicator.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui_chrome.h"

#include "battery_indicator.h"
#include "display.h"
#include "status_bar.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

#define UI_CHROME_TITLE_SCALE_LARGE 2U
#define UI_CHROME_TITLE_SCALE_COMPACT 1U
#define UI_CHROME_FONT_CELL_WIDTH 6
#define UI_CHROME_STATUS_SAFE_X 142
#define UI_CHROME_COMPACT_TITLE_Y 8

void ui_chrome_draw( const char *a_title, uint16_t a_title_color )
{
    const bool game = ui_game_active();
    const int title_x = game ? 26 : UI_LAYOUT_TITLE_X;
    if( game )
    {
        const ui_theme_palette_t *theme = ui_theme_get();
        if( NULL != a_title && 0 == strcmp( a_title, "GPS" ) )
        {
            ui_game_draw_gps_satellite( 8, 2, theme->accent );
        }
        else
        {
            ui_game_draw_icon( ui_game_icon_for_title( a_title ), 6, 3, 2U, theme->accent );
        }
        display_fill_rect_raw( 0, 20, RFSCOUT_DISPLAY_WIDTH, 2, theme->divider );
        display_fill_rect_raw( 6, RFSCOUT_DISPLAY_HEIGHT - 2, 12, 1, theme->divider );
        display_fill_rect_raw(
            RFSCOUT_DISPLAY_WIDTH - 18, RFSCOUT_DISPLAY_HEIGHT - 2, 12, 1, theme->divider );
    }
    if( NULL != a_title )
    {
        char bounded_title[ 24 ];
        const int title_capacity =
            ( UI_CHROME_STATUS_SAFE_X - title_x ) / UI_CHROME_FONT_CELL_WIDTH;
        snprintf( bounded_title, sizeof( bounded_title ), "%.*s", title_capacity, a_title );
        const char *title = bounded_title;
        const size_t title_length = strlen( title );
        const int large_width =
            (int)title_length * UI_CHROME_FONT_CELL_WIDTH * (int)UI_CHROME_TITLE_SCALE_LARGE;
        const bool compact = ( title_x + large_width ) >= UI_CHROME_STATUS_SAFE_X;
        const unsigned scale =
            compact ? UI_CHROME_TITLE_SCALE_COMPACT : UI_CHROME_TITLE_SCALE_LARGE;
        const int y = compact ? UI_CHROME_COMPACT_TITLE_Y : UI_LAYOUT_TITLE_Y;
        display_draw_text( title_x, y, title, game ? ui_theme_get()->text : a_title_color, scale );
    }
    status_bar_draw();
    battery_indicator_draw();
}
