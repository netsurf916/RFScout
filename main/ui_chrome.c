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

void ui_chrome_draw( const char *a_title, uint16_t a_title_color )
{
    if( NULL != a_title )
    {
        display_draw_text( UI_LAYOUT_TITLE_X, UI_LAYOUT_TITLE_Y, a_title, a_title_color, 2U );
    }
    status_bar_draw();
    battery_indicator_draw();
}
