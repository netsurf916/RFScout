/**
    system_events_view.c : RFScout System Events application view
    Description: Presents the volatile structured event ring as a compact browsable diagnostics list.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "system_events_view.h"

#include "app_framework.h"
#include "app_manager.h"
#include "display.h"
#include "system_events.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stdio.h>

#define SYSTEM_EVENTS_VISIBLE_ROWS 4U
#define SYSTEM_EVENTS_ROW_HEIGHT 27
#define SYSTEM_EVENTS_FIRST_Y 31

static bool browsing;
static size_t browse_offset;
static size_t snapshot_count;
static rfscout_system_event_entry_t snapshot[ RFSCOUT_SYSTEM_EVENT_HISTORY_CAPACITY ];

static const char *severity_name( rfscout_system_event_severity_t a_severity )
{
    switch( a_severity )
    {
        case RFSCOUT_SYSTEM_EVENT_WARNING:
            return "W";
        case RFSCOUT_SYSTEM_EVENT_CRITICAL:
            return "C";
        case RFSCOUT_SYSTEM_EVENT_INFO:
        default:
            return "I";
    }
}

static const char *source_name( uint8_t a_source )
{
    if( RFSCOUT_EVENT_SOURCE_SYSTEM == a_source )
    {
        return "SYS";
    }
    switch( (rfscout_app_id_t)a_source )
    {
        case RFSCOUT_APP_SYSTEM_EVENTS:
            return "EVT";
        case RFSCOUT_APP_DEVICE:
            return "SET";
        case RFSCOUT_APP_WIFI_BT:
            return "W/B";
        case RFSCOUT_APP_CC1101:
            return "CC1";
        case RFSCOUT_APP_NRF24:
            return "NRF";
        case RFSCOUT_APP_AUDIO:
            return "AUD";
        case RFSCOUT_APP_THERMAL:
            return "THM";
        case RFSCOUT_APP_AUDIO_SPECTRUM:
            return "SPC";
        case RFSCOUT_APP_COUNT:
        default:
            return "???";
    }
}

static const char *event_app_name( uint8_t a_source )
{
    if( a_source >= (uint8_t)RFSCOUT_APP_COUNT )
    {
        return "HARDWARE";
    }
    const rfscout_app_descriptor_t *descriptor =
        app_framework_descriptor( (rfscout_app_id_t)a_source );
    return ( NULL != descriptor && NULL != descriptor->name ) ? descriptor->name : "HARDWARE";
}

static void event_text( const rfscout_event_t *a_event, char *a_text, size_t a_capacity )
{
    if( NULL == a_event || NULL == a_text || 0U == a_capacity )
    {
        return;
    }
    switch( a_event->type )
    {
        case RFSCOUT_EVENT_POWER_CONNECTED:
            snprintf( a_text, a_capacity, "POWER CONNECTED" );
            break;
        case RFSCOUT_EVENT_POWER_DISCONNECTED:
            snprintf( a_text, a_capacity, "POWER DISCONNECTED" );
            break;
        case RFSCOUT_EVENT_STORAGE_MOUNTED:
            snprintf( a_text, a_capacity, "SD MOUNTED" );
            break;
        case RFSCOUT_EVENT_STORAGE_MOUNT_FAILED:
            snprintf( a_text, a_capacity, "SD UNAVAILABLE" );
            break;
        case RFSCOUT_EVENT_APP_DISCOVERED:
            if( RFSCOUT_APP_AVAILABILITY_AVAILABLE == (rfscout_app_availability_t)a_event->value )
            {
                snprintf( a_text, a_capacity, "%s DETECTED", event_app_name( a_event->source ) );
            }
            else if( RFSCOUT_APP_AVAILABILITY_UNAVAILABLE ==
                     (rfscout_app_availability_t)a_event->value )
            {
                snprintf( a_text, a_capacity, "%s NOT PRESENT", event_app_name( a_event->source ) );
            }
            else
            {
                snprintf( a_text, a_capacity, "%s UNAVAILABLE", event_app_name( a_event->source ) );
            }
            break;
        case RFSCOUT_EVENT_APP_AVAILABLE:
            snprintf( a_text, a_capacity, "%s CONNECTED", event_app_name( a_event->source ) );
            break;
        case RFSCOUT_EVENT_APP_UNAVAILABLE:
            snprintf( a_text, a_capacity, "%s DISCONNECTED", event_app_name( a_event->source ) );
            break;
        case RFSCOUT_EVENT_DISPLAY_PROVIDER_SELECTED:
            snprintf( a_text, a_capacity, "DISPLAY ST7789" );
            break;
        case RFSCOUT_EVENT_INPUT_PROVIDER_SELECTED:
            snprintf( a_text, a_capacity, "INPUT ROTARY/BUTTONS" );
            break;
        case RFSCOUT_EVENT_WARNING:
            snprintf( a_text, a_capacity, "SYSTEM WARNING" );
            break;
        case RFSCOUT_EVENT_ERROR:
            snprintf( a_text, a_capacity, "SYSTEM ERROR" );
            break;
        default:
            snprintf( a_text, a_capacity, "EVENT %u", (unsigned)a_event->type );
            break;
    }
}

static bool entry_at( size_t a_index, rfscout_system_event_entry_t *a_entry )
{
    if( browsing )
    {
        if( a_index >= snapshot_count || NULL == a_entry )
        {
            return false;
        }
        *a_entry = snapshot[ a_index ];
        return true;
    }
    return system_events_history_get( a_index, a_entry );
}

void system_events_view_enter( void )
{
    browsing = false;
    browse_offset = 0U;
    snapshot_count = 0U;
    system_events_view_render();
}

void system_events_view_leave( void )
{
    browsing = false;
    browse_offset = 0U;
    snapshot_count = 0U;
}

bool system_events_view_browsing( void )
{
    return browsing;
}

void system_events_view_toggle_browse( void )
{
    if( browsing )
    {
        browsing = false;
        browse_offset = 0U;
        snapshot_count = 0U;
        system_events_view_render();
        return;
    }

    snapshot_count = system_events_history_count();
    if( snapshot_count > RFSCOUT_SYSTEM_EVENT_HISTORY_CAPACITY )
    {
        snapshot_count = RFSCOUT_SYSTEM_EVENT_HISTORY_CAPACITY;
    }
    size_t retained = 0U;
    for( size_t index = 0U; index < snapshot_count; ++index )
    {
        if( !system_events_history_get( index, &snapshot[ retained ] ) )
        {
            break;
        }
        ++retained;
    }
    snapshot_count = retained;
    browse_offset = 0U;
    browsing = true;
    system_events_view_render();
}

void system_events_view_scroll( int a_direction )
{
    if( !browsing || 0 == a_direction || 0U == snapshot_count )
    {
        return;
    }
    const size_t max_offset = snapshot_count > SYSTEM_EVENTS_VISIBLE_ROWS
                                  ? snapshot_count - SYSTEM_EVENTS_VISIBLE_ROWS
                                  : 0U;
    if( a_direction > 0 )
    {
        if( browse_offset < max_offset )
        {
            ++browse_offset;
        }
    }
    else if( browse_offset > 0U )
    {
        --browse_offset;
    }
    system_events_view_render();
}

void system_events_view_render( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const size_t count = browsing ? snapshot_count : system_events_history_count();

    display_clear( theme->background );
    ui_chrome_draw( "SYSTEM EVENTS", theme->secondary );

    if( 0U == count )
    {
        display_draw_text( UI_LAYOUT_LABEL_X, 72, "NO RECENT EVENTS", theme->secondary, 1U );
    }
    else
    {
        for( size_t row = 0U; row < SYSTEM_EVENTS_VISIBLE_ROWS; ++row )
        {
            const size_t index = browse_offset + row;
            rfscout_system_event_entry_t entry;
            if( index >= count || !entry_at( index, &entry ) )
            {
                break;
            }

            char message[ 32 ];
            char primary[ 64 ];
            char detail[ 48 ];
            event_text( &entry.event, message, sizeof( message ) );
            const uint64_t uptime_ms = (uint64_t)entry.monotonic_us / 1000ULL;
            snprintf( primary,
                      sizeof( primary ),
                      "+%4llu.%03llu %s %-20.20s",
                      (unsigned long long)( uptime_ms / 1000ULL ),
                      (unsigned long long)( uptime_ms % 1000ULL ),
                      severity_name( entry.severity ),
                      message );
            snprintf( detail,
                      sizeof( detail ),
                      "    src=%s value=%ld",
                      source_name( entry.event.source ),
                      (long)entry.event.value );
            const int y = SYSTEM_EVENTS_FIRST_Y + (int)row * SYSTEM_EVENTS_ROW_HEIGHT;
            display_draw_text( UI_LAYOUT_LEFT_X, y, primary, theme->text, 1U );
            display_draw_text( UI_LAYOUT_LEFT_X, y + 11, detail, theme->secondary, 1U );
        }
    }

    char footer[ 72 ];
    if( browsing )
    {
        const size_t first = ( 0U == count ) ? 0U : browse_offset + 1U;
        snprintf( footer,
                  sizeof( footer ),
                  "BROWSE %u/%u  TURN: SCROLL  CENTER: APPS",
                  (unsigned)first,
                  (unsigned)count );
    }
    else
    {
        snprintf( footer, sizeof( footer ), "LIVE  TURN: APPS  CENTER: BROWSE" );
    }
    display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, footer, theme->secondary, 1U );
    (void)display_present();
}
