/**
    radio_app_view.c : RFScout radio application views
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "radio_app_view.h"
#include "cc1101.h"
#include "display.h"
#include "nrf24_activity.h"
#include <stdio.h>

#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define RED 0xF800U
#define LIMIT 0xFD20U
#define GRAPH 0x03E0U
#define PEAK 0x07FFU
#define APP_COLUMN_DIVIDER_X 198
#define APP_RIGHT_COLUMN_X 205

static void mhz( char *a_buffer, size_t a_size, uint32_t a_khz )
{
    snprintf( a_buffer, a_size, "%lu.%03lu",
              (unsigned long)( a_khz / 1000U ),
              (unsigned long)( a_khz % 1000U ) );
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
    display_clear( BG );
    display_draw_text( 8, 7, "CC1101", SUBDUED, 2U );
    char low[ 24 ]; char high[ 24 ]; char line[ 64 ];
    mhz( low, sizeof( low ), a_low_khz );
    mhz( high, sizeof( high ), a_high_khz );
    snprintf( line, sizeof( line ), "%s - %s MHz", low, high );
    display_draw_text( 8, 30, line, DIM, 1U );
    if( !a_detected )
    {
        display_draw_text( 8, 70, "CC1101 NOT DETECTED", YELLOW, 1U );
        display_draw_text( 8, 126, "CENTER: SETTINGS", SUBDUED, 1U );
        (void)display_present(); return;
    }
    const int x0 = 8, y0 = 48, width = APP_COLUMN_DIVIDER_X - 14, height = 92;
    display_fill_rect( x0, y0 + height, width, 1, DIM );
    for( size_t i = 0U; i < a_count; ++i )
    {
        const int x = x0 + (int)( ( i * (size_t)( width - 1 ) ) /
                                  ( a_count > 1U ? a_count - 1U : 1U ) );
        const int bar_height = (int)a_current[ i ] * ( height - 3 ) / 255;
        if( bar_height > 0 ) display_fill_rect( x, y0 + height - bar_height, 2, bar_height, GRAPH );
        int peak_y = y0 + height - (int)a_peak[ i ] * ( height - 3 ) / 255;
        if( peak_y < y0 ) peak_y = y0;
        display_fill_rect( x, peak_y, 2, 2, PEAK );
    }
    display_fill_rect( APP_COLUMN_DIVIDER_X, 36, 1, RFSCOUT_DISPLAY_HEIGHT - 36, DIM );
    display_draw_text( APP_RIGHT_COLUMN_X, 43, "LIKELY DEVICES", SUBDUED, 1U );
    for( size_t i = 0U; ( i < a_device_count ) && ( i < CC1101_FINGERPRINT_MAX ); ++i )
    {
        const int y = 64 + (int)i * 18;
        const char *label = a_devices[ i ].label[ 0 ] ? a_devices[ i ].label :
                            cc1101_activity_fingerprint_name( a_devices[ i ].type );
        snprintf( line, sizeof( line ), "%-12.12s %3u", label, (unsigned)a_devices[ i ].count );
        display_draw_text( APP_RIGHT_COLUMN_X, y, line,
                           ( 0U == a_devices[ i ].count ) ? DIM : RED, 1U );
    }
    display_draw_text( 8, 148, "RSSI / PEAK HOLD", DIM, 1U );
    display_draw_text( 250, 148, "SETTINGS", SUBDUED, 1U );
    (void)display_present();
}

void radio_app_view_cc1101_menu( unsigned a_selected, bool a_editing,
                                 const char *a_profile, uint32_t a_low_khz,
                                 uint32_t a_high_khz, uint32_t a_step_khz,
                                 size_t a_samples, bool a_limit_reached )
{
    display_clear( BG );
    display_draw_text( 8, 7, "CC1101 SETTINGS", SUBDUED, 2U );
    const char *labels[] = { "PROFILE", "LOW", "HIGH", "STEP", "RESET COUNTS", "BACK" };
    char value[ 32 ];
    for( unsigned i = 0U; i < 6U; ++i )
    {
        const int y = 34 + (int)i * 20;
        const bool active = ( i == a_selected );
        const bool action = ( i >= 4U );
        const uint16_t accent = ( active && a_editing && a_limit_reached ) ? LIMIT : YELLOW;
        display_draw_text( 8, y, active ? ( action ? ">" : ( a_editing ? "*" : ">" ) ) : " ",
                           active ? accent : DIM, 1U );
        display_draw_text( 22, y, labels[ i ], active ? TEXT : DIM, 1U );
        value[ 0 ] = '\0';
        if( 0U == i ) snprintf( value, sizeof( value ), "%s", a_profile );
        else if( 1U == i ) snprintf( value, sizeof( value ), "%lu.%03lu MHz",
                                     (unsigned long)( a_low_khz / 1000U ),
                                     (unsigned long)( a_low_khz % 1000U ) );
        else if( 2U == i ) snprintf( value, sizeof( value ), "%lu.%03lu MHz",
                                     (unsigned long)( a_high_khz / 1000U ),
                                     (unsigned long)( a_high_khz % 1000U ) );
        else if( 3U == i ) snprintf( value, sizeof( value ), "%lu kHz", (unsigned long)a_step_khz );
        if( '\0' != value[ 0 ] )
        {
            if( active && a_editing )
            {
                display_fill_rect( 126, y - 4, 174, 18, accent );
                display_draw_text( 130, y, value, BG, 1U );
            }
            else display_draw_text( 130, y, value, active ? YELLOW : DIM, 1U );
        }
    }
    snprintf( value, sizeof( value ), "%u/%u SAMPLES", (unsigned)a_samples,
              (unsigned)CC1101_MAX_SWEEP_SAMPLES );
    display_draw_text( 8, 154, value, a_limit_reached ? LIMIT : DIM, 1U );
    display_draw_text( 8, 164,
                       a_limit_reached ? "SWEEP LIMIT 128" :
                       ( a_editing ? "TURN: CHANGE  CENTER: DONE" : "TURN: SELECT CENTER: ENTER" ),
                       a_limit_reached ? LIMIT : ( a_editing ? YELLOW : SUBDUED ), 1U );
    (void)display_present();
}

void radio_app_view_nrf24_spectrum( const uint8_t *a_fast,
                                    const uint8_t *a_slow,
                                    size_t a_count,
                                    uint8_t a_low,
                                    uint8_t a_high,
                                    bool a_detected,
                                    const nrf24_fingerprint_t *a_devices,
                                    size_t a_device_count )
{
    display_clear( BG );
    display_draw_text( 8, 7, "nRF24", SUBDUED, 2U );
    char line[ 64 ];
    snprintf( line, sizeof( line ), "CH %u-%u   %u-%u MHz",
              (unsigned)a_low, (unsigned)a_high,
              (unsigned)( 2400U + a_low ), (unsigned)( 2400U + a_high ) );
    display_draw_text( 8, 30, line, DIM, 1U );
    if( !a_detected )
    {
        display_draw_text( 8, 70, "nRF24 NOT DETECTED", YELLOW, 1U );
        (void)display_present(); return;
    }
    const int x0 = 8, y0 = 48, width = APP_COLUMN_DIVIDER_X - 14, height = 92;
    display_fill_rect( x0, y0 + height, width, 1, DIM );
    const size_t span = (size_t)( a_high - a_low ) + 1U;
    for( size_t i = 0U; i < span; ++i )
    {
        const size_t channel = (size_t)a_low + i;
        if( channel >= a_count ) break;
        const int x = x0 + (int)( ( i * (size_t)( width - 1 ) ) /
                                  ( span > 1U ? span - 1U : 1U ) );
        const int bar_height = (int)a_fast[ channel ] * ( height - 3 ) / 255;
        if( bar_height > 0 ) display_fill_rect( x, y0 + height - bar_height, 2, bar_height, GRAPH );
        int peak_y = y0 + height - (int)a_slow[ channel ] * ( height - 3 ) / 255;
        if( peak_y < y0 ) peak_y = y0;
        display_fill_rect( x, peak_y, 2, 2, PEAK );
    }
    display_fill_rect( APP_COLUMN_DIVIDER_X, 36, 1, RFSCOUT_DISPLAY_HEIGHT - 36, DIM );
    display_draw_text( APP_RIGHT_COLUMN_X, 43, "LIKELY DEVICES", SUBDUED, 1U );
    for( size_t i = 0U; ( i < a_device_count ) && ( i < NRF24_FINGERPRINT_MAX ); ++i )
    {
        const int y = 64 + (int)i * 18;
        const char *label = a_devices[ i ].label[ 0 ] ? a_devices[ i ].label :
                            nrf24_activity_fingerprint_name( a_devices[ i ].type );
        snprintf( line, sizeof( line ), "%-12.12s %3lu", label,
                  (unsigned long)a_devices[ i ].packets );
        display_draw_text( APP_RIGHT_COLUMN_X, y, line,
                           ( 0U == a_devices[ i ].packets ) ? DIM : RED, 1U );
    }
    display_draw_text( 8, 148, "FAST ACTIVITY / PEAK HOLD", DIM, 1U );
    display_draw_text( 250, 148, "SETTINGS", SUBDUED, 1U );
    (void)display_present();
}

void radio_app_view_nrf24_menu( unsigned a_selected, bool a_editing,
                                uint8_t a_low, uint8_t a_high, uint16_t a_dwell )
{
    display_clear( BG );
    display_draw_text( 8, 7, "nRF24 SETTINGS", SUBDUED, 2U );
    const char *labels[] = { "LOW CH", "HIGH CH", "DWELL", "BACK" };
    char value[ 32 ];
    for( unsigned i = 0U; i < 4U; ++i )
    {
        const int y = 42 + (int)i * 28;
        const bool active = ( i == a_selected );
        display_draw_text( 8, y, active ? ( a_editing ? "*" : ">" ) : " ",
                           active ? YELLOW : DIM, 1U );
        display_draw_text( 22, y, labels[ i ], active ? TEXT : DIM, 1U );
        value[ 0 ] = '\0';
        if( 0U == i ) snprintf( value, sizeof( value ), "%u / %u MHz",
                                (unsigned)a_low, (unsigned)( 2400U + a_low ) );
        else if( 1U == i ) snprintf( value, sizeof( value ), "%u / %u MHz",
                                     (unsigned)a_high, (unsigned)( 2400U + a_high ) );
        else if( 2U == i ) snprintf( value, sizeof( value ), "%u us", (unsigned)a_dwell );
        if( '\0' != value[ 0 ] )
        {
            if( active && a_editing )
            {
                display_fill_rect( 126, y - 4, 174, 18, YELLOW );
                display_draw_text( 130, y, value, BG, 1U );
            }
            else display_draw_text( 130, y, value, active ? YELLOW : DIM, 1U );
        }
    }
    display_draw_text( 8, 158,
                       a_editing ? "TURN: CHANGE       CENTER: DONE" :
                                   "TURN: SELECT      CENTER: ENTER",
                       a_editing ? YELLOW : SUBDUED, 1U );
    (void)display_present();
}
