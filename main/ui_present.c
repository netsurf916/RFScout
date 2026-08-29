/**
    ui_present.c : RFScout shared UI presentation layer
    Description: Applies global overlays before flushing the application framebuffer.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

#include "battery_indicator.h"
#include "power.h"
#include "status_bar.h"

#include "esp_heap_caps.h"

#include <stdbool.h>
#include <stdio.h>

#define POWER_OVERLAY_X 42
#define POWER_OVERLAY_Y 50
#define POWER_OVERLAY_WIDTH 236
#define POWER_OVERLAY_HEIGHT 70
#define POWER_OVERLAY_PIXEL_COUNT ( POWER_OVERLAY_WIDTH * POWER_OVERLAY_HEIGHT )
#define COLOR_BLACK 0x0000U
#define COLOR_YELLOW 0xFFE0U
#define COLOR_RED 0xF800U

static uint16_t *overlay_backup;
static bool have_presented_power_status;
static power_status_t presented_power_status;

/** @brief Return whether two power-overlay states would render identically. */
static bool power_status_equal( const power_status_t *a_left, const power_status_t *a_right )
{
    return ( a_left->state == a_right->state ) &&
           ( a_left->seconds_remaining == a_right->seconds_remaining );
}

/** @brief Remember the power state represented by the most recent successful LCD flush. */
static void remember_power_status( const power_status_t *a_status )
{
    presented_power_status = *a_status;
    have_presented_power_status = true;
}

/** @brief Draw the hold-to-power-off overlay into the active framebuffer. */
static void draw_power_overlay( const power_status_t *a_status )
{
    display_fill_rect(
        POWER_OVERLAY_X, POWER_OVERLAY_Y, POWER_OVERLAY_WIDTH, POWER_OVERLAY_HEIGHT, COLOR_BLACK );
    display_fill_rect( POWER_OVERLAY_X, POWER_OVERLAY_Y, POWER_OVERLAY_WIDTH, 2, COLOR_RED );
    display_fill_rect( POWER_OVERLAY_X,
                       POWER_OVERLAY_Y + POWER_OVERLAY_HEIGHT - 2,
                       POWER_OVERLAY_WIDTH,
                       2,
                       COLOR_RED );
    display_fill_rect( POWER_OVERLAY_X, POWER_OVERLAY_Y, 2, POWER_OVERLAY_HEIGHT, COLOR_RED );
    display_fill_rect( POWER_OVERLAY_X + POWER_OVERLAY_WIDTH - 2,
                       POWER_OVERLAY_Y,
                       2,
                       POWER_OVERLAY_HEIGHT,
                       COLOR_RED );

    if( POWER_HOLD_SHUTDOWN == a_status->state )
    {
        display_draw_text( 91, 77, "POWERING OFF...", COLOR_RED, 2 );
        return;
    }

    display_draw_text( 69, 63, "HOLD TO POWER OFF", COLOR_YELLOW, 2 );
    char countdown[ 4 ];
    snprintf( countdown, sizeof( countdown ), "%u", (unsigned)a_status->seconds_remaining );
    display_draw_text( 150, 88, countdown, COLOR_RED, 3 );
}

esp_err_t display_present( void )
{
    battery_indicator_draw();
    status_bar_draw();

    power_status_t status;
    power_get_status( &status );
    if( POWER_HOLD_IDLE == status.state )
    {
        const esp_err_t result = display_flush();
        if( ESP_OK == result )
        {
            remember_power_status( &status );
        }
        return result;
    }

    if( NULL == overlay_backup )
    {
        overlay_backup = heap_caps_malloc( POWER_OVERLAY_PIXEL_COUNT * sizeof( *overlay_backup ),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    }
    if( NULL == overlay_backup )
    {
        return display_flush();
    }

    const esp_err_t saved = display_read_rect( POWER_OVERLAY_X,
                                               POWER_OVERLAY_Y,
                                               POWER_OVERLAY_WIDTH,
                                               POWER_OVERLAY_HEIGHT,
                                               overlay_backup,
                                               POWER_OVERLAY_PIXEL_COUNT );
    if( ESP_OK != saved )
    {
        return saved;
    }

    draw_power_overlay( &status );
    const esp_err_t presented = display_flush();
    const esp_err_t restored = display_write_rect( POWER_OVERLAY_X,
                                                   POWER_OVERLAY_Y,
                                                   POWER_OVERLAY_WIDTH,
                                                   POWER_OVERLAY_HEIGHT,
                                                   overlay_backup,
                                                   POWER_OVERLAY_PIXEL_COUNT );
    if( ESP_OK != presented )
    {
        return presented;
    }

    remember_power_status( &status );
    return restored;
}

esp_err_t display_present_poll( void )
{
    power_status_t status;
    power_get_status( &status );

    if( have_presented_power_status && power_status_equal( &status, &presented_power_status ) )
    {
        return ESP_OK;
    }

    return display_present();
}
