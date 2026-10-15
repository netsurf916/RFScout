/**
    apps/diagnostics/app.c : Opt-in display diagnostics
    Description: Hardware RGB565 swatches and semantic palette verification.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "display.h"
#include "ui_chrome.h"
#include "ui_theme.h"

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    const char *label;
    uint16_t color;
} diagnostic_color_t;

static bool show_theme_palette;

static void draw_swatch( int a_index, const char *a_name, uint16_t a_color )
{
    const int x = 6 + ( a_index % 3 ) * 105;
    const int y = 40 + ( a_index / 3 ) * 60;
    /* Raw writes intentionally bypass semantic color mapping entirely. */
    display_fill_rect_raw( x, y, 98, 32, a_color );
    display_draw_text( x, y + 35, a_name, 0xFFFFU, 1U );
}

static void diagnostics_render( void )
{
    /* The background and labels are literal RGB565 in the diagnostic view. */
    display_clear( 0x0000U );
    ui_chrome_draw( show_theme_palette ? "DIAG / THEME" : "DIAG / RGB565", 0xFFFFU );

    if( !show_theme_palette )
    {
        static const diagnostic_color_t colors[] = {
            { "RED F800", 0xF800U },
            { "GREEN 07E0", 0x07E0U },
            { "BLUE 001F", 0x001FU },
            { "ORANGE FC00", 0xFC00U },
            { "CYAN 07FF", 0x07FFU },
            { "WHITE FFFF", 0xFFFFU },
        };
        for( size_t i = 0U; i < sizeof( colors ) / sizeof( colors[ 0 ] ); ++i )
        {
            draw_swatch( (int)i, colors[ i ].label, colors[ i ].color );
        }
    }
    else
    {
        const ui_theme_palette_t *theme = ui_theme_get();
        const diagnostic_color_t colors[] = {
            { "BACKGROUND", theme->background },
            { "TEXT", theme->text },
            { "ACCENT", theme->accent },
            { "LOGO PRI", theme->logo_primary },
            { "LOGO SEC", theme->logo_secondary },
            { "SELECTED", theme->selected },
        };
        for( size_t i = 0U; i < sizeof( colors ) / sizeof( colors[ 0 ] ); ++i )
        {
            draw_swatch( (int)i, colors[ i ].label, colors[ i ].color );
        }
    }
    display_draw_text( 6, 159, "CENTER: SWITCH TEST", 0xFFFFU, 1U );
    (void)display_present();
}

static rfscout_app_runtime_result_t
diagnostics_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        show_theme_palette = false;
        diagnostics_render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type && RFSCOUT_INPUT_SELECT == a_event->input.type )
    {
        show_theme_palette = !show_theme_palette;
        diagnostics_render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type &&
        RFSCOUT_INPUT_NAVIGATION == a_event->input.type )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
}

const rfscout_app_descriptor_t rfscout_diagnostics_app = {
    .id = RFSCOUT_APP_DIAGNOSTICS,
    .name = "Diagnostics",
    .persistence_key = "diagnostics",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = false,
    .services = RFSCOUT_APP_SERVICE_NONE,
    .runtime = diagnostics_runtime,
};
