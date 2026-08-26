/**
    wifi_settings_view.c : RFScout WiFi/BT application settings view
    Description: Renders configurable Off, Tone, and one-character Morse surveillance alerts.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "wifi_settings_view.h"

#include "alert.h"
#include "display.h"
#include "morse.h"

#include <stdio.h>

#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define WIFI_SETTINGS_ITEM_COUNT 3U

void wifi_settings_view_render( unsigned a_selected, bool a_editing )
{
    static const char *labels[ WIFI_SETTINGS_ITEM_COUNT ] = { "ALERT", "MORSE CHAR", "BACK" };

    display_clear( BG );
    display_draw_text( 8, 7, "WIFI SETTINGS", SUBDUED, 2U );

    for( unsigned item = 0U; item < WIFI_SETTINGS_ITEM_COUNT; ++item )
    {
        const int y = 42 + (int)item * 30;
        const bool active = ( item == a_selected );
        const bool editable = ( item < 2U );
        const char *marker = " ";

        if( active )
        {
            marker = ( editable && a_editing ) ? "*" : ">";
        }

        display_draw_text( 8, y, marker, active ? YELLOW : DIM, 1U );
        display_draw_text( 22, y, labels[ item ], active ? TEXT : DIM, 1U );

        if( 0U == item )
        {
            display_draw_text( 150, y, alert_mode_name( alert_mode() ), active ? YELLOW : DIM, 1U );
        }
        else if( 1U == item )
        {
            char value[ 24 ];
            const char character = alert_morse_character();
            const char *pattern = morse_pattern( character );
            snprintf(
                value, sizeof( value ), "%c  %s", character, ( NULL != pattern ) ? pattern : "?" );
            display_draw_text( 150, y, value, active ? YELLOW : DIM, 1U );
        }
    }

    display_draw_text( 8,
                       150,
                       a_editing ? "TURN: CHANGE  CENTER: DONE" : "TURN: SELECT CENTER: ENTER",
                       a_editing ? YELLOW : SUBDUED,
                       1U );
    (void)display_present();
}
