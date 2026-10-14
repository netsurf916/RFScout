/**
    apps/audio_spectrum/view.c : RFScout live audio spectrum UI
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "view.h"

#include "display.h"
#include "recorder.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_theme.h"

#include <stddef.h>
#include <stdio.h>

#define AUDIO_SPECTRUM_GRAPH_X 8
#define AUDIO_SPECTRUM_PLOT_Y 32
#define AUDIO_SPECTRUM_PLOT_H ( RFSCOUT_DISPLAY_HEIGHT - 42 )
#define AUDIO_SPECTRUM_LABEL_H 9
#define AUDIO_SPECTRUM_GRAPH_Y ( AUDIO_SPECTRUM_PLOT_Y + AUDIO_SPECTRUM_LABEL_H )
#define AUDIO_SPECTRUM_GRAPH_W ( RFSCOUT_DISPLAY_WIDTH - 16 )
#define AUDIO_SPECTRUM_GRAPH_H ( AUDIO_SPECTRUM_PLOT_H - AUDIO_SPECTRUM_LABEL_H )
#define AUDIO_SPECTRUM_PEAK_MIN_PERCENT 60U
#define AUDIO_SPECTRUM_MAX_PEAK_LABELS 3U
#define AUDIO_SPECTRUM_LABEL_WIDTH 36

typedef struct
{
    size_t bin;
    uint8_t level;
} spectrum_peak_t;

static size_t select_local_peaks( const uint8_t *a_bins,
                                  size_t a_count,
                                  spectrum_peak_t *a_peaks,
                                  size_t a_capacity )
{
    size_t peak_count = 0U;
    if( ( NULL == a_bins ) || ( a_count < 3U ) || ( NULL == a_peaks ) || ( 0U == a_capacity ) )
    {
        return 0U;
    }

    for( size_t bin = 1U; bin + 1U < a_count; ++bin )
    {
        const uint8_t level = a_bins[ bin ];
        if( ( level < AUDIO_SPECTRUM_PEAK_MIN_PERCENT ) || ( level <= a_bins[ bin - 1U ] ) ||
            ( level < a_bins[ bin + 1U ] ) )
        {
            continue;
        }

        size_t insert = 0U;
        while( ( insert < peak_count ) && ( a_peaks[ insert ].level >= level ) )
        {
            ++insert;
        }
        if( insert >= a_capacity )
        {
            continue;
        }

        const size_t last = ( peak_count < a_capacity ) ? peak_count : ( a_capacity - 1U );
        for( size_t move = last; move > insert; --move )
        {
            a_peaks[ move ] = a_peaks[ move - 1U ];
        }
        a_peaks[ insert ].bin = bin;
        a_peaks[ insert ].level = level;
        if( peak_count < a_capacity )
        {
            ++peak_count;
        }
    }
    return peak_count;
}

static void draw_peak_labels( const uint8_t *a_bins, size_t a_count )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    spectrum_peak_t peaks[ AUDIO_SPECTRUM_MAX_PEAK_LABELS ] = { 0 };
    const size_t peak_count =
        select_local_peaks( a_bins, a_count, peaks, AUDIO_SPECTRUM_MAX_PEAK_LABELS );
    int label_x[ AUDIO_SPECTRUM_MAX_PEAK_LABELS ];
    size_t drawn = 0U;

    for( size_t index = 0U; index < peak_count; ++index )
    {
        const int x = AUDIO_SPECTRUM_GRAPH_X +
                      (int)( peaks[ index ].bin * (size_t)AUDIO_SPECTRUM_GRAPH_W / a_count );
        int x_text = x - AUDIO_SPECTRUM_LABEL_WIDTH / 2;
        if( x_text < AUDIO_SPECTRUM_GRAPH_X )
        {
            x_text = AUDIO_SPECTRUM_GRAPH_X;
        }
        const int graph_right = AUDIO_SPECTRUM_GRAPH_X + AUDIO_SPECTRUM_GRAPH_W;
        if( x_text + AUDIO_SPECTRUM_LABEL_WIDTH > graph_right )
        {
            x_text = graph_right - AUDIO_SPECTRUM_LABEL_WIDTH;
        }

        bool overlaps = false;
        for( size_t previous = 0U; previous < drawn; ++previous )
        {
            if( ( x_text < label_x[ previous ] + AUDIO_SPECTRUM_LABEL_WIDTH ) &&
                ( x_text + AUDIO_SPECTRUM_LABEL_WIDTH > label_x[ previous ] ) )
            {
                overlaps = true;
                break;
            }
        }
        if( overlaps )
        {
            continue;
        }

        char label[ 12 ];
        const uint32_t frequency_hz = recorder_monitor_bin_frequency_hz( peaks[ index ].bin );
        if( frequency_hz >= 1000U )
        {
            snprintf( label,
                      sizeof( label ),
                      "~%lu.%luk",
                      (unsigned long)( frequency_hz / 1000U ),
                      (unsigned long)( ( frequency_hz % 1000U ) / 100U ) );
        }
        else
        {
            snprintf( label, sizeof( label ), "~%luHz", (unsigned long)frequency_hz );
        }

        const int height = (int)peaks[ index ].level * AUDIO_SPECTRUM_GRAPH_H / 100;
        int y = AUDIO_SPECTRUM_GRAPH_Y + AUDIO_SPECTRUM_GRAPH_H - height - 9;
        if( y < AUDIO_SPECTRUM_PLOT_Y )
        {
            y = AUDIO_SPECTRUM_PLOT_Y;
        }
        display_draw_text( x_text, y, label, theme->text, 1U );
        label_x[ drawn++ ] = x_text;
    }
}

void audio_spectrum_view_render( void )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "AUDIO SPECTRUM", theme->secondary );

    uint8_t bins[ RFSCOUT_RECORDER_MONITOR_BINS ];
    const size_t count = recorder_monitor_bins( bins, sizeof( bins ) );
    if( ui_game_active() )
    {
        ui_game_draw_panel( AUDIO_SPECTRUM_GRAPH_X - 4,
                            AUDIO_SPECTRUM_PLOT_Y - 4,
                            AUDIO_SPECTRUM_GRAPH_W + 8,
                            AUDIO_SPECTRUM_PLOT_H + 8,
                            theme->divider );
    }

    if( count > 0U )
    {
        for( size_t index = 0U; index < count; ++index )
        {
            const int x =
                AUDIO_SPECTRUM_GRAPH_X + (int)( index * (size_t)AUDIO_SPECTRUM_GRAPH_W / count );
            const int next_x = AUDIO_SPECTRUM_GRAPH_X +
                               (int)( ( index + 1U ) * (size_t)AUDIO_SPECTRUM_GRAPH_W / count );
            const int width = next_x - x > 1 ? next_x - x - 1 : 1;
            const int height = (int)bins[ index ] * AUDIO_SPECTRUM_GRAPH_H / 100;
            if( height > 0 )
            {
                ui_game_draw_column( x,
                                     AUDIO_SPECTRUM_GRAPH_Y + AUDIO_SPECTRUM_GRAPH_H - height,
                                     width,
                                     height,
                                     theme->graph_primary );
            }
        }
        draw_peak_labels( bins, count );
    }

    (void)display_present();
}
