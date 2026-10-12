/**
    ui_present.c : RFScout shared UI presentation layer
    Description: Applies system-owned navigation and global overlays before flushing the framebuffer.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

#include "power.h"
#include "system_events.h"
#include "ui_theme.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#define TRANSITION_NOTICE_US 1800000LL
#define GLOBAL_OVERLAY_X 42
#define GLOBAL_OVERLAY_Y 50
#define GLOBAL_OVERLAY_WIDTH 236
#define GLOBAL_OVERLAY_HEIGHT 70
#define GLOBAL_OVERLAY_PIXEL_COUNT ( GLOBAL_OVERLAY_WIDTH * GLOBAL_OVERLAY_HEIGHT )

static uint16_t *overlay_backup;
static bool modal_active;
static char transition_message[ 32 ];
static int64_t transition_notice_expires_us;
static char modal_title[ 32 ];
static char modal_detail[ 32 ];

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

void display_modal_show( const char *a_title, const char *a_detail )
{
    const char *title = ( NULL != a_title ) ? a_title : "";
    const char *detail = ( NULL != a_detail ) ? a_detail : "";
    if( modal_active && 0 == strcmp( modal_title, title ) && 0 == strcmp( modal_detail, detail ) )
    {
        return;
    }

    modal_active = true;
    snprintf( modal_title, sizeof( modal_title ), "%s", title );
    snprintf( modal_detail, sizeof( modal_detail ), "%s", detail );
    display_invalidate();
}

void display_modal_clear( void )
{
    if( !modal_active )
    {
        return;
    }
    modal_active = false;
    modal_title[ 0 ] = '\0';
    modal_detail[ 0 ] = '\0';
    display_invalidate();
}

void display_transition_notice_show( const char *a_message )
{
    if( NULL == a_message || '\0' == a_message[ 0 ] )
    {
        return;
    }
    (void)snprintf( transition_message, sizeof( transition_message ), "%s", a_message );
    transition_notice_expires_us = esp_timer_get_time() + TRANSITION_NOTICE_US;
    display_invalidate();
}

void display_transition_notice_poll( void )
{
    if( 0 != transition_notice_expires_us &&
        esp_timer_get_time() >= transition_notice_expires_us )
    {
        transition_notice_expires_us = 0;
        display_invalidate();
    }
}

static void draw_transition_notice( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    draw_overlay_frame( theme->warning );
    display_draw_text(
        centered_x( "NAVIGATION BLOCKED", 1U ), 63, "NAVIGATION BLOCKED", theme->warning, 1U );
    display_draw_text(
        centered_x( transition_message, 1U ), 88, transition_message, theme->text, 1U );
}

static void draw_modal_overlay( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    draw_overlay_frame( theme->accent );
    display_draw_text( centered_x( modal_title, 2U ), 63, modal_title, theme->accent, 2U );
    if( '\0' != modal_detail[ 0 ] )
    {
        display_draw_text( centered_x( modal_detail, 2U ), 88, modal_detail, theme->text, 2U );
    }
}

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
        display_draw_text(
            centered_x( a_notification->detail, 2U ), 88, a_notification->detail, theme->text, 2U );
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
    if( POWER_HOLD_IDLE == status.state && !modal_active && !system_event_active &&
        0 == transition_notice_expires_us )
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
    else if( modal_active )
    {
        draw_modal_overlay();
    }
    else if( system_event_active )
    {
        draw_system_event_overlay( &system_notification );
    }
    else
    {
        draw_transition_notice();
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

    display_mark_clean();
    return ESP_OK;
}
