/**
    gps_view.c : GPS receiver diagnostics presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "gps_view.h"

#include "display.h"
#include "gps.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_layout.h"
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

void gps_view_render( bool a_menu_open, bool a_reset_selected, bool a_reset_failed )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    rfscout_gps_snapshot_t gps = { 0 };
    const bool present = gps_snapshot_get( &gps );
    char text[ 64 ];

    display_clear( theme->background );
    ui_chrome_draw( a_menu_open ? "GPS SETTINGS" : "GPS", theme->secondary );

    if( a_menu_open )
    {
        static const char *labels[] = { "Back", "Reset trip" };
        for( unsigned item = 0U; item < 2U; ++item )
        {
            const int y = UI_LAYOUT_MENU_START_Y + (int)item * UI_LAYOUT_MENU_ROW_HEIGHT;
            const bool selected = ( item == ( a_reset_selected ? 1U : 0U ) );
            ui_game_draw_menu_marker( UI_LAYOUT_LEFT_X,
                                      y,
                                      selected ? ">" : " ",
                                      selected ? theme->selected : theme->divider );
            display_draw_text( UI_LAYOUT_LABEL_X,
                               y,
                               labels[ item ],
                               selected ? theme->text : theme->divider,
                               1U );
        }
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "TURN: SELECT  CENTER: ACT", theme->secondary, 1U );
        (void)display_present();
        return;
    }

    /* Four bounded panels below the platform-owned y=0..21 status region. */
    draw_gps_panel( 4, 24, 152, 88 );
    draw_gps_panel( 4, 116, 152, 48 );
    draw_gps_panel( 160, 24, 156, 42 );
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
    if( a_reset_failed )
    {
        display_draw_text( 10, 156, "RESET NOT SAVED", theme->warning, 1U );
    }
    (void)display_present();
}
