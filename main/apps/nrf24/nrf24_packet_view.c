/**
    nrf24_packet_view.c : RFScout nRF24 decoded-packet browser
    Description: Renders bounded newest-first snapshots of CRC-validated ESB packets.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "nrf24_packet_view.h"

#include "display.h"
#include "nrf24_activity.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "ui_menu.h"

#include <stdio.h>
#include <string.h>

#define PACKET_DETAIL_ROW_HEIGHT 18

static nrf24_recent_packet_t displayed_packets[ NRF24_RECENT_PACKET_MAX ];
static size_t displayed_packet_count;

static const char *bitrate_name( uint8_t a_bitrate )
{
    switch( a_bitrate )
    {
        case 1U:
            return "1M";
        case 2U:
            return "2M";
        case 3U:
            return "250K";
        default:
            return "?";
    }
}

static void
format_address( const nrf24_recent_packet_t *a_packet, char *a_buffer, size_t a_capacity )
{
    if( ( NULL == a_packet ) || ( NULL == a_buffer ) || ( 0U == a_capacity ) )
    {
        return;
    }

    size_t used = 0U;
    a_buffer[ 0 ] = '\0';
    for( uint8_t i = 0U; ( i < a_packet->address_len ) && ( used < a_capacity ); ++i )
    {
        const int written = snprintf( a_buffer + used,
                                      a_capacity - used,
                                      ( 0U == i ) ? "%02X" : ":%02X",
                                      a_packet->address[ i ] );
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

static void format_payload_range( const nrf24_recent_packet_t *a_packet,
                                  size_t a_start,
                                  size_t a_count,
                                  char *a_buffer,
                                  size_t a_capacity )
{
    if( ( NULL == a_packet ) || ( NULL == a_buffer ) || ( 0U == a_capacity ) )
    {
        return;
    }

    size_t used = 0U;
    a_buffer[ 0 ] = '\0';
    const size_t end =
        ( a_start + a_count < a_packet->payload_len ) ? a_start + a_count : a_packet->payload_len;
    for( size_t i = a_start; ( i < end ) && ( used < a_capacity ); ++i )
    {
        const int written = snprintf( a_buffer + used,
                                      a_capacity - used,
                                      ( i == a_start ) ? "%02X" : " %02X",
                                      a_packet->payload[ i ] );
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

size_t nrf24_packet_view_list( unsigned a_selected )
{
    displayed_packet_count =
        nrf24_activity_recent_packets( displayed_packets, NRF24_RECENT_PACKET_MAX );
    const size_t item_count = displayed_packet_count + 1U;
    const ui_theme_palette_t *theme = ui_theme_get();

    display_clear( theme->background );
    ui_chrome_draw( "nRF24 PACKETS", theme->secondary );

    unsigned selected = a_selected;
    if( selected >= item_count )
    {
        selected = (unsigned)item_count - 1U;
    }
    const unsigned first =
        ui_menu_window_start( selected, (unsigned)item_count, UI_LAYOUT_MENU_VISIBLE_ROWS );

    for( unsigned row = 0U; row < UI_LAYOUT_MENU_VISIBLE_ROWS; ++row )
    {
        const unsigned item = first + row;
        if( item >= item_count )
        {
            break;
        }

        const int y = UI_LAYOUT_MENU_START_Y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT;
        const ui_menu_state_t state = { .selected = item == selected, .available = true };
        if( 0U == item )
        {
            ui_menu_draw_row( y, "BACK", NULL, state );
            continue;
        }

        const nrf24_recent_packet_t *packet = &displayed_packets[ item - 1U ];
        char line[ 52 ];
        char address[ 18 ];
        format_address( packet, address, sizeof( address ) );
        snprintf( line,
                  sizeof( line ),
                  "CH%3u %-4s %-14.14s %2uB",
                  (unsigned)packet->channel,
                  bitrate_name( packet->bitrate ),
                  address,
                  (unsigned)packet->payload_len );
        ui_menu_draw_row( y, line, NULL, state );
    }

    if( 0U == displayed_packet_count )
    {
        display_draw_text( UI_LAYOUT_LABEL_X, 78, "NO VALIDATED PACKETS", theme->secondary, 1U );
        display_draw_text(
            UI_LAYOUT_LABEL_X, 98, "WAITING FOR DECODER INPUT", theme->secondary, 1U );
    }

    display_draw_text(
        UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "TURN: SELECT  CENTER: ENTER", theme->secondary, 1U );
    return item_count;
}

void nrf24_packet_view_detail( unsigned a_selected )
{
    const ui_theme_palette_t *theme = ui_theme_get();

    display_clear( theme->background );
    ui_chrome_draw( "nRF24 PACKET", theme->secondary );

    if( ( 0U == displayed_packet_count ) || ( a_selected >= displayed_packet_count ) )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 70, "PACKET NO LONGER AVAILABLE", theme->text, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: BACK", theme->secondary, 1U );
        return;
    }

    const nrf24_recent_packet_t *packet = &displayed_packets[ a_selected ];
    char line[ 64 ];
    char address[ 18 ];
    char payload[ 64 ];
    format_address( packet, address, sizeof( address ) );

    snprintf( line,
              sizeof( line ),
              "CH %u   RATE %s   LEN %u",
              (unsigned)packet->channel,
              bitrate_name( packet->bitrate ),
              (unsigned)packet->payload_len );
    display_draw_text( UI_LAYOUT_LEFT_X, 34, line, theme->text, 1U );

    snprintf( line, sizeof( line ), "ADDR %s", address );
    display_draw_text( UI_LAYOUT_LEFT_X, 34 + PACKET_DETAIL_ROW_HEIGHT, line, theme->text, 1U );

    snprintf( line,
              sizeof( line ),
              "PID %u   %s   CRC %s",
              (unsigned)packet->pid,
              packet->no_ack ? "NO_ACK" : "ACK",
              packet->crc_valid ? "OK" : "BAD" );
    display_draw_text(
        UI_LAYOUT_LEFT_X, 34 + ( 2 * PACKET_DETAIL_ROW_HEIGHT ), line, theme->text, 1U );

    snprintf( line, sizeof( line ), "T+ %lu ms", (unsigned long)packet->timestamp_ms );
    display_draw_text(
        UI_LAYOUT_LEFT_X, 34 + ( 3 * PACKET_DETAIL_ROW_HEIGHT ), line, theme->secondary, 1U );

    display_draw_text(
        UI_LAYOUT_LEFT_X, 34 + ( 4 * PACKET_DETAIL_ROW_HEIGHT ), "PAYLOAD", theme->secondary, 1U );
    format_payload_range( packet, 0U, 16U, payload, sizeof( payload ) );
    display_draw_text(
        UI_LAYOUT_LEFT_X, 34 + ( 5 * PACKET_DETAIL_ROW_HEIGHT ), payload, theme->text, 1U );
    if( packet->payload_len > 16U )
    {
        format_payload_range( packet, 16U, 16U, payload, sizeof( payload ) );
        display_draw_text(
            UI_LAYOUT_LEFT_X, 34 + ( 6 * PACKET_DETAIL_ROW_HEIGHT ), payload, theme->text, 1U );
    }

    display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: BACK", theme->secondary, 1U );
}
