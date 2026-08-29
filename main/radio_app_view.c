/**
    radio_app_view.c : RFScout radio application views
    Description: Renders CC1101 and nRF24 spectra, detection panes, and settings menus.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "radio_app_view.h"

#include "cc1101.h"
#include "display.h"
#include "nrf24_activity.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include <stdio.h>

#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define RED 0xF800U
#define LIMIT 0xFD20U
#define APP_COLUMN_DIVIDER_X 198
#define APP_RIGHT_COLUMN_X 205
#define APP_SUMMARY_Y 30
#define APP_DIVIDER_Y 36
#define APP_GRAPH_Y 48
#define APP_GRAPH_HEIGHT 92
#define APP_DETECTIONS_Y 43
#define APP_DETECTION_ROW_Y 64
#define APP_DETECTION_ROW_HEIGHT 18

/** @brief Format a kHz value as a MHz string with three decimal places. */
static void format_mhz( char *a_buffer, size_t a_capacity, uint32_t a_frequency_khz )
{
    snprintf( a_buffer,
              a_capacity,
              "%lu.%03lu",
              (unsigned long)( a_frequency_khz / 1000U ),
              (unsigned long)( a_frequency_khz % 1000U ) );
}

void radio_app_view_cc1101_spectrum( const uint8_t *a_current,
                                     const uint8_t *a_peak,
                                     size_t a_count,
                                     uint32_t a_low_khz,
                                     uint32_t a_high_khz,
                                     bool a_detected,
                                     const cc1101_fingerprint_t *a_devices,
                                     size_t a_device_count )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( BG );
    ui_chrome_draw( "CC1101", SUBDUED );

    char low_text[ 24 ];
    char high_text[ 24 ];
    char line[ 64 ];
    format_mhz( low_text, sizeof( low_text ), a_low_khz );
    format_mhz( high_text, sizeof( high_text ), a_high_khz );
    snprintf( line, sizeof( line ), "%s - %s MHz", low_text, high_text );
    display_draw_text( UI_LAYOUT_LEFT_X, APP_SUMMARY_Y, line, DIM, 1U );

    if( !a_detected )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 70, "CC1101 NOT DETECTED", YELLOW, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: SETTINGS", SUBDUED, 1U );
        (void)display_present();
        return;
    }

    const int graph_x = UI_LAYOUT_LEFT_X;
    const int graph_width = APP_COLUMN_DIVIDER_X - UI_LAYOUT_LEFT_X - 6;
    display_fill_rect( graph_x, APP_GRAPH_Y + APP_GRAPH_HEIGHT, graph_width, 1, DIM );

    if( ( NULL != a_current ) && ( NULL != a_peak ) )
    {
        for( size_t i = 0U; i < a_count; ++i )
        {
            const size_t denominator = ( a_count > 1U ) ? a_count - 1U : 1U;
            const int x = graph_x + (int)( i * (size_t)( graph_width - 1 ) / denominator );
            const int height = (int)a_current[ i ] * ( APP_GRAPH_HEIGHT - 3 ) / 255;

            if( height > 0 )
            {
                display_fill_rect(
                    x, APP_GRAPH_Y + APP_GRAPH_HEIGHT - height, 2, height, theme->graph_primary );
            }

            if( a_peak[ i ] > 0U )
            {
                int peak_y = APP_GRAPH_Y + APP_GRAPH_HEIGHT -
                             (int)a_peak[ i ] * ( APP_GRAPH_HEIGHT - 3 ) / 255;
                if( peak_y < APP_GRAPH_Y )
                {
                    peak_y = APP_GRAPH_Y;
                }
                display_fill_rect( x, peak_y, 2, 2, theme->graph_peak );
            }
        }
    }

    display_fill_rect(
        APP_COLUMN_DIVIDER_X, APP_DIVIDER_Y, 1, RFSCOUT_DISPLAY_HEIGHT - APP_DIVIDER_Y, DIM );
    display_draw_text( APP_RIGHT_COLUMN_X, APP_DETECTIONS_Y, "DETECTIONS", SUBDUED, 1U );

    if( NULL != a_devices )
    {
        const size_t rows =
            ( a_device_count < CC1101_FINGERPRINT_MAX ) ? a_device_count : CC1101_FINGERPRINT_MAX;

        for( size_t i = 0U; i < rows; ++i )
        {
            const int y = APP_DETECTION_ROW_Y + (int)i * APP_DETECTION_ROW_HEIGHT;
            const char *label = ( '\0' != a_devices[ i ].label[ 0 ] )
                                    ? a_devices[ i ].label
                                    : cc1101_activity_fingerprint_name( a_devices[ i ].type );
            snprintf( line, sizeof( line ), "%-12.12s %3u", label, (unsigned)a_devices[ i ].count );
            display_draw_text(
                APP_RIGHT_COLUMN_X, y, line, ( 0U == a_devices[ i ].count ) ? DIM : RED, 1U );
        }
    }

    display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "RSSI / PEAK HOLD", DIM, 1U );
    display_draw_text( 250, UI_LAYOUT_FOOTER_Y, "SETTINGS", SUBDUED, 1U );
    (void)display_present();
}

