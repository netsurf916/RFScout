/**
    radio_app_view.c : RFScout shared nRF24 application views
    Description: Renders nRF24 spectrum, packet, detection, and settings views.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "radio_app_view.h"

#include "display.h"
#include "nrf24_activity.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "ui_menu.h"

#include <stdio.h>

#define APP_COLUMN_DIVIDER_X 198
#define APP_RIGHT_COLUMN_X 205
#define APP_SUMMARY_Y 30
#define APP_DIVIDER_Y 36
#define APP_GRAPH_Y 48
#define NRF24_GRAPH_HEIGHT 28
#define APP_DETECTIONS_Y 43
#define APP_DETECTION_ROW_Y 61
#define APP_DETECTION_ROW_HEIGHT 15
#define NRF24_PACKET_HEADER_Y 82
#define NRF24_PACKET_ROW_START_Y 94
#define NRF24_PACKET_ROW_HEIGHT 11
#define NRF24_PACKET_PREVIEW_ROWS 7U
#define NRF24_PACKET_PREVIEW_BYTES 8U
#define NRF24_MENU_ITEM_COUNT 7U

static void
format_packet_preview( const nrf24_recent_packet_t *a_packet, char *a_buffer, size_t a_capacity )
{
    if( ( NULL == a_packet ) || ( NULL == a_buffer ) || ( 0U == a_capacity ) )
    {
        return;
    }

    const int prefix = snprintf( a_buffer, a_capacity, "%03u |", (unsigned)a_packet->channel );
    if( prefix < 0 )
    {
        a_buffer[ 0 ] = '\0';
        return;
    }

    size_t used = (size_t)prefix;
    if( used >= a_capacity )
    {
        a_buffer[ a_capacity - 1U ] = '\0';
        return;
    }

    const size_t byte_count = ( a_packet->payload_len < NRF24_PACKET_PREVIEW_BYTES )
                                  ? a_packet->payload_len
                                  : NRF24_PACKET_PREVIEW_BYTES;
    for( size_t index = 0U; index < byte_count; ++index )
    {
        const int written =
            snprintf( a_buffer + used, a_capacity - used, " %02X", a_packet->payload[ index ] );
        if( written <= 0 )
        {
            break;
        }
        used += (size_t)written;
        if( used >= a_capacity )
        {
            a_buffer[ a_capacity - 1U ] = '\0';
            break;
        }
    }
}

void radio_app_view_nrf24_spectrum( const uint8_t *a_fast,
                                    size_t a_count,
                                    uint8_t a_low_channel,
                                    uint8_t a_high_channel,
                                    bool a_detected,
                                    const nrf24_fingerprint_t *a_devices,
                                    size_t a_device_count )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "nRF24", theme->secondary );

    char line[ 64 ];
    snprintf( line,
              sizeof( line ),
              "CH %u-%u   %u-%u MHz",
              (unsigned)a_low_channel,
              (unsigned)a_high_channel,
              (unsigned)( 2400U + a_low_channel ),
              (unsigned)( 2400U + a_high_channel ) );
    display_draw_text( UI_LAYOUT_LEFT_X, APP_SUMMARY_Y, line, theme->secondary, 1U );

    if( !a_detected )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 70, "nRF24 NOT DETECTED", theme->warning, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: SETTINGS", theme->secondary, 1U );
        (void)display_present();
        return;
    }

    const int graph_x = UI_LAYOUT_LEFT_X;
    const int graph_width = APP_COLUMN_DIVIDER_X - UI_LAYOUT_LEFT_X - 6;
    const size_t span = (size_t)( a_high_channel - a_low_channel ) + 1U;
    if( ui_game_active() )
    {
        ui_game_draw_panel(
            graph_x - 2, APP_GRAPH_Y - 2, graph_width + 4, NRF24_GRAPH_HEIGHT + 5, theme->divider );
    }
    display_fill_rect( graph_x, APP_GRAPH_Y + NRF24_GRAPH_HEIGHT, graph_width, 1, theme->divider );

    if( NULL != a_fast )
    {
        for( size_t i = 0U; i < span; ++i )
        {
            const size_t channel = (size_t)a_low_channel + i;
            if( channel >= a_count )
            {
                break;
            }

            const size_t denominator = ( span > 1U ) ? span - 1U : 1U;
            const int x = graph_x + (int)( i * (size_t)( graph_width - 1 ) / denominator );
            const int height = (int)a_fast[ channel ] * ( NRF24_GRAPH_HEIGHT - 3 ) / 255;

            if( height > 0 )
            {
                ui_game_draw_column(
                    x, APP_GRAPH_Y + NRF24_GRAPH_HEIGHT - height, 2, height, theme->graph_primary );
            }
        }
    }

    display_draw_text( UI_LAYOUT_LEFT_X, NRF24_PACKET_HEADER_Y, "PACKETS", theme->secondary, 1U );
    nrf24_recent_packet_t packets[ NRF24_PACKET_PREVIEW_ROWS ];
    const size_t packet_count = nrf24_activity_recent_packets( packets, NRF24_PACKET_PREVIEW_ROWS );
    for( size_t index = 0U; index < packet_count; ++index )
    {
        format_packet_preview( &packets[ index ], line, sizeof( line ) );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           NRF24_PACKET_ROW_START_Y + (int)index * NRF24_PACKET_ROW_HEIGHT,
                           line,
                           theme->warning,
                           1U );
    }

    display_fill_rect( APP_COLUMN_DIVIDER_X,
                       APP_DIVIDER_Y,
                       1,
                       RFSCOUT_DISPLAY_HEIGHT - APP_DIVIDER_Y,
                       theme->divider );
    display_draw_text( APP_RIGHT_COLUMN_X, APP_DETECTIONS_Y, "DETECTIONS", theme->secondary, 1U );

    if( NULL != a_devices )
    {
        const size_t rows =
            ( a_device_count < NRF24_FINGERPRINT_MAX ) ? a_device_count : NRF24_FINGERPRINT_MAX;

        for( size_t i = 0U; i < rows; ++i )
        {
            const int y = APP_DETECTION_ROW_Y + (int)i * APP_DETECTION_ROW_HEIGHT;
            const char *label = ( '\0' != a_devices[ i ].label[ 0 ] )
                                    ? a_devices[ i ].label
                                    : nrf24_activity_fingerprint_name( a_devices[ i ].type );
            snprintf( line,
                      sizeof( line ),
                      "%-12.12s %3lu",
                      label,
                      (unsigned long)a_devices[ i ].packets );
            display_draw_text( APP_RIGHT_COLUMN_X,
                               y,
                               line,
                               ( 0U == a_devices[ i ].packets ) ? theme->divider : theme->warning,
                               1U );
        }
    }
}

void radio_app_view_nrf24_menu( unsigned a_selected,
                                bool a_editing,
                                uint8_t a_low_channel,
                                uint8_t a_high_channel,
                                uint16_t a_dwell_us,
                                bool a_notifications_enabled )
{
    static const char *labels[ NRF24_MENU_ITEM_COUNT ] = {
        "BACK", "RESET COUNTS", "LOW CH", "HIGH CH", "DWELL", "PACKET DETAILS", "NOTIFICATIONS" };
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned first =
        ui_menu_window_start( a_selected, NRF24_MENU_ITEM_COUNT, UI_LAYOUT_MENU_VISIBLE_ROWS );

    display_clear( theme->background );
    ui_chrome_draw( "nRF24 SETTINGS", theme->secondary );

    char value[ 32 ];
    for( unsigned row = 0U; row < UI_LAYOUT_MENU_VISIBLE_ROWS; ++row )
    {
        const unsigned i = first + row;
        if( i >= NRF24_MENU_ITEM_COUNT )
        {
            break;
        }

        const int y = UI_LAYOUT_MENU_START_Y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT;
        const bool active = ( i == a_selected );
        const bool action = ( 0U == i ) || ( 1U == i ) || ( 5U == i ) || ( 6U == i );
        ui_menu_draw_row( y,
                          labels[ i ],
                          NULL,
                          ( ui_menu_state_t ){ .selected = active,
                                               .editing = a_editing && !action,
                                               .available = true } );

        value[ 0 ] = '\0';
        if( 2U == i )
        {
            snprintf( value,
                      sizeof( value ),
                      "%u / %u MHz",
                      (unsigned)a_low_channel,
                      (unsigned)( 2400U + a_low_channel ) );
        }
        else if( 3U == i )
        {
            snprintf( value,
                      sizeof( value ),
                      "%u / %u MHz",
                      (unsigned)a_high_channel,
                      (unsigned)( 2400U + a_high_channel ) );
        }
        else if( 4U == i )
        {
            snprintf( value, sizeof( value ), "%u us", (unsigned)a_dwell_us );
        }
        else if( 6U == i )
        {
            snprintf( value, sizeof( value ), "%s", a_notifications_enabled ? "ON" : "OFF" );
        }

        if( '\0' != value[ 0 ] )
        {
            if( active && a_editing )
            {
                const ui_color_pair_t colors = ui_theme_pair( UI_COLOR_PAIR_SELECTED );
                display_fill_rect( 126, y - 3, 174, 15, colors.bg );
                display_draw_text( 130, y, value, colors.fg, 1U );
            }
            else
            {
                display_draw_text( 130, y, value, active ? theme->selected : theme->secondary, 1U );
            }
        }
    }

    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       a_editing ? "TURN: CHANGE  CENTER: DONE" : "TURN: SELECT  CENTER: ENTER",
                       a_editing ? theme->selected : theme->secondary,
                       1U );
}
