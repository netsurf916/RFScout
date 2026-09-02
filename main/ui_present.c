/**
    ui_present.c : RFScout shared UI presentation layer
    Description: Applies global overlays before flushing the application framebuffer.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

#include "power.h"
#include "system_events.h"
#include "ui_theme.h"

#include "esp_heap_caps.h"

#include <stdio.h>
#include <string.h>

#define GLOBAL_OVERLAY_X 42
#define GLOBAL_OVERLAY_Y 50
#define GLOBAL_OVERLAY_WIDTH 236
#define GLOBAL_OVERLAY_HEIGHT 70
#define GLOBAL_OVERLAY_PIXEL_COUNT ( GLOBAL_OVERLAY_WIDTH * GLOBAL_OVERLAY_HEIGHT )

static uint16_t *overlay_backup;

static int text_width( const char *a_text, unsigned a_scale )
{
    return (int)strlen( a_text ) * 6 * (int)a_scale;
}

static int centered_x( const char *a_text, unsigned a_scale )
{
    const int width = text_width( a_text, a_scale );
    return GLOBAL_OVERLAY_X + ( GLOBAL_OVERLAY_WIDTH - width ) / 2;
}

static void draw_overlay_frame( uint16_t a_border_color )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect( GLOBAL_OVERLAY_X,
                       GLOBAL_OVERLAY_Y,
                       GLOBAL_OVERLAY_WIDTH,
                       GLOBAL_OVERLAY_HEIGHT,
                       theme->background );
    display_fill_rect(
        GLOBAL_OVERLAY_X, GLOBAL_OVERLAY_Y, GLOBAL_OVERLAY_WIDTH, 2, a_border_color );
    display_fill_rect( GLOBAL_OVERLAY_X,
                       GLOBAL_OVERLAY_Y + GLOBAL_OVERLAY_HEIGHT - 2,
                       GLOBAL_OVERLAY_WIDTH,
                       2,
                       a_border_color );
    display_fill_rect(
        GLOBAL_OVERLAY_X, GLOBAL_OVERLAY_Y, 2, GLOBAL_OVERLAY_HEIGHT, a_border_color );
    display_fill_rect( GLOBAL_OVERLAY_X + GLOBAL_OVERLAY_WIDTH - 2,
                       GLOBAL_OVERLAY_Y,
                       2,
                       GLOBAL_OVERLAY_HEIGHT,
                       a_border_color );
}

/** @brief Draw the generic power-transition overlay into the active framebuffer. */
static void draw_power_overlay( const power_status_t *a_status )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    draw_overlay_frame( theme->error );

    const char *message = ( '\0' != a_status->message[ 0 ] ) ? a_status->message : "POWER ACTION";
    if( '\0' != a_status->detail[ 0 ] )
    {
        display_draw_text( centered_x( message, 2U ),
                           63,
                           message,
                           ( POWER_HOLD_EXECUTING == a_status->state ) ? theme->error
                                                                       : theme->warning,
                           2U );
        display_draw_text(
            centered_x( a_status->detail, 2U ), 88, a_status->detail, theme->error, 2U );
        return;
    }
    if( POWER_HOLD_EXECUTING == a_status->state )
    {
        display_draw_text( centered_x( message, 2U ), 77, message, theme->error, 2U );
        return;
    }

    display_draw_text( centered_x( message, 2U ), 63, message, theme->warning, 2U );
    char countdown[ 4 ];
    snprintf( countdown, sizeof( countdown ), "%u", (unsigned)a_status->seconds_remaining );
    display_draw_text( centered_x( countdown, 3U ), 88, countdown, theme->error, 3U );
}

static void draw_system_event_overlay( const rfscout_system_event_notification_t *a_notification )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    uint16_t border = theme->accent;
    if( RFSCOUT_SYSTEM_EVENT_WARNING == a_notification->severity )
    {
        border = theme->warning;
    }
    else if( RFSCOUT_SYSTEM_EVENT_CRITICAL == a_notification->severity )
    {
        border = theme->error;
    }
    draw_overlay_frame( border );

    display_draw_text(
        centered_x( a_notification->title, 2U ), 63, a_notification->title, border, 2U );
    if( '\0' != a_notification->detail[ 0 ] )
    {
        display_draw_text( centered_x( a_notification->detail, 2U ),
                           88,
                           a_notification->detail,
                           theme->text,
                           2U );
    }
}

esp_err_t display_present( void )
{
    if( !display_output_enabled() )
    {
        return ESP_OK;
    }
    if( !display_is_dirty() )
    {
        return ESP_OK;
    }

    power_status_t status;
    power_get_status( &status );
    rfscout_system_event_notification_t system_notification;
    const bool system_event_active = system_events_notification_get( &system_notification );
    if( POWER_HOLD_IDLE == status.state && !system_event_active )
    {
        return display_flush();
    }

    if( NULL == overlay_backup )
    {
        overlay_backup = heap_caps_malloc( GLOBAL_OVERLAY_PIXEL_COUNT * sizeof( *overlay_backup ),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    }
    if( NULL == overlay_backup )
    {
        const esp_err_t result = display_flush();
        if( ESP_OK == result )
        {
            display_invalidate();
        }
        return result;
    }

    const esp_err_t saved = display_read_rect( GLOBAL_OVERLAY_X,
                                               GLOBAL_OVERLAY_Y,
                                               GLOBAL_OVERLAY_WIDTH,
                                               GLOBAL_OVERLAY_HEIGHT,
                                               overlay_backup,
                                               GLOBAL_OVERLAY_PIXEL_COUNT );
    if( ESP_OK != saved )
    {
        return saved;
    }

    if( POWER_HOLD_IDLE != status.state )
    {
        draw_power_overlay( &status );
    }
    else
    {
        draw_system_event_overlay( &system_notification );
    }

    const esp_err_t presented = display_flush();
    const esp_err_t restored = display_write_rect( GLOBAL_OVERLAY_X,
                                                   GLOBAL_OVERLAY_Y,
                                                   GLOBAL_OVERLAY_WIDTH,
                                                   GLOBAL_OVERLAY_HEIGHT,
                                                   overlay_backup,
                                                   GLOBAL_OVERLAY_PIXEL_COUNT );
    if( ESP_OK != presented )
    {
        return presented;
    }
    if( ESP_OK != restored )
    {
        return restored;
    }

    /* The LCD contains the intended composed frame while the framebuffer is restored to its
     * overlay-free application state. That restoration is not a reason to send another frame. */
    display_mark_clean();
    return ESP_OK;
}
