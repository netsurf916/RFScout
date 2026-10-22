/**
    gps_view.c : GPS receiver diagnostics presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "gps_view.h"

#include "display.h"
#include "device_config.h"
#include "breadcrumbs.h"
#include "gps.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_menu.h"
#include "ui_theme.h"

#include <stdio.h>

static void draw_gps_panel( int a_x, int a_y, int a_width, int a_height )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    if( ui_game_active() )
    {
        ui_game_draw_panel( a_x, a_y, a_width, a_height, theme->divider );
        return;
    }
    display_fill_rect( a_x, a_y, a_width, a_height, theme->surface );
    display_fill_rect( a_x, a_y, a_width, 1, theme->divider );
    display_fill_rect( a_x, a_y + a_height - 1, a_width, 1, theme->divider );
    display_fill_rect( a_x, a_y, 1, a_height, theme->divider );
    display_fill_rect( a_x + a_width - 1, a_y, 1, a_height, theme->divider );
}

static unsigned track_sector( float a_degrees )
{
    return ( (unsigned)( ( a_degrees + 22.5f ) / 45.0f ) ) % 8U;
}

typedef struct
{
    uint32_t rgb;
    const char *name;
} gps_led_color_t;

static const gps_led_color_t LED_COLORS[] = {
    { 0x00FF00U, "Green" },
    { 0xFF0000U, "Red" },
    { 0x0000FFU, "Blue" },
    { 0xFFFFFFU, "White" },
    { 0xFF8000U, "Orange" },
    { 0x00FFFFU, "Cyan" },
};
#define LED_COLOR_COUNT ( sizeof( LED_COLORS ) / sizeof( LED_COLORS[ 0 ] ) )

static const char *led_color_name( uint32_t a_rgb )
{
    for( size_t index = 0U; index < LED_COLOR_COUNT; ++index )
    {
        if( LED_COLORS[ index ].rgb == a_rgb )
        {
            return LED_COLORS[ index ].name;
        }
    }
    return "";
}

uint32_t gps_view_next_led_color( uint32_t a_rgb )
{
    for( size_t index = 0U; index < LED_COLOR_COUNT; ++index )
    {
        if( LED_COLORS[ index ].rgb == a_rgb )
        {
            return LED_COLORS[ ( index + 1U ) % LED_COLOR_COUNT ].rgb;
        }
    }
    return LED_COLORS[ 0 ].rgb;
}

bool gps_view_setting_available( unsigned a_setting )
{
    const bool needs_receiver =
        GPS_SETTING_BREADCRUMBS == a_setting || GPS_SETTING_BREADCRUMB_FILES == a_setting ||
        GPS_SETTING_MOVEMENT == a_setting || GPS_SETTING_TIMEPULSE == a_setting;
    return !needs_receiver || gps_detected();
}

void gps_view_render( bool a_menu_open, unsigned a_menu_item, bool a_menu_error )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    rfscout_gps_snapshot_t gps = { 0 };
    const bool present = gps_snapshot_get( &gps );
    char text[ 64 ];

    display_clear( theme->background );
    ui_chrome_draw( a_menu_open ? "GPS SETTINGS" : "GPS", theme->secondary );

    if( a_menu_open )
    {
        static const char *labels[ GPS_SETTING_COUNT ] = {
            [GPS_SETTING_BACK] = "Back",
            [GPS_SETTING_RESET_TRIP] = "Reset trip",
            [GPS_SETTING_LED] = "LED guidance",
            [GPS_SETTING_LED_BRIGHTNESS] = "LED brightness",
            [GPS_SETTING_LED_COLOR] = "LED color",
            [GPS_SETTING_KEEP_AWAKE] = "Stay awake",
            [GPS_SETTING_BREADCRUMBS] = "Breadcrumbs",
            [GPS_SETTING_BREADCRUMB_FILES] = "Retained files",
            [GPS_SETTING_MOVEMENT] = "Movement trigger",
            [GPS_SETTING_TIMEOUT] = "GPS timeout",
            [GPS_SETTING_TIMEPULSE] = "GPS Timepulse",
        };
        const device_config_t *config = device_config_get();
        const unsigned first = ui_menu_window_start( a_menu_item, GPS_SETTING_COUNT, 5U );
        for( unsigned row = 0U; row < 5U; ++row )
        {
            const unsigned item = first + row;
            if( item >= GPS_SETTING_COUNT )
            {
                break;
            }
            const int y = UI_LAYOUT_MENU_START_Y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT;
            const ui_menu_state_t menu_state = { .selected = item == a_menu_item,
                                                 .available = gps_view_setting_available( item ) };
            ui_menu_draw_row( y, labels[ item ], NULL, menu_state );
            text[ 0 ] = '\0';
            switch( item )
            {
                case GPS_SETTING_LED:
                    (void)snprintf(
                        text, sizeof( text ), "%s", config->gps_led_enabled ? "On" : "Off" );
                    break;
                case GPS_SETTING_LED_BRIGHTNESS:
                    (void)snprintf(
                        text, sizeof( text ), "%u%%", (unsigned)config->gps_led_brightness );
                    break;
                case GPS_SETTING_LED_COLOR:
                    (void)snprintf(
                        text, sizeof( text ), "%s", led_color_name( config->gps_led_rgb ) );
                    break;
                case GPS_SETTING_KEEP_AWAKE:
                    (void)snprintf(
                        text, sizeof( text ), "%s", config->gps_keep_awake ? "On" : "Off" );
                    break;
                case GPS_SETTING_BREADCRUMBS:
                    (void)snprintf(
                        text, sizeof( text ), "%s", breadcrumbs_enabled() ? "On" : "Off" );
                    break;
                case GPS_SETTING_BREADCRUMB_FILES:
                    (void)snprintf(
                        text, sizeof( text ), "%u", (unsigned)breadcrumbs_file_limit() );
                    break;
                case GPS_SETTING_MOVEMENT:
                    (void)snprintf(
                        text, sizeof( text ), "%u m", (unsigned)config->gps_movement_m );
                    break;
                case GPS_SETTING_TIMEOUT:
                    (void)snprintf(
                        text, sizeof( text ), "%us", (unsigned)config->gps_timeout_seconds );
                    break;
                case GPS_SETTING_TIMEPULSE:
                    (void)snprintf(
                        text, sizeof( text ), "%s", config->gps_timepulse_enabled ? "On" : "Off" );
                    break;
                default:
                    break;
            }
            if( '\0' != text[ 0 ] )
            {
                display_draw_text( 224, y, text, ui_menu_value_color( menu_state ), 1U );
            }
        }
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_FOOTER_Y,
                           a_menu_error ? "ACTION FAILED" : "TURN: SELECT  CENTER: CHANGE",
                           a_menu_error ? theme->warning : theme->secondary,
                           1U );
        (void)display_present();
        return;
    }

    /* Four bounded panels below the platform-owned y=0..21 status region. */
    draw_gps_panel( UI_LAYOUT_PANEL_LEFT_X, UI_LAYOUT_PANEL_TOP_Y, 152, 88 );
    draw_gps_panel( 4, 116, 152, 48 );
    draw_gps_panel( 160, UI_LAYOUT_PANEL_TOP_Y, 156, 42 );
    draw_gps_panel( 160, 70, 156, 94 );

    display_draw_text( 10, 28, "FIX", theme->secondary, 1U );
    const bool fixed = present && gps.location_valid && gps.location_age_ms <= 5000U;
    display_draw_text(
        115, 28, fixed ? "FIX" : "NO FIX", fixed ? theme->success : theme->warning, 1U );
    if( fixed )
    {
        (void)snprintf( text, sizeof( text ), "LAT %.6f", gps.latitude_deg );
        display_draw_text( 10, 42, text, theme->text, 1U );
        (void)snprintf( text, sizeof( text ), "LON %.6f", gps.longitude_deg );
        display_draw_text( 10, 54, text, theme->text, 1U );
        if( gps.altitude_valid )
        {
            (void)snprintf( text, sizeof( text ), "ALT %.1fm", (double)gps.altitude_m );
        }
        else
        {
            (void)snprintf( text, sizeof( text ), "ALT --" );
        }
        display_draw_text( 10, 70, text, theme->info, 1U );
        (void)snprintf( text, sizeof( text ), "AGE %lums", (unsigned long)gps.location_age_ms );
        display_draw_text( 10, 85, text, theme->info, 1U );
    }
    else
    {
        display_draw_text( 10, 48, "POSITION --", theme->warning, 1U );
        display_draw_text(
            10, 66, !gps_available() ? "UART INACTIVE" : "WAITING FOR FIX", theme->info, 1U );
    }

    display_draw_text( 10, 120, "MOTION", theme->secondary, 1U );
    (void)snprintf( text, sizeof( text ), "TRIP %.2fkm", gps_trip_distance_m() / 1000.0 );
    display_draw_text( 10, 133, text, theme->normal, 1U );
    if( fixed && gps.speed_valid )
    {
        (void)snprintf( text, sizeof( text ), "VEL %.1fkm/h", (double)gps.speed_mps * 3.6 );
    }
    else
    {
        (void)snprintf( text, sizeof( text ), "VEL --" );
    }
    display_draw_text( 10, 146, text, theme->info, 1U );

    display_draw_text( 166, 28, "SATELLITES", theme->secondary, 1U );
    if( present && gps.satellites_valid )
    {
        (void)snprintf( text, sizeof( text ), "USED %u", (unsigned)gps.satellites );
    }
    else
    {
        (void)snprintf( text, sizeof( text ), "USED --" );
    }
    display_draw_text( 166, 43, text, theme->normal, 1U );

    display_draw_text( 166, 74, "GROUND TRACK", theme->secondary, 1U );
    const bool track = fixed && gps.track_valid && gps.speed_valid && gps.speed_mps >= 0.7f;
    if( track )
    {
        static const char *labels[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        const unsigned sector = track_sector( gps.track_deg );
        display_draw_text( 176, 96, labels[ sector ], theme->accent, 3U );
        (void)snprintf( text, sizeof( text ), "%.1f deg", (double)gps.track_deg );
        display_draw_text( 176, 142, text, theme->text, 1U );
    }
    else
    {
        display_draw_text( 176, 99, "--", theme->secondary, 3U );
        display_draw_text( 176, 143, "TRACK --", theme->info, 1U );
    }
    display_draw_text( 267, 96, "N", theme->secondary, 1U );
    display_fill_rect( 272, 109, 2, 22, theme->divider );
    display_fill_rect( 267, 119, 12, 2, theme->divider );
    if( track )
    {
        static const int offsets_x[] = { 0, 7, 10, 7, 0, -7, -10, -7 };
        static const int offsets_y[] = { -10, -7, 0, 7, 10, 7, 0, -7 };
        const unsigned sector = track_sector( gps.track_deg );
        display_fill_rect(
            272 + offsets_x[ sector ], 120 + offsets_y[ sector ], 3, 3, theme->accent );
    }
    (void)display_present();
}
