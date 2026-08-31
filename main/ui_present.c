/**
    ui_present.c : RFScout shared UI presentation layer
    Description: Applies global overlays before flushing the application framebuffer.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display.h"

#include "display_idle.h"
#include "power.h"
#include "ui_theme.h"

#include "esp_heap_caps.h"

#include <stdio.h>

#define POWER_OVERLAY_X 42
#define POWER_OVERLAY_Y 50
#define POWER_OVERLAY_WIDTH 236
#define POWER_OVERLAY_HEIGHT 70
#define POWER_OVERLAY_PIXEL_COUNT ( POWER_OVERLAY_WIDTH * POWER_OVERLAY_HEIGHT )

static uint16_t *overlay_backup;

/** @brief Draw the hold-to-power-off overlay into the active framebuffer. */
static void draw_power_overlay( const power_status_t *a_status )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_fill_rect( POWER_OVERLAY_X,
                       POWER_OVERLAY_Y,
                       POWER_OVERLAY_WIDTH,
                       POWER_OVERLAY_HEIGHT,
                       theme->background );
    display_fill_rect( POWER_OVERLAY_X, POWER_OVERLAY_Y, POWER_OVERLAY_WIDTH, 2, theme->error );
    display_fill_rect( POWER_OVERLAY_X,
                       POWER_OVERLAY_Y + POWER_OVERLAY_HEIGHT - 2,
                       POWER_OVERLAY_WIDTH,
                       2,
                       theme->error );
    display_fill_rect( POWER_OVERLAY_X, POWER_OVERLAY_Y, 2, POWER_OVERLAY_HEIGHT, theme->error );
    display_fill_rect( POWER_OVERLAY_X + POWER_OVERLAY_WIDTH - 2,
                       POWER_OVERLAY_Y,
                       2,
                       POWER_OVERLAY_HEIGHT,
                       theme->error );

    if( POWER_HOLD_SHUTDOWN == a_status->state )
    {
        display_draw_text( 91, 77, "POWERING OFF...", theme->error, 2 );
        return;
    }

    display_draw_text( 69, 63, "HOLD TO POWER OFF", theme->warning, 2 );
    char countdown[ 4 ];
    snprintf( countdown, sizeof( countdown ), "%u", (unsigned)a_status->seconds_remaining );
    display_draw_text( 150, 88, countdown, theme->error, 3 );
}

esp_err_t display_present( void )
{
    if( !display_is_dirty() )
    {
        return ESP_OK;
    }

    /* Apps continue composing the logical framebuffer while the display is
       blank. Leave it dirty and skip LCD GRAM transfers until a wake event. */
    if( !display_idle_presentation_enabled() )
    {
        return ESP_OK;
    }

    power_status_t status;
    power_get_status( &status );
    if( POWER_HOLD_IDLE == status.state )
    {
        return display_flush();
    }

    if( NULL == overlay_backup )
    {
        overlay_backup = heap_caps_malloc( POWER_OVERLAY_PIXEL_COUNT * sizeof( *overlay_backup ),
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
    if( ESP_OK != restored )
    {
        return restored;
    }

    /* The LCD contains the intended composed frame while the framebuffer is restored to its
     * overlay-free application state. That restoration is not a reason to send another frame. */
    display_mark_clean();
    return ESP_OK;
}
