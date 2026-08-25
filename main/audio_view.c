/** audio_view.c : RFScout microphone analyzer UI */
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

static void draw_status_chrome( void )
{
    status_bar_draw();
    battery_indicator_draw();
}

static void format_frequency( char *a_text, size_t a_size, uint32_t a_hz )
{
    if( a_hz >= 1000U )
    {
        snprintf( a_text, a_size, "%lu.%luk", (unsigned long)( a_hz / 1000U ),
                  (unsigned long)( ( a_hz % 1000U ) / 100U ) );
    }
    else snprintf( a_text, a_size, "%lu", (unsigned long)a_hz );
}

void audio_view_render( const uint8_t *a_current, const uint8_t *a_peak )
{
    display_clear( BG );
    display_draw_text( 8, 7, "AUDIO", SUBDUED, 2U );
    draw_status_chrome();
    char line[ 64 ];
    snprintf( line, sizeof( line ), "%s  %s", audio_spectrum_range_name( audio_spectrum_range() ),
              audio_spectrum_channel_name( audio_spectrum_channel() ) );
    display_draw_text( 8, 30, line, DIM, 1U );
    const int x0 = 8, y0 = 46, width = 304, height = 82;
    display_fill_rect( x0, y0 + height, width, 1, DIM );
    size_t bin_count = audio_spectrum_display_bin_count();
    if( bin_count > AUDIO_SPECTRUM_BINS ) bin_count = AUDIO_SPECTRUM_BINS;
    size_t strongest = 0U;
    if( bin_count > 0U )
    {
        for( size_t i = 0U; i < bin_count; ++i )
        {
            if( a_current[ i ] > a_current[ strongest ] ) strongest = i;
            const int left = x0 + (int)( i * (size_t)width / bin_count );
            const int right = x0 + (int)( ( i + 1U ) * (size_t)width / bin_count );
            int bar_width = right - left; if( bar_width < 1 ) bar_width = 1;
            const int bar = (int)a_current[ i ] * ( height - 3 ) / 255;
            if( bar > 0 ) display_fill_rect( left, y0 + height - bar, bar_width, bar, GRAPH );
            if( a_peak[ i ] > 0U )
            {
                int py = y0 + height - (int)a_peak[ i ] * ( height - 3 ) / 255;
                if( py < y0 ) py = y0;
                display_fill_rect( left, py, bar_width, 2, PEAK );
            }
        }
    }
    const uint32_t low = audio_spectrum_low_hz(), high = audio_spectrum_high_hz();
    const uint32_t middle = low + ( high - low ) / 2U;
    char low_text[ 12 ], mid_text[ 12 ], high_text[ 12 ];
    format_frequency( low_text, sizeof( low_text ), low );
    format_frequency( mid_text, sizeof( mid_text ), middle );
    format_frequency( high_text, sizeof( high_text ), high );
    display_draw_text( 8, 132, low_text, DIM, 1U );
    display_draw_text( 145, 132, mid_text, DIM, 1U );
    display_draw_text( 274, 132, high_text, DIM, 1U );
    uint32_t peak_hz = low;
    if( bin_count > 1U ) peak_hz = low + (uint32_t)( ( (uint64_t)( high - low ) * strongest ) / ( bin_count - 1U ) );
    char peak_text[ 12 ]; format_frequency( peak_text, sizeof( peak_text ), peak_hz );
    snprintf( line, sizeof( line ), "PK:%sHz AUTO:%d..%ddB", peak_text,
              audio_spectrum_scale_floor_db(), audio_spectrum_scale_ceiling_db() );
    display_draw_text( 8, 151, line, DIM, 1U );
    display_draw_text( 250, 151, "SETTINGS", SUBDUED, 1U );
    (void)display_present();
}

void audio_view_menu( unsigned a_selected, bool a_editing, bool a_peak_hold )
{
    display_clear( BG );
    display_draw_text( 8, 7, "AUDIO SETTINGS", SUBDUED, 2U );
    draw_status_chrome();
    const char *labels[] = { "RANGE", "CHANNEL", "PEAK HOLD", "BACK" };
    for( unsigned i = 0U; i < 4U; ++i )
    {
        const int y = 38 + (int)i * 25;
        const bool active = i == a_selected;
        const bool editable = i < 3U;
        display_draw_text( 8, y, active ? ( ( editable && a_editing ) ? "*" : ">" ) : " ",
                           active ? YELLOW : DIM, 1U );
        display_draw_text( 22, y, labels[ i ], active ? TEXT : DIM, 1U );
        if( 0U == i ) display_draw_text( 130, y, audio_spectrum_range_name( audio_spectrum_range() ), active ? YELLOW : DIM, 1U );
        else if( 1U == i ) display_draw_text( 130, y, audio_spectrum_channel_name( audio_spectrum_channel() ), active ? YELLOW : DIM, 1U );
        else if( 2U == i ) display_draw_text( 130, y, a_peak_hold ? "ON" : "OFF", active ? YELLOW : DIM, 1U );
    }
    display_draw_text( 8, 150, a_editing ? "TURN: CHANGE  CENTER: DONE" : "TURN: SELECT CENTER: ENTER",
                       a_editing ? YELLOW : SUBDUED, 1U );
    (void)display_present();
}
