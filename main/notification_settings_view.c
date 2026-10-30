/**
    notification_settings_view.c : RFScout application alert settings view
    Description: Renders per-app alert enablement and audio/Morse mappings.
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
#include "ui_menu.h"

#include <stdio.h>

#define NOTIFICATION_SETTINGS_ROWS_PER_PAGE 6U
#define NOTIFICATION_SETTINGS_FIXED_ITEMS 2U
#define NOTIFICATION_SETTINGS_BACK 0U
#define NOTIFICATION_SETTINGS_ENABLED 1U
#define NOTIFICATION_SETTINGS_WIFI_EVENT_COUNT 2U

static unsigned mapping_base( rfscout_app_id_t a_app )
{
    return NOTIFICATION_SETTINGS_FIXED_ITEMS +
           ( ( RFSCOUT_APP_WIFI_BT == a_app ) ? NOTIFICATION_SETTINGS_WIFI_EVENT_COUNT : 0U );
}

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
    const unsigned first_mapping = mapping_base( a_app );
    const unsigned item_count = first_mapping + (unsigned)notification_count;
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
        const char *label = NULL;
        char value[ 24 ];
        value[ 0 ] = '\0';

        if( NOTIFICATION_SETTINGS_BACK == item )
        {
            label = "BACK";
        }
        else if( NOTIFICATION_SETTINGS_ENABLED == item )
        {
            label = "ENABLED";
            snprintf( value,
                      sizeof( value ),
                      "%s",
                      notification_preferences_app_enabled( a_app ) ? "ON" : "OFF" );
        }
        else if( ( RFSCOUT_APP_WIFI_BT == a_app ) &&
                 ( item >= NOTIFICATION_SETTINGS_FIXED_ITEMS ) && ( item < first_mapping ) )
        {
            const rfscout_wifi_detection_event_t event =
                (rfscout_wifi_detection_event_t)( item - NOTIFICATION_SETTINGS_FIXED_ITEMS );
            label =
                ( RFSCOUT_WIFI_DETECTION_WIFI_DEVICE == event ) ? "WIFI DEVICES" : "BLE DEVICES";
            snprintf( value,
                      sizeof( value ),
                      "%s",
                      notification_preferences_wifi_detection_enabled( event ) ? "ON" : "OFF" );
        }
        else
        {
            const rfscout_notification_descriptor_t *descriptor =
                notification_descriptor_for_app( a_app, item - first_mapping );
            if( NULL != descriptor )
            {
                const char character = device_config_notification_character( descriptor->type );
                label = descriptor->label;
                if( ' ' == character )
                {
                    snprintf( value, sizeof( value ), "OFF" );
                }
                else
                {
                    const char *pattern = morse_pattern( character );
                    snprintf( value,
                              sizeof( value ),
                              "%c %s",
                              character,
                              ( NULL != pattern ) ? pattern : "?" );
                }
            }
        }

        ui_menu_draw_row(
            y,
            label,
            NULL,
            ( ui_menu_state_t ){ .selected = active, .editing = a_editing, .available = true } );
        if( '\0' != value[ 0 ] )
        {
            /* Values keep this view's wider column at x=232. */
            display_draw_text( 232, y, value, active ? theme->selected : theme->secondary, 1U );
        }
    }

    char footer[ 64 ];
    snprintf( footer,
              sizeof( footer ),
              "%u/%u  %s",
              (unsigned)( a_selected + 1U ),
              item_count,
              a_editing ? "TURN AUDIO / CENTER DONE" : "TURN SELECT / CENTER ENTER" );
    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       footer,
                       a_editing ? theme->selected : theme->secondary,
                       1U );
}
