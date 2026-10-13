/**
    wifi_settings_view.c : RFScout WiFi/BT application settings view
    Description: Renders app-scoped notification, retention, and live-observation actions for WiFi/BT.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "wifi_settings_view.h"

#include "display.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stdio.h>

#define WIFI_SETTINGS_ITEM_COUNT 8U

void wifi_settings_view_render( unsigned a_selected,
                                bool a_editing,
                                bool a_app_locked,
                                bool a_notifications_enabled,
                                rfscout_evidence_strength_t a_threshold,
                                uint16_t a_wifi_timeout_seconds,
                                uint16_t a_ble_timeout_seconds )
{
    static const char *labels[ WIFI_SETTINGS_ITEM_COUNT ] = { "Back",
                                                              "App lock",
                                                              "Reset detections",
                                                              "Notifications",
                                                              "Evidence threshold",
                                                              "WiFi timeout",
                                                              "BLE timeout",
                                                              "Sources" };
    const ui_theme_palette_t *theme = ui_theme_get();

    display_clear( theme->background );
    ui_chrome_draw( "WiFi / BLE settings", theme->secondary );

    /* Keep the selected row visible on compact displays. */
    const unsigned first = ( a_selected >= 4U ) ? a_selected - 3U : 0U;
    const unsigned end =
        ( first + 5U < WIFI_SETTINGS_ITEM_COUNT ) ? first + 5U : WIFI_SETTINGS_ITEM_COUNT;
    for( unsigned item = first; item < end; ++item )
    {
        const int y = UI_LAYOUT_MENU_START_Y + (int)( item - first ) * UI_LAYOUT_MENU_ROW_HEIGHT;
        const bool active = ( item == a_selected );
        const bool editable = ( item >= 5U ) && ( item <= 6U );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           y,
                           active ? ( a_editing && editable ? "*" : ">" ) : " ",
                           active ? theme->selected : theme->divider,
                           1U );
        display_draw_text(
            UI_LAYOUT_LABEL_X, y, labels[ item ], active ? theme->text : theme->divider, 1U );

        char value[ 16 ];
        value[ 0 ] = '\0';
        if( 1U == item )
        {
            snprintf( value, sizeof( value ), "%s", a_app_locked ? "On" : "Off" );
        }
        else if( 3U == item )
        {
            snprintf( value, sizeof( value ), "%s", a_notifications_enabled ? "On" : "Off" );
        }
        else if( 4U == item )
        {
            static const char *names[ RFSCOUT_EVIDENCE_STRENGTH_COUNT ] = {
                [RFSCOUT_EVIDENCE_STRENGTH_EVERYTHING] = "Everything",
                [RFSCOUT_EVIDENCE_STRENGTH_POSSIBLE] = "Possible",
                [RFSCOUT_EVIDENCE_STRENGTH_PROBABLE] = "Probable",
                [RFSCOUT_EVIDENCE_STRENGTH_DISTINCTIVE] = "Distinctive",
            };
            snprintf( value,
                      sizeof( value ),
                      "%s",
                      ( a_threshold < RFSCOUT_EVIDENCE_STRENGTH_COUNT ) ? names[ a_threshold ]
                                                                        : "Possible" );
        }
        else if( 5U == item )
        {
            snprintf( value, sizeof( value ), "%u s", (unsigned)a_wifi_timeout_seconds );
        }
        else if( 6U == item )
        {
            snprintf( value, sizeof( value ), "%u s", (unsigned)a_ble_timeout_seconds );
        }

        if( '\0' != value[ 0 ] )
        {
            display_draw_text( 232, y, value, active ? theme->selected : theme->secondary, 1U );
        }
    }

    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       a_editing ? "TURN: CHANGE  CENTER: DONE" : "TURN: SELECT  CENTER: ACT",
                       a_editing ? theme->selected : theme->secondary,
                       1U );
    (void)display_present();
}
