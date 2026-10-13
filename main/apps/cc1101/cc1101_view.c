/**
    cc1101_view.c : RFScout CC1101 discovery and receive views
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "cc1101_view.h"

#include "display.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stdio.h>

#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define CC1101_MENU_ITEM_COUNT 9U
#define GRAPH_TOP 48
#define GRAPH_HEIGHT 88
#define TIMING_ROWS 6U

static void format_mhz( char *a_buffer, size_t a_capacity, uint32_t a_frequency_khz )
{
    snprintf( a_buffer,
              a_capacity,
              "%lu.%03lu",
              (unsigned long)( a_frequency_khz / 1000U ),
              (unsigned long)( a_frequency_khz % 1000U ) );
}

static const char *view_name( cc1101_view_mode_t a_view )
{
    switch( a_view )
    {
        case CC1101_VIEW_DECODED:
            return "DECODE";
        case CC1101_VIEW_TIMING:
            return "TIMING";
        case CC1101_VIEW_WAVEFORM:
            return "WAVE";
        case CC1101_VIEW_COUNT:
        default:
            return "?";
    }
}

void cc1101_view_discovery( const int8_t *a_rssi,
                            size_t a_count,
                            const cc1101_scan_config_t *a_config,
                            uint32_t a_peak_khz,
                            int8_t a_peak_rssi,
                            bool a_detected,
                            bool a_app_locked )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( BG );
    ui_chrome_draw( "CC1101 / DISCOVERY", SUBDUED );

    if( !a_detected || ( NULL == a_config ) )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 70, "CC1101 NOT DETECTED", YELLOW, 1U );
        (void)display_present();
        return;
    }

    char low[ 20 ];
    char high[ 20 ];
    char peak[ 20 ];
    char line[ 72 ];
    format_mhz( low, sizeof( low ), a_config->low_khz );
    format_mhz( high, sizeof( high ), a_config->high_khz );
    format_mhz( peak, sizeof( peak ), a_peak_khz );
    snprintf( line,
              sizeof( line ),
              "%s-%s MHz  STEP %luK",
              low,
              high,
              (unsigned long)a_config->step_khz );
    display_draw_text( UI_LAYOUT_LEFT_X, 30, line, DIM, 1U );

    const int graph_x = UI_LAYOUT_LEFT_X;
    const int graph_width = RFSCOUT_DISPLAY_WIDTH - ( UI_LAYOUT_LEFT_X * 2 );
    if( ui_game_active() )
    {
        ui_game_draw_panel(
            graph_x - 2, GRAPH_TOP - 2, graph_width + 4, GRAPH_HEIGHT + 5, theme->divider );
    }
    display_fill_rect( graph_x, GRAPH_TOP + GRAPH_HEIGHT, graph_width, 1, theme->divider );

    if( ( NULL != a_rssi ) && ( a_count > 0U ) )
    {
        for( size_t index = 0U; index < a_count; ++index )
        {
            int level = (int)a_rssi[ index ] + 110;
            if( level < 0 )
            {
                level = 0;
            }
            if( level > 80 )
            {
                level = 80;
            }

            const size_t denominator = ( a_count > 1U ) ? a_count - 1U : 1U;
            const int x = graph_x + (int)( index * (size_t)( graph_width - 1 ) / denominator );
            const int height = level * ( GRAPH_HEIGHT - 2 ) / 80;
            if( height > 0 )
            {
                ui_game_draw_column(
                    x, GRAPH_TOP + GRAPH_HEIGHT - height, 2, height, theme->graph_primary );
            }
        }
    }

    snprintf( line, sizeof( line ), "PEAK %s MHz  %d dBm", peak, (int)a_peak_rssi );
    display_draw_text( UI_LAYOUT_LEFT_X, 142, line, theme->secondary, 1U );
    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       a_app_locked ? "TURN: VIEWS  CENTER: SETTINGS"
                                    : "TURN: APPS  LONG PRESS: VIEW  CENTER: SETTINGS",
                       SUBDUED,
                       1U );
    (void)display_present();
}

static void render_decode( const rfscout_subghz_analysis_t *a_analysis,
                           const cc1101_capture_stats_t *a_stats )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    char line[ 80 ];

    if( ( NULL == a_analysis ) || ( 0U == a_analysis->sequence ) )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 58, "WAITING FOR SIGNAL", theme->secondary, 1U );
    }
    else if( RFSCOUT_SUBGHZ_DECODE_MATCH == a_analysis->protocol.status )
    {
        snprintf( line, sizeof( line ), "%s", a_analysis->protocol.protocol );
        display_draw_text( UI_LAYOUT_LEFT_X, 50, line, theme->warning, 2U );
        snprintf( line,
                  sizeof( line ),
                  "%u bits  x%u",
                  (unsigned)a_analysis->protocol.bit_count,
                  (unsigned)a_analysis->protocol.repeats );
        display_draw_text( UI_LAYOUT_LEFT_X, 82, line, theme->secondary, 1U );
        snprintf(
            line, sizeof( line ), "KEY %06llX", (unsigned long long)a_analysis->protocol.data );
        display_draw_text( UI_LAYOUT_LEFT_X, 99, line, theme->text, 1U );
        snprintf( line,
                  sizeof( line ),
                  "PERIOD ~%lu us",
                  (unsigned long)a_analysis->protocol.symbol_period_us );
        display_draw_text( UI_LAYOUT_LEFT_X, 116, line, theme->secondary, 1U );
    }
    else
    {
        snprintf( line,
                  sizeof( line ),
                  "UNKNOWN  %u RMT / %u levels",
                  (unsigned)a_analysis->symbol_count,
                  (unsigned)a_analysis->timed_level_count );
        display_draw_text( UI_LAYOUT_LEFT_X, 58, line, theme->secondary, 1U );
        snprintf( line,
                  sizeof( line ),
                  "%lu-%lu us",
                  (unsigned long)a_analysis->min_duration_us,
                  (unsigned long)a_analysis->max_duration_us );
        display_draw_text( UI_LAYOUT_LEFT_X, 77, line, theme->text, 1U );
    }

    if( NULL != a_stats )
    {
        snprintf( line,
                  sizeof( line ),
                  "CAP %lu  TRUNC %lu  OVR %lu",
                  (unsigned long)a_stats->captures_completed,
                  (unsigned long)a_stats->captures_truncated,
                  (unsigned long)a_stats->capture_overruns );
        display_draw_text( UI_LAYOUT_LEFT_X, 143, line, theme->divider, 1U );
    }
}

static void render_timing( const rfscout_subghz_analysis_t *a_analysis )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    char line[ 40 ];

    if( ( NULL == a_analysis ) || ( 0U == a_analysis->timing_count ) )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 58, "NO TIMING DATA", theme->secondary, 1U );
        return;
    }

    uint32_t largest = 1U;
    for( size_t index = 0U; index < a_analysis->timing_count; ++index )
    {
        const uint32_t total = (uint32_t)a_analysis->timing[ index ].high_count +
                               a_analysis->timing[ index ].low_count;
        if( total > largest )
        {
            largest = total;
        }
    }

    const size_t rows =
        a_analysis->timing_count < TIMING_ROWS ? a_analysis->timing_count : TIMING_ROWS;
    for( size_t index = 0U; index < rows; ++index )
    {
        const rfscout_subghz_timing_bucket_t *bucket = &a_analysis->timing[ index ];
        const uint32_t total = (uint32_t)bucket->high_count + bucket->low_count;
        const int y = 47 + (int)index * 16;
        snprintf( line,
                  sizeof( line ),
                  "%4luus H%u L%u",
                  (unsigned long)bucket->duration_us,
                  (unsigned)bucket->high_count,
                  (unsigned)bucket->low_count );
        display_draw_text( UI_LAYOUT_LEFT_X, y, line, theme->text, 1U );
        const int width = (int)( total * 120U / largest );
        if( width > 0 )
        {
            display_fill_rect( 190, y + 2, width, 6, theme->graph_primary );
        }
    }
}

static void waveform_level( int *a_x,
                            int a_end_x,
                            bool a_level,
                            uint32_t a_duration,
                            uint64_t a_total,
                            const ui_theme_palette_t *a_theme )
{
    if( ( NULL == a_x ) || ( 0U == a_total ) || ( *a_x >= a_end_x ) )
    {
        return;
    }

    int width =
        (int)( ( (uint64_t)a_duration * (uint64_t)( a_end_x - UI_LAYOUT_LEFT_X ) ) / a_total );
    if( width < 1 )
    {
        width = 1;
    }
    if( *a_x + width > a_end_x )
    {
        width = a_end_x - *a_x;
    }

    const int y = a_level ? 66 : 118;
    display_fill_rect( *a_x, y, width, 2, a_theme->graph_primary );
    *a_x += width;
}

static void render_waveform( const rfscout_subghz_symbol_t *a_symbols,
                             size_t a_count,
                             uint32_t a_resolution_hz )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    if( ( NULL == a_symbols ) || ( 0U == a_count ) || ( 0U == a_resolution_hz ) )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 58, "WAITING FOR RAW SNAPSHOT", theme->secondary, 1U );
        return;
    }

    const size_t shown = a_count < 96U ? a_count : 96U;
    uint64_t total_ticks = 0U;
    for( size_t index = 0U; index < shown; ++index )
    {
        total_ticks += rfscout_subghz_symbol_duration0( a_symbols[ index ] );
        total_ticks += rfscout_subghz_symbol_duration1( a_symbols[ index ] );
    }
    if( 0U == total_ticks )
    {
        return;
    }

    display_draw_text( UI_LAYOUT_LEFT_X, 48, "NATIVE RMT SNAPSHOT", theme->secondary, 1U );
    display_fill_rect(
        UI_LAYOUT_LEFT_X, 92, RFSCOUT_DISPLAY_WIDTH - 2 * UI_LAYOUT_LEFT_X, 1, theme->divider );

    int x = UI_LAYOUT_LEFT_X;
    const int end_x = RFSCOUT_DISPLAY_WIDTH - UI_LAYOUT_LEFT_X;
    for( size_t index = 0U; ( index < shown ) && ( x < end_x ); ++index )
    {
        waveform_level( &x,
                        end_x,
                        rfscout_subghz_symbol_level0( a_symbols[ index ] ),
                        rfscout_subghz_symbol_duration0( a_symbols[ index ] ),
                        total_ticks,
                        theme );
        waveform_level( &x,
                        end_x,
                        rfscout_subghz_symbol_level1( a_symbols[ index ] ),
                        rfscout_subghz_symbol_duration1( a_symbols[ index ] ),
                        total_ticks,
                        theme );
    }
}

void cc1101_view_receive( cc1101_view_mode_t a_view,
                          const cc1101_receive_config_t *a_config,
                          const rfscout_subghz_analysis_t *a_analysis,
                          const cc1101_capture_stats_t *a_stats,
                          const rfscout_subghz_symbol_t *a_raw_symbols,
                          size_t a_raw_count,
                          uint32_t a_raw_resolution_hz,
                          bool a_app_locked )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( BG );
    ui_chrome_draw( "CC1101 / RECEIVE", SUBDUED );

    char frequency[ 20 ];
    char line[ 72 ];
    if( NULL != a_config )
    {
        format_mhz( frequency, sizeof( frequency ), a_config->frequency_khz );
        snprintf( line,
                  sizeof( line ),
                  "%s MHz  %s  [%s]",
                  frequency,
                  cc1101_receive_preset_name( a_config->preset ),
                  view_name( a_view ) );
        display_draw_text( UI_LAYOUT_LEFT_X, 30, line, theme->secondary, 1U );
    }

    if( CC1101_VIEW_TIMING == a_view )
    {
        render_timing( a_analysis );
    }
    else if( CC1101_VIEW_WAVEFORM == a_view )
    {
        render_waveform( a_raw_symbols, a_raw_count, a_raw_resolution_hz );
    }
    else
    {
        render_decode( a_analysis, a_stats );
    }

    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       a_app_locked ? "TURN: VIEWS  CENTER: SETTINGS"
                                    : "TURN: APPS  LONG PRESS: VIEW  CENTER: SETTINGS",
                       SUBDUED,
                       1U );
    (void)display_present();
}

void cc1101_view_menu( unsigned a_selected,
                       bool a_editing,
                       bool a_limit_reached,
                       bool a_app_locked,
                       cc1101_acquisition_mode_t a_mode,
                       const cc1101_scan_config_t *a_scan,
                       const cc1101_receive_config_t *a_receive )
{
    static const char *labels[ CC1101_MENU_ITEM_COUNT ] = {
        "BACK",
        "APP LOCK",
        "MODE",
        "SCAN PROFILE",
        "RX FREQ",
        "RX PRESET",
        "LOW",
        "HIGH",
        "STEP",
    };
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned first = ui_layout_menu_window_start( a_selected, CC1101_MENU_ITEM_COUNT );

    display_clear( BG );
    ui_chrome_draw( "CC1101 SETTINGS", SUBDUED );

    char value[ 36 ];
    for( unsigned row = 0U; row < UI_LAYOUT_MENU_VISIBLE_ROWS; ++row )
    {
        const unsigned index = first + row;
        if( index >= CC1101_MENU_ITEM_COUNT )
        {
            break;
        }

        const int y = UI_LAYOUT_MENU_START_Y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT;
        const bool active = index == a_selected;
        const bool action = 0U == index;
        const uint16_t accent =
            ( active && a_editing && a_limit_reached ) ? theme->warning : theme->selected;
        const char *marker = active ? ( action ? ">" : ( a_editing ? "*" : ">" ) ) : " ";

        ui_game_draw_menu_marker( UI_LAYOUT_LEFT_X, y, marker, active ? accent : theme->divider );
        display_draw_text(
            UI_LAYOUT_LABEL_X, y, labels[ index ], active ? theme->text : theme->divider, 1U );

        value[ 0 ] = '\0';
        if( 1U == index )
        {
            snprintf( value, sizeof( value ), "%s", a_app_locked ? "ON" : "OFF" );
        }
        else if( 2U == index )
        {
            snprintf( value,
                      sizeof( value ),
                      "%s",
                      CC1101_ACQUISITION_RECEIVE == a_mode ? "RECEIVE" : "DISCOVERY" );
        }
        else if( ( 3U == index ) && ( NULL != a_scan ) )
        {
            snprintf( value, sizeof( value ), "%s", cc1101_profile_name( a_scan->profile ) );
        }
        else if( ( 4U == index ) && ( NULL != a_receive ) )
        {
            char mhz[ 20 ];
            format_mhz( mhz, sizeof( mhz ), a_receive->frequency_khz );
            snprintf( value, sizeof( value ), "%s MHz", mhz );
        }
        else if( ( 5U == index ) && ( NULL != a_receive ) )
        {
            snprintf(
                value, sizeof( value ), "%s", cc1101_receive_preset_name( a_receive->preset ) );
        }
        else if( ( index >= 6U ) && ( NULL != a_scan ) )
        {
            const uint32_t setting = ( 6U == index )   ? a_scan->low_khz
                                     : ( 7U == index ) ? a_scan->high_khz
                                                       : a_scan->step_khz;
            if( 8U == index )
            {
                snprintf( value, sizeof( value ), "%lu kHz", (unsigned long)setting );
            }
            else
            {
                char mhz[ 20 ];
                format_mhz( mhz, sizeof( mhz ), setting );
                snprintf( value, sizeof( value ), "%s MHz", mhz );
            }
        }

        if( '\0' != value[ 0 ] )
        {
            display_draw_text( 142, y, value, active ? accent : theme->secondary, 1U );
        }
    }

    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       a_editing ? "TURN: CHANGE  CENTER: DONE" : "TURN: SELECT  CENTER: ENTER",
                       a_editing ? theme->selected : theme->secondary,
                       1U );
    (void)display_present();
}
