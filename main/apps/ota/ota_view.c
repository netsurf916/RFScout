/**
    apps/ota/ota_view.c : Home Update menu presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ota_view.h"

#include "display.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "ui_menu.h"

#include <stdio.h>
#include <string.h>

#define OTA_MENU_VISIBLE_ROWS 5U
#define OTA_UPDATE_MENU_START_Y 72
#define OTA_UPDATE_MENU_ROW_HEIGHT UI_LAYOUT_MENU_ROW_HEIGHT

static void draw_menu_row_at(
    int a_y, unsigned a_item, unsigned a_selected, const char *a_label, const char *a_value )
{
    ui_menu_draw_row( a_y,
                      a_label,
                      a_value,
                      ( ui_menu_state_t ){ .selected = a_item == a_selected, .available = true } );
}

static void draw_menu_row(
    unsigned a_row, unsigned a_item, unsigned a_selected, const char *a_label, const char *a_value )
{
    draw_menu_row_at( UI_LAYOUT_MENU_START_Y + (int)a_row * UI_LAYOUT_MENU_ROW_HEIGHT,
                      a_item,
                      a_selected,
                      a_label,
                      a_value );
}

static void
draw_information_row( int a_y, const char *a_label, const char *a_value, bool a_emphasize_value )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_draw_text( UI_LAYOUT_LEFT_X, a_y, a_label, theme->secondary, 1U );
    display_draw_text( 78,
                       a_y,
                       NULL != a_value && '\0' != a_value[ 0 ] ? a_value : "unknown",
                       a_emphasize_value ? theme->accent : theme->text,
                       1U );
}

void ota_view_render_network_main( const ota_view_network_t *a_networks,
                                   size_t a_network_count,
                                   unsigned a_selected,
                                   bool a_scanning,
                                   bool a_connected,
                                   const char *a_error )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned action_count = a_connected ? 1U : 3U;
    const unsigned item_count = 1U + (unsigned)a_network_count + action_count;
    const unsigned start = ui_menu_window_start( a_selected, item_count, OTA_MENU_VISIBLE_ROWS );

    display_clear( theme->background );
    ui_chrome_draw( "NETWORK", theme->secondary );

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

        const unsigned menu_item = item - 1U;
        if( menu_item < a_network_count )
        {
            char rssi[ 16 ];
            snprintf( rssi, sizeof( rssi ), "%d", (int)a_networks[ menu_item ].rssi );
            draw_menu_row( row, item, a_selected, a_networks[ menu_item ].ssid, rssi );
            continue;
        }

        const unsigned action = menu_item - (unsigned)a_network_count;
        if( a_connected )
        {
            draw_menu_row( row, item, a_selected, "Saved networks", NULL );
        }
        else if( 0U == action )
        {
            draw_menu_row( row, item, a_selected, a_scanning ? "Scanning..." : "Scan again", NULL );
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

    const char *footer =
        NULL != a_error && '\0' != a_error[ 0 ] ? a_error : "TURN SELECT / CENTER ACTION";
    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       footer,
                       NULL != a_error && '\0' != a_error[ 0 ] ? theme->error : theme->secondary,
                       1U );
    (void)display_present();
}

void ota_view_render_update_main( const char *a_installed_version,
                                  const char *a_update_status,
                                  const char *a_connection,
                                  unsigned a_selected,
                                  bool a_ready,
                                  bool a_can_install,
                                  const char *a_error )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned item_count = a_ready ? ( a_can_install ? 4U : 3U ) : 1U;

    display_clear( theme->background );
    ui_chrome_draw( "OTA UPDATE", theme->secondary );

    draw_information_row( 28, "Installed", a_installed_version, true );
    draw_information_row( 42, "Update", a_update_status, a_can_install );
    draw_information_row( 56, "Network", a_connection, false );

    for( unsigned item = 0U; item < item_count; ++item )
    {
        const int y = OTA_UPDATE_MENU_START_Y + (int)item * OTA_UPDATE_MENU_ROW_HEIGHT;
        if( 0U == item )
        {
            draw_menu_row_at( y, item, a_selected, "Back", NULL );
        }
        else if( 1U == item )
        {
            draw_menu_row_at( y, item, a_selected, "Check for update", NULL );
        }
        else if( a_can_install && 2U == item )
        {
            draw_menu_row_at( y, item, a_selected, "Install update", NULL );
        }
        else
        {
            draw_menu_row_at( y, item, a_selected, "Available builds", NULL );
        }
    }

    const char *footer =
        NULL != a_error && '\0' != a_error[ 0 ]
            ? a_error
            : ( a_ready ? "TURN SELECT / CENTER ACTION" : "CONNECT NETWORK FIRST" );
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
            label = "Credentials";
        }
        draw_menu_row_at( 58 + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT, row, a_selected, label, NULL );
    }

    const bool has_status = NULL != a_status && '\0' != a_status[ 0 ];
    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       has_status ? a_status : "TURN SELECT / CENTER ACTION",
                       theme->secondary,
                       1U );
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
    const unsigned start = ui_menu_window_start( a_selected, item_count, OTA_MENU_VISIBLE_ROWS );

    display_clear( theme->background );
    ui_chrome_draw( NULL != a_title ? a_title : "NETWORKS", theme->secondary );

    if( 0U == a_network_count && NULL != a_empty_message )
    {
        display_draw_text( UI_LAYOUT_LABEL_X,
                           UI_LAYOUT_MENU_START_Y + UI_LAYOUT_MENU_ROW_HEIGHT,
                           a_empty_message,
                           theme->secondary,
                           1U );
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

void ota_view_render_build_list( const char *a_installed_version,
                                 const ota_view_build_t *a_builds,
                                 size_t a_build_count,
                                 unsigned a_selected,
                                 const char *a_empty_message )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned item_count = (unsigned)a_build_count + 1U;
    const unsigned start = ui_menu_window_start( a_selected, item_count, OTA_MENU_VISIBLE_ROWS );

    display_clear( theme->background );
    ui_chrome_draw( "AVAILABLE BUILDS", theme->secondary );
    draw_information_row( 25, "Installed", a_installed_version, true );
    if( 0U == a_build_count && NULL != a_empty_message )
    {
        display_draw_text( UI_LAYOUT_LABEL_X,
                           UI_LAYOUT_MENU_START_Y + UI_LAYOUT_MENU_ROW_HEIGHT,
                           a_empty_message,
                           theme->secondary,
                           1U );
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
        const ota_view_build_t *build = &a_builds[ item - 1U ];
        draw_menu_row( row, item, a_selected, build->label, build->version );
    }

    display_draw_text(
        UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "TURN SELECT / CENTER ACTION", theme->secondary, 1U );
    (void)display_present();
}

void ota_view_render_build_action( const char *a_installed_version,
                                   const char *a_label,
                                   const char *a_version,
                                   const char *a_commit,
                                   unsigned a_selected )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "FIRMWARE BUILD", theme->secondary );
    display_draw_text(
        UI_LAYOUT_LEFT_X, 25, NULL != a_label ? a_label : "Build", theme->accent, 1U );

    draw_information_row( 40, "Installed", a_installed_version, false );
    draw_information_row( 54, "Selected", a_version, true );

    char commit[ 24 ] = "";
    if( NULL != a_commit )
    {
        snprintf( commit, sizeof( commit ), "%.12s", a_commit );
    }
    draw_information_row( 68, "Commit", commit, false );

    draw_menu_row_at( 90, 0U, a_selected, "Back", NULL );
    draw_menu_row_at( 110, 1U, a_selected, "Install selected", NULL );

    display_draw_text(
        UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "EXPLICIT BUILD INSTALL", theme->secondary, 1U );
    (void)display_present();
}