void radio_app_view_cc1101_menu( unsigned a_selected,
                                 bool a_editing,
                                 const char *a_profile,
                                 uint32_t a_low_khz,
                                 uint32_t a_high_khz,
                                 uint32_t a_step_khz,
                                 size_t a_samples,
                                 bool a_limit_reached,
                                 bool a_peak_hold )
{
    static const char *labels[] = {
        "BACK", "PROFILE", "LOW", "HIGH", "STEP", "PEAK HOLD", "RESET COUNTS" };
    const ui_theme_palette_t *theme = ui_theme_get();

    display_clear( BG );
    ui_chrome_draw( "CC1101 SETTINGS", SUBDUED );

    char value[ 32 ];
    for( unsigned i = 0U; i < 7U; ++i )
    {
        const int y = UI_LAYOUT_MENU_START_Y + (int)i * 18;
        const bool active = ( i == a_selected );
        const bool action = ( 0U == i ) || ( 6U == i );
        const uint16_t accent =
            ( active && a_editing && a_limit_reached ) ? theme->warning : theme->selected;
        const char *marker = " ";

        if( active )
        {
            marker = action ? ">" : ( a_editing ? "*" : ">" );
        }

        display_draw_text( UI_LAYOUT_LEFT_X, y, marker, active ? accent : DIM, 1U );
        display_draw_text( UI_LAYOUT_LABEL_X, y, labels[ i ], active ? TEXT : DIM, 1U );

        value[ 0 ] = '\0';
        if( 1U == i )
        {
            snprintf( value, sizeof( value ), "%s", ( NULL != a_profile ) ? a_profile : "?" );
        }
        else if( 2U == i )
        {
            snprintf( value,
                      sizeof( value ),
                      "%lu.%03lu MHz",
                      (unsigned long)( a_low_khz / 1000U ),
                      (unsigned long)( a_low_khz % 1000U ) );
        }
        else if( 3U == i )
        {
            snprintf( value,
                      sizeof( value ),
                      "%lu.%03lu MHz",
                      (unsigned long)( a_high_khz / 1000U ),
                      (unsigned long)( a_high_khz % 1000U ) );
        }
        else if( 4U == i )
        {
            snprintf( value, sizeof( value ), "%lu kHz", (unsigned long)a_step_khz );
        }
        else if( 5U == i )
        {
            snprintf( value, sizeof( value ), "%s", a_peak_hold ? "ON" : "OFF" );
        }

        if( '\0' != value[ 0 ] )
        {
            if( active && a_editing )
            {
                display_fill_rect( 126, y - 4, 174, 18, accent );
                display_draw_text( 130, y, value, BG, 1U );
            }
            else
            {
                display_draw_text( 130, y, value, active ? theme->selected : DIM, 1U );
            }
        }
    }

    snprintf( value,
              sizeof( value ),
              "%u/%u SAMPLES",
              (unsigned)a_samples,
              (unsigned)CC1101_MAX_SWEEP_SAMPLES );
    display_draw_text( UI_LAYOUT_LEFT_X, 160, value, a_limit_reached ? LIMIT : DIM, 1U );
    (void)display_present();
}

