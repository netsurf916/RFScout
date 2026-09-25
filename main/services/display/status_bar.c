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
#include "mlx90640.h"
#include "storage.h"
#include "ui_theme.h"

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
    const int y = STATUS_BAR_ICON_Y;
    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect( a_x + 3, y + 1, 6, 1, theme->warning );
    display_fill_rect( a_x + 2, y + 2, 1, 4, theme->warning );
    display_fill_rect( a_x + 9, y + 2, 1, 4, theme->warning );
    display_fill_rect( a_x + 1, y + 6, 10, 7, theme->warning );
    display_fill_rect( a_x + 5, y + 8, 2, 3, theme->background );
}

static void draw_app_lock_icon( int a_x )
{
    const int y = STATUS_BAR_ICON_Y;
    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect( a_x + 3, y + 1, 6, 1, theme->accent );
    display_fill_rect( a_x + 2, y + 2, 1, 4, theme->accent );
    display_fill_rect( a_x + 9, y + 2, 1, 4, theme->accent );
    display_fill_rect( a_x + 1, y + 6, 10, 7, theme->accent );
    display_draw_text( a_x + 3, y + 7, "A", theme->background, 1U );
}

static void draw_sd_icon( int a_x, uint16_t a_color, bool a_error )
{
    const int y = STATUS_BAR_ICON_Y;
    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect( a_x, y + 2, 1, 11, a_color );
    display_fill_rect( a_x + 1, y + 1, 8, 1, a_color );
    display_fill_rect( a_x + 9, y + 2, 2, 1, a_color );
    display_fill_rect( a_x + 11, y + 3, 1, 10, a_color );
    display_fill_rect( a_x, y + 12, 12, 1, a_color );
    display_fill_rect( a_x + 2, y + 4, 2, 3, a_color );
    display_fill_rect( a_x + 5, y + 4, 2, 3, a_color );
    display_fill_rect( a_x + 8, y + 4, 2, 3, a_color );
    if( a_error )
    {
        display_fill_rect( a_x + 3, y + 8, 6, 3, theme->background );
        display_fill_rect( a_x + 5, y + 8, 2, 2, a_color );
        display_fill_rect( a_x + 5, y + 11, 2, 1, a_color );
    }
}

static void draw_thermal_icon( int a_x )
{
    const int y = STATUS_BAR_ICON_Y;
    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect( a_x + 1, y + 3, 11, 8, theme->info );
    display_fill_rect( a_x + 3, y + 1, 5, 2, theme->info );
    display_fill_rect( a_x + 3, y + 5, 7, 4, theme->background );
    display_fill_rect( a_x + 5, y + 5, 3, 4, theme->info );
}

static void draw_gps_icon( int a_x, bool a_fix_valid )
{
    const int y = STATUS_BAR_ICON_Y;
    const ui_theme_palette_t *theme = ui_theme_get();
    const uint16_t color = a_fix_valid ? theme->success : theme->warning;

    display_draw_text( a_x + 3, y + 3, "G", color, 1U );
    display_fill_rect( a_x + 1, y + 7, 2, 1, color );
    display_fill_rect( a_x + 9, y + 7, 2, 1, color );
    display_fill_rect( a_x + 5, y + 1, 1, 2, color );
    display_fill_rect( a_x + 5, y + 12, 1, 2, color );
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
    a_state->sd_available = storage_is_available();
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

    const int clock_width = (int)strlen( a_state->clock ) * 6;
    if( clock_width > 0 )
    {
        x -= STATUS_BAR_CLOCK_GAP + clock_width;
        display_draw_text( x, STATUS_BAR_ICON_Y + 3, a_state->clock, theme->info, 1U );
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
