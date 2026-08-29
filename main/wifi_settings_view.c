/**
    wifi_settings_view.c : RFScout WiFi/BT application settings view
    Description: Renders app-scoped notification and live-observation actions for WiFi/BT.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "wifi_settings_view.h"

#include "display.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#define WIFI_SETTINGS_ITEM_COUNT 3U

void wifi_settings_view_render( unsigned a_selected, bool a_notifications_enabled )
{
    static const char *labels[ WIFI_SETTINGS_ITEM_COUNT ] = {
        "BACK", "NOTIFICATIONS", "RESET DETECTIONS" };
    const ui_theme_palette_t *theme = ui_theme_get();

    display_clear( theme->background );
    ui_chrome_draw( "WIFI / BT SETTINGS", theme->secondary );

    for( unsigned item = 0U; item < WIFI_SETTINGS_ITEM_COUNT; ++item )
    {
        const int y = UI_LAYOUT_MENU_START_Y + (int)item * UI_LAYOUT_MENU_ROW_HEIGHT;
        const bool active = ( item == a_selected );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           y,
                           active ? ">" : " ",
                           active ? theme->selected : theme->divider,
                           1U );
        display_draw_text(
            UI_LAYOUT_LABEL_X, y, labels[ item ], active ? theme->text : theme->divider, 1U );

        if( 1U == item )
        {
            display_draw_text( 232,
                               y,
                               a_notifications_enabled ? "ON" : "OFF",
                               active ? theme->selected : theme->secondary,
                               1U );
        }
    }

    display_draw_text(
        UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "TURN: SELECT  CENTER: ACT", theme->secondary, 1U );
    (void)display_present();
}
