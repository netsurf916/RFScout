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

#include <stdio.h>
#include <string.h>

#define PACKET_ROWS_PER_PAGE 6U
#define PACKET_ROW_HEIGHT 20
#define PACKET_ROW_START_Y 42
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
    const size_t count = displayed_packet_count;
    const ui_theme_palette_t *theme = ui_theme_get();

    display_clear( theme->background );
    ui_chrome_draw( "nRF24 PACKETS", theme->secondary );

    if( 0U == count )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 70, "NO VALIDATED PACKETS", theme->text, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, 90, "WAITING FOR DECODER INPUT", theme->secondary, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "HOLD: BACK", theme->secondary, 1U );
        (void)display_present();
        return 0U;
    }

    unsigned selected = a_selected;
    if( selected >= count )
    {
        selected = (unsigned)count - 1U;
    }
    const unsigned page_start = ( selected / PACKET_ROWS_PER_PAGE ) * PACKET_ROWS_PER_PAGE;

    for( unsigned row = 0U; row < PACKET_ROWS_PER_PAGE; ++row )
    {
        const unsigned index = page_start + row;
        if( index >= count )
        {
            break;
        }

        const nrf24_recent_packet_t *packet = &displayed_packets[ index ];
        char line[ 52 ];
        char address[ 18 ];
        format_address( packet, address, sizeof( address ) );
        snprintf( line,
                  sizeof( line ),
                  "%c CH%3u %-4s %-14.14s %2uB",
                  ( index == selected ) ? '>' : ' ',
                  (unsigned)packet->channel,
                  bitrate_name( packet->bitrate ),
                  address,
                  (unsigned)packet->payload_len );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           PACKET_ROW_START_Y + (int)row * PACKET_ROW_HEIGHT,
                           line,
                           ( index == selected ) ? theme->selected : theme->text,
                           1U );
    }

    char footer[ 64 ];
    snprintf( footer,
              sizeof( footer ),
              "%u/%u  CENTER: DETAIL  HOLD: BACK",
              selected + 1U,
              (unsigned)count );
    display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, footer, theme->secondary, 1U );
    (void)display_present();
    return count;
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
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "HOLD: BACK", theme->secondary, 1U );
        (void)display_present();
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

    display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "HOLD: BACK", theme->secondary, 1U );
    (void)display_present();
}
