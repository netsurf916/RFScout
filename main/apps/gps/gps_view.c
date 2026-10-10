/**
    gps_view.c : GPS receiver diagnostics presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "gps_view.h"

#include "display.h"
#include "gps.h"
#include "ui_chrome.h"
#include "ui_theme.h"

#include <stdio.h>

void gps_view_render( bool a_reset_prompt, bool a_reset_yes, bool a_reset_failed )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    rfscout_gps_snapshot_t gps = { 0 };
    const bool present = gps_snapshot_get( &gps );
    char text[ 64 ];

    display_clear( theme->background );
    ui_chrome_draw( "GPS", theme->secondary );

    if( a_reset_prompt )
    {
        snprintf( text, sizeof( text ), "TRIP: %.2f km", gps_trip_distance_m() / 1000.0 );
        display_draw_text( 8, 44, text, theme->text, 2U );
        display_draw_text( 8, 82, "RESET TRIP DISTANCE?", theme->warning, 1U );
        display_draw_text(
            8, 104, a_reset_yes ? "   NO     [YES]" : "  [NO]     YES", theme->text, 2U );
        display_draw_text( 8, 135, "ROTATE: CHOOSE  SELECT: OK", theme->info, 1U );
        display_draw_text( 8, 153, "LONG PRESS: CANCEL", theme->info, 1U );
        (void)display_present();
        return;
    }

    if( !gps_available() )
    {
        display_draw_text( 8, 56, "GPS UART INACTIVE", theme->warning, 2U );
        display_draw_text( 8, 94, "SHARED WITH NRF24 RADIO", theme->info, 1U );
    }
    else if( !present )
    {
        display_draw_text( 8, 56, "WAITING FOR GPS", theme->warning, 2U );
        display_draw_text( 8, 94, "NO VALID NMEA RECEIVED", theme->info, 1U );
    }
    else
    {
        snprintf( text, sizeof( text ), "RECEIVER: %s", gps.location_valid ? "FIX" : "NO FIX" );
        display_draw_text( 8, 26, text, gps.location_valid ? theme->success : theme->warning, 1U );

        snprintf( text, sizeof( text ), "SAT USED: %u", (unsigned)gps.satellites );
        display_draw_text( 170, 26, text, theme->text, 1U );

        if( gps.location_valid )
        {
            snprintf( text, sizeof( text ), "LAT: %.6f", gps.latitude_deg );
            display_draw_text( 8, 48, text, theme->text, 1U );
            snprintf( text, sizeof( text ), "LON: %.6f", gps.longitude_deg );
            display_draw_text( 8, 66, text, theme->text, 1U );
            snprintf( text, sizeof( text ), "ALT: %.1f m", (double)gps.altitude_m );
            display_draw_text( 8, 84, text, theme->text, 1U );
            snprintf( text, sizeof( text ), "SPEED: %.1f km/h", (double)gps.speed_mps * 3.6 );
            display_draw_text( 8, 102, text, theme->text, 1U );
            snprintf( text, sizeof( text ), "FIX AGE: %lu ms", (unsigned long)gps.location_age_ms );
            display_draw_text( 8, 120, text, theme->info, 1U );
        }
        else
        {
            display_draw_text( 8, 55, "POSITION UNAVAILABLE", theme->warning, 1U );
            display_draw_text( 8, 76, "WAITING FOR FRESH FIX", theme->info, 1U );
        }
    }

    snprintf( text, sizeof( text ), "TRIP: %.2f km", gps_trip_distance_m() / 1000.0 );
    display_draw_text( 8, 141, text, theme->text, 1U );
    display_draw_text( 8,
                       157,
                       a_reset_failed ? "RESET NOT SAVED" : "SELECT: RESET TRIP",
                       a_reset_failed ? theme->warning : theme->info,
                       1U );
    (void)display_present();
}
