/**
    recorder_view.c : RFScout recorder application UI
    Description: Renders storage-dependent recording and playback state for the Recorder app.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "recorder_view.h"

#include "battery_indicator.h"
#include "display.h"
#include "recorder.h"
#include "status_bar.h"
#include "ui_theme.h"

#include <stdio.h>

#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define RED 0xF800U

static void draw_status_chrome( void )
{
    status_bar_draw();
    battery_indicator_draw();
}

static const char *state_name( rfscout_recorder_state_t a_state )
{
    switch( a_state )
    {
        case RFSCOUT_RECORDER_RECORDING:
            return "RECORDING";
        case RFSCOUT_RECORDER_PLAYING:
            return "PLAYING";
        case RFSCOUT_RECORDER_NO_STORAGE:
            return "NO SD CARD";
        case RFSCOUT_RECORDER_ERROR:
            return "ERROR";
        case RFSCOUT_RECORDER_IDLE:
        default:
            return "READY";
    }
}

void recorder_view_render( size_t a_selected )
{
    (void)a_selected;
    const rfscout_recorder_state_t state = recorder_state();
    display_clear( BG );
    display_draw_text( 8, 7, "RECORDER", SUBDUED, 2U );
    draw_status_chrome();

    const uint16_t state_color =
        ( RFSCOUT_RECORDER_NO_STORAGE == state || RFSCOUT_RECORDER_ERROR == state ) ? RED : YELLOW;
    display_draw_text( 8, 38, state_name( state ), state_color, 1U );

    char line[ 80 ];
    if( RFSCOUT_RECORDER_RECORDING == state )
    {
        snprintf( line, sizeof( line ), "%s", recorder_active_name() );
        display_draw_text( 8, 63, line, TEXT, 1U );
        const uint32_t seconds = recorder_recorded_bytes() / ( RFSCOUT_RECORDER_SAMPLE_RATE * 2U );
        snprintf( line,
                  sizeof( line ),
                  "%02lu:%02lu  %lu KB",
                  (unsigned long)( seconds / 60U ),
                  (unsigned long)( seconds % 60U ),
                  (unsigned long)( recorder_recorded_bytes() / 1024U ) );
        display_draw_text( 8, 88, line, DIM, 1U );
        display_draw_text( 8, 144, "CENTER: STOP", SUBDUED, 1U );
    }
    else if( RFSCOUT_RECORDER_PLAYING == state )
    {
        display_draw_text( 8, 63, "PLAYBACK ACTIVE", TEXT, 1U );
        display_draw_text( 8, 144, "CENTER: STOP", SUBDUED, 1U );
    }
    else if( RFSCOUT_RECORDER_NO_STORAGE == state )
    {
        display_draw_text( 8, 63, "Insert/mount SD card", DIM, 1U );
        display_draw_text( 8, 88, "/sdcard/audio required", DIM, 1U );
    }
    else
    {
        snprintf( line, sizeof( line ), "%u RECORDINGS", (unsigned)recorder_file_count() );
        display_draw_text( 8, 63, line, TEXT, 1U );
        display_draw_text( 8, 103, "CENTER: MENU", SUBDUED, 1U );
    }

    display_draw_text( 8, 121, "16 kHz / 16-bit mono WAV", DIM, 1U );
    (void)display_present();
}

void recorder_view_files( size_t a_selected )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( BG );
    display_draw_text( 8, 7, "RECORDINGS", SUBDUED, 2U );
    draw_status_chrome();

    const size_t count = recorder_file_count();
    if( 0U == count )
    {
        display_draw_text( 8, 48, "NO PLAYABLE WAV FILES", DIM, 1U );
        display_draw_text( 8, 144, "CENTER: BACK", SUBDUED, 1U );
        (void)display_present();
        return;
    }

    if( a_selected >= count )
    {
        a_selected = count - 1U;
    }

    const size_t first = ( a_selected > 2U ) ? a_selected - 2U : 0U;
    for( size_t row = 0U; row < 5U && ( first + row ) < count; ++row )
    {
        const size_t index = first + row;
        const rfscout_recording_t *recording = recorder_file( index );
        if( NULL == recording )
        {
            continue;
        }

        char line[ 96 ];
        snprintf( line,
                  sizeof( line ),
                  "%c %-24.24s %02lu:%02lu",
                  ( index == a_selected ) ? '>' : ' ',
                  recording->name,
                  (unsigned long)( recording->duration_seconds / 60U ),
                  (unsigned long)( recording->duration_seconds % 60U ) );
        display_draw_text(
            8, 40 + (int)row * 20, line, index == a_selected ? theme->selected : TEXT, 1U );
    }

    display_draw_text( 8, 144, "TURN: SELECT CENTER: PLAY", SUBDUED, 1U );
    (void)display_present();
}
