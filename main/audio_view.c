/** audio_view.c : RFScout microphone analyzer UI */
#include "audio_view.h"
#include "display.h"
#include <stdio.h>
#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define GRAPH 0x03E0U
#define PEAK 0x07FFU

void audio_view_render( const uint8_t *a_current, const uint8_t *a_peak )
{
    display_clear( BG );
    display_draw_text( 8, 7, "AUDIO", SUBDUED, 2U );
    char line[ 64 ];
    snprintf( line, sizeof( line ), "%s  %lu-%lu Hz",
              audio_spectrum_range_name( audio_spectrum_range() ),
              (unsigned long)audio_spectrum_low_hz(),
              (unsigned long)audio_spectrum_high_hz() );
    display_draw_text( 8, 30, line, DIM, 1U );
    const int x0 = 8, y0 = 48, width = 304, height = 92;
    display_fill_rect( x0, y0 + height, width, 1, DIM );
    for( unsigned i = 0U; i < AUDIO_SPECTRUM_BINS; ++i )
    {
        const int x = x0 + (int)( i * (unsigned)( width - 1 ) / ( AUDIO_SPECTRUM_BINS - 1U ) );
        const int bar = (int)a_current[ i ] * ( height - 3 ) / 255;
        if( bar > 0 ) display_fill_rect( x, y0 + height - bar, 2, bar, GRAPH );
        int py = y0 + height - (int)a_peak[ i ] * ( height - 3 ) / 255;
        if( py < y0 ) py = y0;
        display_fill_rect( x, py, 2, 2, PEAK );
    }
    snprintf( line, sizeof( line ), "FFT / PEAK HOLD  DROP:%lu",
              (unsigned long)audio_spectrum_overruns() );
    display_draw_text( 8, 148, line, DIM, 1U );
    display_draw_text( 250, 148, "SETTINGS", SUBDUED, 1U );
    (void)display_present();
}

void audio_view_menu( unsigned a_selected, bool a_editing )
{
    display_clear( BG );
    display_draw_text( 8, 7, "AUDIO SETTINGS", SUBDUED, 2U );
    const char *labels[] = { "RANGE", "BACK" };
    for( unsigned i = 0U; i < 2U; ++i )
    {
        const int y = 42 + (int)i * 28;
        const bool active = i == a_selected;
        display_draw_text( 8, y, active ? ( ( 0U == i && a_editing ) ? "*" : ">" ) : " ",
                           active ? YELLOW : DIM, 1U );
        display_draw_text( 22, y, labels[ i ], active ? TEXT : DIM, 1U );
        if( 0U == i )
            display_draw_text( 130, y, audio_spectrum_range_name( audio_spectrum_range() ),
                               active ? YELLOW : DIM, 1U );
    }
    display_draw_text( 8, 150,
                       a_editing ? "TURN: CHANGE  CENTER: DONE" : "TURN: SELECT CENTER: ENTER",
                       a_editing ? YELLOW : SUBDUED, 1U );
    (void)display_present();
}
