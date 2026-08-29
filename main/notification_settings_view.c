/**
    notification_settings_view.c : RFScout application notification settings view
    Description: Renders per-app notification enablement and Morse mappings.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "notification_settings_view.h"

#include "device_config.h"
#include "display.h"
#include "morse.h"
#include "notification.h"
#include "notification_preferences.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stdio.h>

#define NOTIFICATION_SETTINGS_ROWS_PER_PAGE 6U
#define NOTIFICATION_SETTINGS_FIXED_ITEMS 2U
#define NOTIFICATION_SETTINGS_BACK 0U
#define NOTIFICATION_SETTINGS_ENABLED 1U

static const char *app_title( rfscout_app_id_t a_app )
{
    switch( a_app )
    {
        case RFSCOUT_APP_WIFI_BT:
            return "WIFI / BT ALERTS";
        case RFSCOUT_APP_CC1101:
            return "CC1101 ALERTS";
        case RFSCOUT_APP_NRF24:
            return "nRF24 ALERTS";
        default:
            return "APP ALERTS";
    }
}

void notification_settings_view_render( rfscout_app_id_t a_app,
                                        unsigned a_selected,
                                        bool a_editing )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const size_t notification_count = notification_count_for_app( a_app );
    const unsigned item_count = NOTIFICATION_SETTINGS_FIXED_ITEMS + (unsigned)notification_count;
    const unsigned page_start =
        ( a_selected / NOTIFICATION_SETTINGS_ROWS_PER_PAGE ) * NOTIFICATION_SETTINGS_ROWS_PER_PAGE;

    display_clear( theme->background );
    ui_chrome_draw( app_title( a_app ), theme->secondary );

    for( unsigned row = 0U; row < NOTIFICATION_SETTINGS_ROWS_PER_PAGE; ++row )
    {
        const unsigned item = page_start + row;
        if( item >= item_count )
        {
            break;
        }

        const int y = UI_LAYOUT_MENU_START_Y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT;
        const bool active = ( item == a_selected );
        const uint16_t foreground = active ? theme->text : theme->secondary;
        display_draw_text( UI_LAYOUT_LEFT_X,
                           y,
                           active ? ( a_editing ? "*" : ">" ) : " ",
                           active ? theme->selected : theme->divider,
                           1U );

        if( NOTIFICATION_SETTINGS_BACK == item )
        {
            display_draw_text( UI_LAYOUT_LABEL_X, y, "BACK", foreground, 1U );
            continue;
        }
        if( NOTIFICATION_SETTINGS_ENABLED == item )
        {
            display_draw_text( UI_LAYOUT_LABEL_X, y, "ENABLED", foreground, 1U );
            display_draw_text( 232,
                               y,
                               notification_preferences_app_enabled( a_app ) ? "ON" : "OFF",
                               active ? theme->selected : theme->secondary,
                               1U );
            continue;
        }

        const rfscout_notification_descriptor_t *descriptor =
            notification_descriptor_for_app( a_app, item - NOTIFICATION_SETTINGS_FIXED_ITEMS );
        if( NULL == descriptor )
        {
            continue;
        }

        const char character = device_config_notification_character( descriptor->type );
        const char *pattern = morse_pattern( character );
        char value[ 24 ];
        snprintf( value,
                  sizeof( value ),
                  "%c %s",
                  character,
                  ( NULL != pattern ) ? pattern : "?" );
        display_draw_text( UI_LAYOUT_LABEL_X, y, descriptor->label, foreground, 1U );
        display_draw_text(
            232, y, value, active ? theme->selected : theme->secondary, 1U );
    }

    char footer[ 64 ];
    snprintf( footer,
              sizeof( footer ),
              "%u/%u  %s",
              (unsigned)( a_selected + 1U ),
              item_count,
              a_editing ? "TURN CHARACTER / CENTER DONE" : "TURN SELECT / CENTER ENTER" );
    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       footer,
                       a_editing ? theme->selected : theme->secondary,
                       1U );
    (void)display_present();
}
