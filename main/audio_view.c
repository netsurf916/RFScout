/**
    audio_view.c : RFScout microphone analyzer UI
    Description: Renders the audio spectrum and encoder-driven settings menu.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "audio_view.h"

#include "battery_indicator.h"
#include "display.h"
#include "status_bar.h"

#include <stdio.h>

#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define GRAPH 0x03E0U
#define PEAK 0x07FFU

/** @brief Draw the status bar and battery indicator shared by application views. */
static void draw_status_chrome( void )
{
    status_bar_draw();
    battery_indicator_draw();
}

/** @brief Format a frequency using compact Hz or kHz notation for the graph axis. */
static void format_frequency( char *a_text, size_t a_size, uint32_t a_hz )
{
    if( a_hz >= 1000U )
    {
        snprintf( a_text,
                  a_size,
                  "%lu.%luk",
                  (unsigned long)( a_hz / 1000U ),
                  (unsigned long)( ( a_hz % 1000U ) / 100U ) );
    }
    else
    {
        snprintf( a_text, a_size, "%lu", (unsigned long)a_hz );
    }
}

void audio_view_render( const uint8_t *a_current, const uint8_t *a_peak )
{
    display_clear( BG );
    display_draw_text( 8, 7, "AUDIO", SUBDUED, 2U );
    draw_status_chrome();

    char line[ 64 ];
    snprintf( line,
              sizeof( line ),
              "%s  %s",
              audio_spectrum_range_name( audio_spectrum_range() ),
              audio_spectrum_channel_name( audio_spectrum_channel() ) );
    display_draw_text( 8, 30, line, DIM, 1U );

    const int graph_x = 8;
    const int graph_y = 46;
    const int graph_width = 304;
    const int graph_height = 82;
    display_fill_rect( graph_x, graph_y + graph_height, graph_width, 1, DIM );

    size_t bin_count = audio_spectrum_display_bin_count();
    if( bin_count > AUDIO_SPECTRUM_BINS )
    {
        bin_count = AUDIO_SPECTRUM_BINS;
    }

    size_t strongest = 0U;
    if( ( NULL != a_current ) && ( NULL != a_peak ) && ( bin_count > 0U ) )
    {
        for( size_t i = 0U; i < bin_count; ++i )
        {
            if( a_current[ i ] > a_current[ strongest ] )
            {
                strongest = i;
            }

            const int left = graph_x + (int)( i * (size_t)graph_width / bin_count );
            const int right = graph_x + (int)( ( i + 1U ) * (size_t)graph_width / bin_count );
            int bar_width = right - left;
            if( bar_width < 1 )
            {
                bar_width = 1;
            }

            const int bar_height = (int)a_current[ i ] * ( graph_height - 3 ) / 255;
            if( bar_height > 0 )
            {
                display_fill_rect(
                    left, graph_y + graph_height - bar_height, bar_width, bar_height, GRAPH );
            }

            if( a_peak[ i ] > 0U )
            {
                int peak_y = graph_y + graph_height - (int)a_peak[ i ] * ( graph_height - 3 ) / 255;
                if( peak_y < graph_y )
                {
                    peak_y = graph_y;
                }
                display_fill_rect( left, peak_y, bar_width, 2, PEAK );
            }
        }
    }

    const uint32_t low_hz = audio_spectrum_low_hz();
    const uint32_t high_hz = audio_spectrum_high_hz();
    const uint32_t middle_hz = low_hz + ( high_hz - low_hz ) / 2U;
    char low_text[ 12 ];
    char middle_text[ 12 ];
    char high_text[ 12 ];
    format_frequency( low_text, sizeof( low_text ), low_hz );
    format_frequency( middle_text, sizeof( middle_text ), middle_hz );
    format_frequency( high_text, sizeof( high_text ), high_hz );
    display_draw_text( 8, 132, low_text, DIM, 1U );
    display_draw_text( 145, 132, middle_text, DIM, 1U );
    display_draw_text( 274, 132, high_text, DIM, 1U );

    uint32_t peak_hz = low_hz;
    if( bin_count > 1U )
    {
        peak_hz = low_hz +
                  (uint32_t)( ( (uint64_t)( high_hz - low_hz ) * strongest ) / ( bin_count - 1U ) );
    }

    char peak_text[ 12 ];
    format_frequency( peak_text, sizeof( peak_text ), peak_hz );
    snprintf( line,
              sizeof( line ),
              "PK:%sHz AUTO:%d..%ddB",
              peak_text,
              audio_spectrum_scale_floor_db(),
              audio_spectrum_scale_ceiling_db() );
    display_draw_text( 8, 151, line, DIM, 1U );
    display_draw_text( 250, 151, "SETTINGS", SUBDUED, 1U );
    (void)display_present();
}

void audio_view_menu( unsigned a_selected, bool a_editing, bool a_peak_hold )
{
    static const char *labels[] = { "BACK", "RANGE", "CHANNEL", "PEAK HOLD" };

    display_clear( BG );
    display_draw_text( 8, 7, "AUDIO SETTINGS", SUBDUED, 2U );
    draw_status_chrome();

    for( unsigned i = 0U; i < 4U; ++i )
    {
        const int y = 38 + (int)i * 25;
        const bool active = ( i == a_selected );
        const bool editable = ( i > 0U );
        const char *marker = " ";

        if( active )
        {
            marker = ( editable && a_editing ) ? "*" : ">";
        }

        display_draw_text( 8, y, marker, active ? YELLOW : DIM, 1U );
        display_draw_text( 22, y, labels[ i ], active ? TEXT : DIM, 1U );

        if( 1U == i )
        {
            display_draw_text( 130,
                               y,
                               audio_spectrum_range_name( audio_spectrum_range() ),
                               active ? YELLOW : DIM,
                               1U );
        }
        else if( 2U == i )
        {
            display_draw_text( 130,
                               y,
                               audio_spectrum_channel_name( audio_spectrum_channel() ),
                               active ? YELLOW : DIM,
                               1U );
        }
        else if( 3U == i )
        {
            display_draw_text( 130, y, a_peak_hold ? "ON" : "OFF", active ? YELLOW : DIM, 1U );
        }
    }

    display_draw_text( 8,
                       150,
                       a_editing ? "TURN: CHANGE  CENTER: DONE" : "TURN: SELECT CENTER: ENTER",
                       a_editing ? YELLOW : SUBDUED,
                       1U );
    (void)display_present();
}
