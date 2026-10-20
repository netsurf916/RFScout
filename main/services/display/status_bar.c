/**
    status_bar.c : RFScout top status bar implementation
    Description: Snapshots and renders compact right-aligned global system status.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "status_bar.h"

#include "app_manager.h"
#include "device_time.h"
#include "display.h"
#include "gps.h"
#include "input.h"
#include "log_writer.h"
#include "media_store.h"
#include "mlx90640.h"
#include "ui_theme.h"
#include "ui_game.h"
#include "ui_icon.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define STATUS_BAR_LEFT_X 142
#define STATUS_BAR_RIGHT_X 249
#define STATUS_BAR_HEIGHT 20
#define STATUS_BAR_ICON_Y 4
#define STATUS_BAR_ICON_WIDTH 12
#define STATUS_BAR_ICON_HEIGHT 14
#define STATUS_BAR_ICON_GAP 2
#define STATUS_BAR_CLOCK_GAP 2

static bool have_last_state;
static rfscout_status_bar_state_t last_state;

static void draw_lock_icon( int a_x )
{
    ui_icon_draw_lock( a_x, STATUS_BAR_ICON_Y, false, ui_theme_get()->warning );
}

static void draw_app_lock_icon( int a_x )
{
    ui_icon_draw_lock( a_x, STATUS_BAR_ICON_Y, true, ui_theme_get()->accent );
}

static void draw_sd_icon( int a_x, uint16_t a_color, bool a_error )
{
    ui_icon_draw_sd( a_x, STATUS_BAR_ICON_Y, a_color, a_error );
}

static void draw_thermal_icon( int a_x )
{
    ui_icon_draw_thermal( a_x, STATUS_BAR_ICON_Y, ui_theme_get()->info );
}

static void draw_gps_icon( int a_x, bool a_fix_valid )
{
    const int y = STATUS_BAR_ICON_Y;
    const ui_theme_palette_t *theme = ui_theme_get();
    const uint16_t color = a_fix_valid ? theme->success : theme->warning;

    ui_game_draw_gps_satellite( a_x, y, color );
}

static bool status_equal( const rfscout_status_bar_state_t *a_left,
                          const rfscout_status_bar_state_t *a_right )
{
    return ( a_left->input_locked == a_right->input_locked ) &&
           ( a_left->app_locked == a_right->app_locked ) &&
           ( a_left->sd_available == a_right->sd_available ) &&
           ( a_left->storage_error == a_right->storage_error ) &&
           ( a_left->thermal_available == a_right->thermal_available ) &&
           ( a_left->gps_available == a_right->gps_available ) &&
           ( a_left->gps_fix_valid == a_right->gps_fix_valid ) &&
           ( 0 == strcmp( a_left->clock, a_right->clock ) );
}

bool status_bar_snapshot( rfscout_status_bar_state_t *a_state )
{
    if( NULL == a_state )
    {
        return false;
    }

    memset( a_state, 0, sizeof( *a_state ) );
    a_state->input_locked = input_locked();
    a_state->app_locked = app_manager_locked();
    a_state->sd_available = media_store_available();
    a_state->storage_error = log_writer_has_error();
    a_state->thermal_available = mlx90640_is_detected();

    rfscout_gps_snapshot_t gps;
    if( gps_snapshot_get( &gps ) )
    {
        a_state->gps_available = gps.available;
        a_state->gps_fix_valid = gps.location_valid;
    }

    (void)device_time_format_clock( a_state->clock, sizeof( a_state->clock ) );
    return true;
}

void status_bar_draw_state( const rfscout_status_bar_state_t *a_state )
{
    if( NULL == a_state )
    {
        return;
    }

    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect( STATUS_BAR_LEFT_X,
                       0,
                       STATUS_BAR_RIGHT_X - STATUS_BAR_LEFT_X,
                       STATUS_BAR_HEIGHT,
                       theme->background );

    int x = STATUS_BAR_RIGHT_X;
    if( a_state->input_locked )
    {
        x -= STATUS_BAR_ICON_WIDTH;
        draw_lock_icon( x );
        x -= STATUS_BAR_ICON_GAP;
    }
    if( a_state->app_locked && !a_state->input_locked )
    {
        x -= STATUS_BAR_ICON_WIDTH;
        draw_app_lock_icon( x );
        x -= STATUS_BAR_ICON_GAP;
    }
    if( a_state->sd_available )
    {
        x -= STATUS_BAR_ICON_WIDTH;
        const uint16_t color = a_state->storage_error ? theme->error : theme->info;
        draw_sd_icon( x, color, a_state->storage_error );
        x -= STATUS_BAR_ICON_GAP;
    }
    if( a_state->thermal_available )
    {
        x -= STATUS_BAR_ICON_WIDTH;
        draw_thermal_icon( x );
        x -= STATUS_BAR_ICON_GAP;
    }
    if( a_state->gps_available )
    {
        x -= STATUS_BAR_ICON_WIDTH;
        draw_gps_icon( x, a_state->gps_fix_valid );
        x -= STATUS_BAR_ICON_GAP;
    }

    /* A seconds clock can overlap the title when four indicators are present.
       Drop just the seconds when the available status region cannot contain it. */
    char clock[ RFSCOUT_STATUS_CLOCK_MAX ];
    memcpy( clock, a_state->clock, sizeof( clock ) );
    clock[ sizeof( clock ) - 1U ] = '\0';
    int clock_width = (int)strlen( clock ) * 6;
    if( clock_width > 0 && x - STATUS_BAR_CLOCK_GAP - clock_width < STATUS_BAR_LEFT_X )
    {
        /* Both 24-hour HH:MM:SS and 12-hour HH:MM:SSAM have seconds at [5..7]. */
        if( strlen( clock ) >= 8U && ':' == clock[ 5 ] )
        {
            memmove( clock + 5, clock + 8, strlen( clock + 8 ) + 1U );
            clock_width = (int)strlen( clock ) * 6;
        }
    }
    if( clock_width > 0 && x - STATUS_BAR_CLOCK_GAP - clock_width >= STATUS_BAR_LEFT_X )
    {
        x -= STATUS_BAR_CLOCK_GAP + clock_width;
        display_draw_text( x, STATUS_BAR_ICON_Y + 3, clock, theme->info, 1U );
    }

    last_state = *a_state;
    have_last_state = true;
}

void status_bar_draw( void )
{
    rfscout_status_bar_state_t state;
    if( status_bar_snapshot( &state ) )
    {
        status_bar_draw_state( &state );
    }
}

void status_bar_poll( void )
{
    rfscout_status_bar_state_t state;
    if( !status_bar_snapshot( &state ) )
    {
        return;
    }
    if( have_last_state && status_equal( &state, &last_state ) )
    {
        return;
    }
    status_bar_draw_state( &state );
}
