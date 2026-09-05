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
#include "ui_layout.h"

#include <string.h>

#define UI_CHROME_TITLE_SCALE_LARGE 2U
#define UI_CHROME_TITLE_SCALE_COMPACT 1U
#define UI_CHROME_FONT_CELL_WIDTH 6
#define UI_CHROME_STATUS_SAFE_X 142
#define UI_CHROME_COMPACT_TITLE_Y 8

void ui_chrome_draw( const char *a_title, uint16_t a_title_color )
{
    if( NULL != a_title )
    {
        const size_t title_length = strlen( a_title );
        const int large_width =
            (int)title_length * UI_CHROME_FONT_CELL_WIDTH * (int)UI_CHROME_TITLE_SCALE_LARGE;
        const bool compact = ( UI_LAYOUT_TITLE_X + large_width ) >= UI_CHROME_STATUS_SAFE_X;
        const unsigned scale =
            compact ? UI_CHROME_TITLE_SCALE_COMPACT : UI_CHROME_TITLE_SCALE_LARGE;
        const int y = compact ? UI_CHROME_COMPACT_TITLE_Y : UI_LAYOUT_TITLE_Y;
        display_draw_text( UI_LAYOUT_TITLE_X, y, a_title, a_title_color, scale );
    }
    status_bar_draw();
    battery_indicator_draw();
}
