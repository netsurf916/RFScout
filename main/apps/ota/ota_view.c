/**
    apps/ota/ota_view.c : OTA Update application presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ota_view.h"

#include "display.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

#define OTA_MENU_START_Y 43
#define OTA_MENU_ROW_HEIGHT 20
#define OTA_MENU_VISIBLE_ROWS 5U

static unsigned menu_window_start( unsigned a_selected, unsigned a_count )
{
    if( a_count <= OTA_MENU_VISIBLE_ROWS || a_selected < OTA_MENU_VISIBLE_ROWS )
    {
        return 0U;
    }
    unsigned start = a_selected - OTA_MENU_VISIBLE_ROWS + 1U;
    const unsigned maximum = a_count - OTA_MENU_VISIBLE_ROWS;
    if( start > maximum )
    {
        start = maximum;
    }
    return start;
}

static void draw_menu_row(
    unsigned a_row, unsigned a_item, unsigned a_selected, const char *a_label, const char *a_value )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const bool active = a_item == a_selected;
    const uint16_t color = active ? theme->selected : theme->text;
    const int y = OTA_MENU_START_Y + (int)a_row * OTA_MENU_ROW_HEIGHT;
    if( active )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, y, ">", color, 1U );
    }
    display_draw_text( UI_LAYOUT_LABEL_X, y, a_label, color, 1U );
    if( NULL != a_value && '\0' != a_value[ 0 ] )
    {
        display_draw_text( 242, y, a_value, active ? theme->accent : theme->secondary, 1U );
    }
}

void ota_view_render_main( const char *a_version,
                           const char *a_connection,
                           const ota_view_network_t *a_networks,
                           size_t a_network_count,
                           unsigned a_selected,
                           bool a_scanning,
                           bool a_can_check,
                           const char *a_update_status,
                           const char *a_error )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned item_count = (unsigned)a_network_count + ( a_can_check ? 2U : 3U );
    const unsigned start = menu_window_start( a_selected, item_count );

    display_clear( theme->background );
    ui_chrome_draw( "OTA UPDATE", theme->secondary );

    char status[ 64 ];
    snprintf( status,
              sizeof( status ),
              "v%s  %s",
              NULL != a_version ? a_version : "unknown",
              NULL != a_connection ? a_connection : "Disconnected" );
    display_draw_text( UI_LAYOUT_LEFT_X, 25, status, theme->secondary, 1U );

    for( unsigned row = 0U; row < OTA_MENU_VISIBLE_ROWS; ++row )
    {
        const unsigned item = start + row;
        if( item >= item_count )
        {
            break;
        }

        if( item < a_network_count )
        {
            char rssi[ 16 ];
            snprintf( rssi, sizeof( rssi ), "%d", (int)a_networks[ item ].rssi );
            draw_menu_row( row, item, a_selected, a_networks[ item ].ssid, rssi );
        }
        else
        {
            const unsigned action = item - (unsigned)a_network_count;
            if( a_can_check )
            {
                if( 0U == action )
                {
                    draw_menu_row( row, item, a_selected, "Check for update", a_update_status );
                }
                else
                {
                    draw_menu_row( row, item, a_selected, "Saved networks", NULL );
                }
            }
            else if( 0U == action )
            {
                draw_menu_row(
                    row, item, a_selected, a_scanning ? "Scanning..." : "Scan again", NULL );
            }
            else if( 1U == action )
            {
                draw_menu_row( row, item, a_selected, "Add network", NULL );
            }
            else
            {
                draw_menu_row( row, item, a_selected, "Saved networks", NULL );
            }
        }
    }

    const char *footer =
        NULL != a_error && '\0' != a_error[ 0 ] ? a_error : "TURN SELECT / CENTER ACTION";
    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       footer,
                       NULL != a_error && '\0' != a_error[ 0 ] ? theme->error : theme->secondary,
                       1U );
    (void)display_present();
}

void ota_view_render_network_action( const char *a_ssid,
                                     int8_t a_rssi,
                                     bool a_visible,
                                     bool a_saved,
                                     bool a_open,
                                     bool a_connected,
                                     unsigned a_selected,
                                     const char *a_status )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned action_count = a_saved ? 3U : 2U;

    display_clear( theme->background );
    ui_chrome_draw( "NETWORK", theme->secondary );
    display_draw_text(
        UI_LAYOUT_LEFT_X, 26, NULL != a_ssid ? a_ssid : "Unknown", theme->accent, 1U );

    char detail[ 48 ];
    if( a_visible )
    {
        snprintf( detail, sizeof( detail ), "Seen now  %d dBm", (int)a_rssi );
    }
    else
    {
        snprintf( detail, sizeof( detail ), "Not seen in last scan" );
    }
    display_draw_text( UI_LAYOUT_LEFT_X, 39, detail, theme->secondary, 1U );

    for( unsigned row = 0U; row < action_count; ++row )
    {
        const char *label = "Back";
        if( 1U == row )
        {
            if( a_saved )
            {
                label = a_connected ? "Disconnect" : ( a_visible ? "Connect" : "Connect anyway" );
            }
            else
            {
                label = a_open ? "Save network" : "Enter password";
            }
        }
        else if( 2U == row )
        {
            label = "Forget";
        }
        draw_menu_row( row, row, a_selected, label, NULL );
    }

    if( NULL != a_status && '\0' != a_status[ 0 ] )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 118, a_status, theme->secondary, 1U );
    }
    display_draw_text(
        UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "TURN SELECT / CENTER ACTION", theme->secondary, 1U );
    (void)display_present();
}

void ota_view_render_network_list( const char *a_title,
                                   const ota_view_network_t *a_networks,
                                   size_t a_network_count,
                                   unsigned a_selected,
                                   const char *a_empty_message )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned item_count = (unsigned)a_network_count + 1U;
    const unsigned start = menu_window_start( a_selected, item_count );

    display_clear( theme->background );
    ui_chrome_draw( NULL != a_title ? a_title : "NETWORKS", theme->secondary );

    if( 0U == a_network_count && NULL != a_empty_message )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 28, a_empty_message, theme->secondary, 1U );
    }

    for( unsigned row = 0U; row < OTA_MENU_VISIBLE_ROWS; ++row )
    {
        const unsigned item = start + row;
        if( item >= item_count )
        {
            break;
        }
        if( 0U == item )
        {
            draw_menu_row( row, item, a_selected, "Back", NULL );
            continue;
        }

        const ota_view_network_t *network = &a_networks[ item - 1U ];
        char rssi[ 16 ] = "";
        if( network->rssi > -128 )
        {
            snprintf( rssi, sizeof( rssi ), "%d", (int)network->rssi );
        }
        draw_menu_row( row, item, a_selected, network->ssid, rssi );
    }

    display_draw_text(
        UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "TURN SELECT / CENTER ACTION", theme->secondary, 1U );
    (void)display_present();
}

void ota_view_render_password( const char *a_ssid,
                               size_t a_length,
                               rfscout_text_entry_selection_t a_selection,
                               char a_character,
                               const char *a_error )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "WI-FI PASSWORD", theme->secondary );

    display_draw_text(
        UI_LAYOUT_LEFT_X, 31, NULL != a_ssid ? a_ssid : "Network", theme->accent, 1U );

    char masked[ 33 ];
    const size_t shown = a_length < sizeof( masked ) - 1U ? a_length : sizeof( masked ) - 1U;
    memset( masked, '*', shown );
    masked[ shown ] = '\0';
    display_draw_text( UI_LAYOUT_LEFT_X, 58, "Password", theme->text, 1U );
    display_draw_text( 90, 58, masked, theme->secondary, 1U );

    char candidate[ 32 ];
    if( RFSCOUT_TEXT_ENTRY_SELECTION_DONE == a_selection )
    {
        snprintf( candidate, sizeof( candidate ), "Action: Done" );
    }
    else if( RFSCOUT_TEXT_ENTRY_SELECTION_BACKSPACE == a_selection )
    {
        snprintf( candidate, sizeof( candidate ), "Action: Backspace" );
    }
    else
    {
        snprintf(
            candidate, sizeof( candidate ), "Character: %c", ' ' == a_character ? '_' : a_character );
    }
    display_draw_text( UI_LAYOUT_LEFT_X, 84, candidate, theme->selected, 1U );
    display_draw_text( UI_LAYOUT_LEFT_X, 106, "CENTER: USE", theme->secondary, 1U );
    display_draw_text( UI_LAYOUT_LEFT_X, 121, "BACK: CANCEL", theme->secondary, 1U );

    if( NULL != a_error && '\0' != a_error[ 0 ] )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, a_error, theme->error, 1U );
    }
    else
    {
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "TURN: CHARACTER / ACTION", theme->secondary, 1U );
    }
    (void)display_present();
}
