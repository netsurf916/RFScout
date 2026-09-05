/**
    apps/audio_spectrum/view.c : RFScout live audio spectrum UI
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "view.h"

#include "display.h"
#include "recorder.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stddef.h>
#include <stdio.h>

void audio_spectrum_view_render( uint8_t a_gain_db, bool a_settings )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "AUDIO SPECTRUM", theme->secondary );

    if( a_settings )
    {
        char line[ 48 ];
        display_draw_text( UI_LAYOUT_LEFT_X, 42, "SETTINGS", theme->text, 2U );
        snprintf( line, sizeof( line ), "> INPUT GAIN  +%u dB", (unsigned)a_gain_db );
        display_draw_text( UI_LAYOUT_LEFT_X, 82, line, theme->selected, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 111, "0-48 dB / 3 dB steps", theme->divider, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_FOOTER_Y,
                           "TURN: ADJUST  CENTER/BACK: CLOSE",
                           theme->secondary,
                           1U );
        (void)display_present();
        return;
    }

    uint8_t bins[ RFSCOUT_RECORDER_MONITOR_BINS ];
    const size_t count = recorder_monitor_bins( bins, sizeof( bins ) );
    char line[ 48 ];
    snprintf( line, sizeof( line ), "LIVE MIC   GAIN +%u dB", (unsigned)a_gain_db );
    display_draw_text( UI_LAYOUT_LEFT_X, 34, line, theme->text, 1U );

    const int graph_x = UI_LAYOUT_LEFT_X;
    const int graph_y = 57;
    const int graph_w = 224;
    const int graph_h = 68;
    if( count > 0U )
    {
        for( size_t index = 0U; index < count; ++index )
        {
            const int x = graph_x + (int)( index * (size_t)graph_w / count );
            const int height = (int)bins[ index ] * graph_h / 100;
            if( height > 0 )
            {
                display_fill_rect( x, graph_y + graph_h - height, 2, height, theme->graph_primary );
            }
        }
    }

    display_draw_text( UI_LAYOUT_LEFT_X, 132, "CENTER: SETTINGS", theme->secondary, 1U );
    display_draw_text(
        UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "TURN: CHANGE APP", theme->secondary, 1U );
    (void)display_present();
}