void radio_app_view_nrf24_spectrum( const uint8_t *a_fast,
                                    const uint8_t *a_slow,
                                    size_t a_count,
                                    uint8_t a_low_channel,
                                    uint8_t a_high_channel,
                                    bool a_detected,
                                    const nrf24_fingerprint_t *a_devices,
                                    size_t a_device_count )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( BG );
    ui_chrome_draw( "nRF24", SUBDUED );

    char line[ 64 ];
    snprintf( line,
              sizeof( line ),
              "CH %u-%u   %u-%u MHz",
              (unsigned)a_low_channel,
              (unsigned)a_high_channel,
              (unsigned)( 2400U + a_low_channel ),
              (unsigned)( 2400U + a_high_channel ) );
    display_draw_text( UI_LAYOUT_LEFT_X, APP_SUMMARY_Y, line, DIM, 1U );

    if( !a_detected )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 70, "nRF24 NOT DETECTED", YELLOW, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: SETTINGS", SUBDUED, 1U );
        (void)display_present();
        return;
    }

    const int graph_x = UI_LAYOUT_LEFT_X;
    const int graph_width = APP_COLUMN_DIVIDER_X - UI_LAYOUT_LEFT_X - 6;
    const size_t span = (size_t)( a_high_channel - a_low_channel ) + 1U;
    display_fill_rect( graph_x, APP_GRAPH_Y + APP_GRAPH_HEIGHT, graph_width, 1, DIM );

    if( ( NULL != a_fast ) && ( NULL != a_slow ) )
    {
        for( size_t i = 0U; i < span; ++i )
        {
            const size_t channel = (size_t)a_low_channel + i;
            if( channel >= a_count )
            {
                break;
            }

            const size_t denominator = ( span > 1U ) ? span - 1U : 1U;
            const int x = graph_x + (int)( i * (size_t)( graph_width - 1 ) / denominator );
            const int height = (int)a_fast[ channel ] * ( APP_GRAPH_HEIGHT - 3 ) / 255;

            if( height > 0 )
            {
                display_fill_rect(
                    x, APP_GRAPH_Y + APP_GRAPH_HEIGHT - height, 2, height, theme->graph_primary );
            }

            if( a_slow[ channel ] > 0U )
            {
                int peak_y = APP_GRAPH_Y + APP_GRAPH_HEIGHT -
                             (int)a_slow[ channel ] * ( APP_GRAPH_HEIGHT - 3 ) / 255;
                if( peak_y < APP_GRAPH_Y )
                {
                    peak_y = APP_GRAPH_Y;
                }
                display_fill_rect( x, peak_y, 2, 2, theme->graph_peak );
            }
        }
    }

    display_fill_rect(
        APP_COLUMN_DIVIDER_X, APP_DIVIDER_Y, 1, RFSCOUT_DISPLAY_HEIGHT - APP_DIVIDER_Y, DIM );
    display_draw_text( APP_RIGHT_COLUMN_X, APP_DETECTIONS_Y, "DETECTIONS", SUBDUED, 1U );

    if( NULL != a_devices )
    {
        const size_t rows =
            ( a_device_count < NRF24_FINGERPRINT_MAX ) ? a_device_count : NRF24_FINGERPRINT_MAX;

        for( size_t i = 0U; i < rows; ++i )
        {
            const int y = APP_DETECTION_ROW_Y + (int)i * APP_DETECTION_ROW_HEIGHT;
            const char *label = ( '\0' != a_devices[ i ].label[ 0 ] )
                                    ? a_devices[ i ].label
                                    : nrf24_activity_fingerprint_name( a_devices[ i ].type );
            snprintf( line,
                      sizeof( line ),
                      "%-12.12s %3lu",
                      label,
                      (unsigned long)a_devices[ i ].packets );
            display_draw_text(
                APP_RIGHT_COLUMN_X, y, line, ( 0U == a_devices[ i ].packets ) ? DIM : RED, 1U );
        }
    }

    display_draw_text( UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "FAST ACTIVITY / PEAK HOLD", DIM, 1U );
    display_draw_text( 250, UI_LAYOUT_FOOTER_Y, "SETTINGS", SUBDUED, 1U );
    (void)display_present();
}

void radio_app_view_nrf24_menu( unsigned a_selected,
                                bool a_editing,
                                uint8_t a_low_channel,
                                uint8_t a_high_channel,
                                uint16_t a_dwell_us,
                                bool a_peak_hold )
{
    static const char *labels[] = {
        "BACK", "LOW CH", "HIGH CH", "DWELL", "PEAK HOLD", "RESET COUNTS" };
    const ui_theme_palette_t *theme = ui_theme_get();

    display_clear( BG );
    ui_chrome_draw( "nRF24 SETTINGS", SUBDUED );

    char value[ 32 ];
    for( unsigned i = 0U; i < 6U; ++i )
    {
        const int y = UI_LAYOUT_MENU_START_Y + (int)i * UI_LAYOUT_MENU_ROW_HEIGHT;
        const bool active = ( i == a_selected );
        const bool action = ( 0U == i ) || ( 5U == i );
        const char *marker = " ";

        if( active )
        {
            marker = action ? ">" : ( a_editing ? "*" : ">" );
        }

        display_draw_text( UI_LAYOUT_LEFT_X, y, marker, active ? theme->selected : DIM, 1U );
        display_draw_text( UI_LAYOUT_LABEL_X, y, labels[ i ], active ? TEXT : DIM, 1U );

        value[ 0 ] = '\0';
        if( 1U == i )
        {
            snprintf( value,
                      sizeof( value ),
                      "%u / %u MHz",
                      (unsigned)a_low_channel,
                      (unsigned)( 2400U + a_low_channel ) );
        }
        else if( 2U == i )
        {
            snprintf( value,
                      sizeof( value ),
                      "%u / %u MHz",
                      (unsigned)a_high_channel,
                      (unsigned)( 2400U + a_high_channel ) );
        }
        else if( 3U == i )
        {
            snprintf( value, sizeof( value ), "%u us", (unsigned)a_dwell_us );
        }
        else if( 4U == i )
        {
            snprintf( value, sizeof( value ), "%s", a_peak_hold ? "ON" : "OFF" );
        }

        if( '\0' != value[ 0 ] )
        {
            if( active && a_editing )
            {
                display_fill_rect( 126, y - 4, 174, 18, theme->selected );
                display_draw_text( 130, y, value, BG, 1U );
            }
            else
            {
                display_draw_text( 130, y, value, active ? theme->selected : DIM, 1U );
            }
        }
    }

    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       a_editing ? "TURN: CHANGE  CENTER: DONE" : "TURN: SELECT CENTER: ENTER",
                       a_editing ? theme->selected : SUBDUED,
                       1U );
    (void)display_present();
}
