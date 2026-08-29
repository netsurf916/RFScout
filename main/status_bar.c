/**
    status_bar.c : RFScout top status bar implementation
    Description: Snapshots and renders compact right-aligned global system status.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "status_bar.h"

#include "device_time.h"
#include "display.h"
#include "log_writer.h"
#include "mlx90640.h"
#include "platform.h"
#include "storage.h"

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

#define STATUS_BAR_COLOR_NORMAL 0x03F0U
#define STATUS_BAR_COLOR_ATTENTION 0xFFE1U
#define STATUS_BAR_COLOR_ERROR 0xF800U
#define STATUS_BAR_COLOR_BACKGROUND 0x0000U

static bool have_last_state;
static rfscout_status_bar_state_t last_state;

/** @brief Draw a compact closed-padlock glyph for the global input lock. */
static void draw_lock_icon( int a_x )
{
    const int y = STATUS_BAR_ICON_Y;
    const uint16_t color = STATUS_BAR_COLOR_ATTENTION;

    /* Rounded shackle. */
    display_fill_rect( a_x + 3, y + 1, 6, 1, color );
    display_fill_rect( a_x + 2, y + 2, 1, 4, color );
    display_fill_rect( a_x + 9, y + 2, 1, 4, color );

    /* Solid lock body with a tiny keyway cutout. */
    display_fill_rect( a_x + 1, y + 6, 10, 7, color );
    display_fill_rect( a_x + 5, y + 8, 2, 3, STATUS_BAR_COLOR_BACKGROUND );
}

/** @brief Draw a compact SD-card glyph using the supplied subdued/error color. */
static void draw_sd_icon( int a_x, uint16_t a_color, bool a_error )
{
    const int y = STATUS_BAR_ICON_Y;

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
        display_fill_rect( a_x + 3, y + 8, 6, 3, STATUS_BAR_COLOR_BACKGROUND );
        display_fill_rect( a_x + 5, y + 8, 2, 2, a_color );
        display_fill_rect( a_x + 5, y + 11, 2, 1, a_color );
    }
}

/** @brief Draw a subdued thermal-camera glyph when the MLX90640 is available. */
static void draw_thermal_icon( int a_x )
{
    const int y = STATUS_BAR_ICON_Y;
    const uint16_t color = STATUS_BAR_COLOR_NORMAL;

    display_fill_rect( a_x + 1, y + 3, 11, 8, color );
    display_fill_rect( a_x + 3, y + 1, 5, 2, color );
    display_fill_rect( a_x + 3, y + 5, 7, 4, STATUS_BAR_COLOR_BACKGROUND );
    display_fill_rect( a_x + 5, y + 5, 3, 4, color );
}

/** @brief Return whether two status snapshots would render identically. */
static bool status_equal( const rfscout_status_bar_state_t *a_left,
                          const rfscout_status_bar_state_t *a_right )
{
    return ( a_left->input_locked == a_right->input_locked ) &&
           ( a_left->sd_available == a_right->sd_available ) &&
           ( a_left->storage_error == a_right->storage_error ) &&
           ( a_left->thermal_available == a_right->thermal_available ) &&
           ( 0 == strcmp( a_left->clock, a_right->clock ) );
}

bool status_bar_snapshot( rfscout_status_bar_state_t *a_state )
{
    if( NULL == a_state )
    {
        return false;
    }

    memset( a_state, 0, sizeof( *a_state ) );
    a_state->input_locked = platform_input_locked();
    a_state->sd_available = storage_is_available();
    a_state->storage_error = log_writer_has_error();
    a_state->thermal_available = mlx90640_is_detected();
    (void)device_time_format_clock( a_state->clock, sizeof( a_state->clock ) );
    return true;
}

void status_bar_draw_state( const rfscout_status_bar_state_t *a_state )
{
    if( NULL == a_state )
    {
        return;
    }

    display_fill_rect( STATUS_BAR_LEFT_X,
                       0,
                       STATUS_BAR_RIGHT_X - STATUS_BAR_LEFT_X,
                       STATUS_BAR_HEIGHT,
                       STATUS_BAR_COLOR_BACKGROUND );

    int x = STATUS_BAR_RIGHT_X;

    if( a_state->input_locked )
    {
        x -= STATUS_BAR_ICON_WIDTH;
        draw_lock_icon( x );
        x -= STATUS_BAR_ICON_GAP;
    }

    if( a_state->sd_available )
    {
        x -= STATUS_BAR_ICON_WIDTH;
        const uint16_t color =
            a_state->storage_error ? STATUS_BAR_COLOR_ERROR : STATUS_BAR_COLOR_NORMAL;
        draw_sd_icon( x, color, a_state->storage_error );
        x -= STATUS_BAR_ICON_GAP;
    }

    if( a_state->thermal_available )
    {
        x -= STATUS_BAR_ICON_WIDTH;
        draw_thermal_icon( x );
        x -= STATUS_BAR_ICON_GAP;
    }

    const int clock_width = (int)strlen( a_state->clock ) * 6;
    if( clock_width > 0 )
    {
        x -= STATUS_BAR_CLOCK_GAP + clock_width;
        display_draw_text( x, STATUS_BAR_ICON_Y + 3, a_state->clock, STATUS_BAR_COLOR_NORMAL, 1U );
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
