/**
    wifi_settings_view.c : RFScout WiFi/BT application settings view
    Description: Renders Back and live-observation reset actions for WiFi/BT.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "wifi_settings_view.h"

#include "display.h"

#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define WIFI_SETTINGS_ITEM_COUNT 2U

void wifi_settings_view_render( unsigned a_selected, bool a_editing )
{
    static const char *labels[ WIFI_SETTINGS_ITEM_COUNT ] = { "BACK", "RESET DETECTIONS" };

    (void)a_editing;
    display_clear( BG );
    display_draw_text( 8, 7, "WIFI / BT SETTINGS", SUBDUED, 2U );

    for( unsigned item = 0U; item < WIFI_SETTINGS_ITEM_COUNT; ++item )
    {
        const int y = 48 + (int)item * 32;
        const bool active = ( item == a_selected );
        display_draw_text( 8, y, active ? ">" : " ", active ? YELLOW : DIM, 1U );
        display_draw_text( 22, y, labels[ item ], active ? TEXT : DIM, 1U );
    }

    display_draw_text( 8, 150, "TURN: SELECT  CENTER: ACT", SUBDUED, 1U );
    (void)display_present();
}
