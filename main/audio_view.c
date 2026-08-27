/**
    audio_view.c : RFScout Recorder compatibility UI
    Description: Renders and controls the replacement Recorder app in the former Audio slot.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "audio_view.h"

#include "audio_spectrum.h"
#include "battery_indicator.h"
#include "device_config.h"
#include "display.h"
#include "recorder.h"
#include "status_bar.h"

#include <stdio.h>

#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define RED 0xF800U
#define RECORDER_MENU_BACK 0U
#define RECORDER_MENU_RECORD 1U
#define RECORDER_MENU_FILE 2U
#define RECORDER_MENU_PLAY 3U
#define RECORDER_MENU_ITEM_COUNT 4U

static unsigned last_selected = RECORDER_MENU_BACK;
static bool last_editing;

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

static void trigger_action( unsigned a_selected )
{
    const rfscout_recorder_state_t state = recorder_state();

    if( RECORDER_MENU_RECORD == a_selected )
    {
        if( RFSCOUT_RECORDER_RECORDING == state )
        {
            recorder_stop_recording();
        }
        else if( RFSCOUT_RECORDER_IDLE == state )
        {
            (void)recorder_start_recording();
        }
    }
    else if( RECORDER_MENU_PLAY == a_selected )
    {
        if( RFSCOUT_RECORDER_PLAYING == state )
        {
            recorder_stop();
        }
        else if( RFSCOUT_RECORDER_IDLE == state && recorder_file_count() > 0U )
        {
            const device_config_t *config = device_config_get();
            const uint8_t volume = ( NULL != config ) ? config->alert_volume_percent : 50U;
            (void)recorder_start_playback( audio_spectrum_channel(), volume );
        }
    }
}

void audio_view_render( const uint8_t *a_current, const uint8_t *a_peak )
{
    (void)a_current;
    (void)a_peak;

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
        display_draw_text( 8, 63, recorder_active_name(), TEXT, 1U );
        const uint32_t seconds = recorder_recorded_bytes() / ( RFSCOUT_RECORDER_SAMPLE_RATE * 2U );
        snprintf( line,
                  sizeof( line ),
                  "%02lu:%02lu  %lu KB",
                  (unsigned long)( seconds / 60U ),
                  (unsigned long)( seconds % 60U ),
                  (unsigned long)( recorder_recorded_bytes() / 1024U ) );
        display_draw_text( 8, 88, line, DIM, 1U );
    }
    else if( RFSCOUT_RECORDER_PLAYING == state )
    {
        display_draw_text(
            8, 63, audio_spectrum_channel_name( audio_spectrum_channel() ), TEXT, 1U );
    }
    else if( RFSCOUT_RECORDER_NO_STORAGE == state )
    {
        display_draw_text( 8, 63, "Insert or mount SD card", DIM, 1U );
        display_draw_text( 8, 88, "/sdcard/audio required", DIM, 1U );
    }
    else
    {
        snprintf( line, sizeof( line ), "%u RECORDINGS", (unsigned)recorder_file_count() );
        display_draw_text( 8, 63, line, TEXT, 1U );
        if( recorder_file_count() > 0U )
        {
            display_draw_text(
                8, 88, audio_spectrum_channel_name( audio_spectrum_channel() ), DIM, 1U );
        }
    }

    display_draw_text( 8, 121, "16 kHz / 16-bit mono WAV", DIM, 1U );
    display_draw_text( 8, 144, "CENTER: MENU", SUBDUED, 1U );
    (void)display_present();
}

void audio_view_menu( unsigned a_selected, bool a_editing, bool a_unused )
{
    (void)a_unused;
    static const char *labels[ RECORDER_MENU_ITEM_COUNT ] = { "BACK", "RECORD", "FILE", "PLAY" };

    if( a_selected >= RECORDER_MENU_ITEM_COUNT )
    {
        a_selected = RECORDER_MENU_BACK;
    }

    if( a_editing && ( !last_editing || a_selected != last_selected ) &&
        ( RECORDER_MENU_RECORD == a_selected || RECORDER_MENU_PLAY == a_selected ) )
    {
        trigger_action( a_selected );
    }
    last_selected = a_selected;
    last_editing = a_editing;

    display_clear( BG );
    display_draw_text( 8, 7, "RECORDER", SUBDUED, 2U );
    draw_status_chrome();

    const rfscout_recorder_state_t state = recorder_state();
    for( unsigned i = 0U; i < RECORDER_MENU_ITEM_COUNT; ++i )
    {
        const int y = 38 + (int)i * 25;
        const bool active = ( i == a_selected );
        const bool editable = ( RECORDER_MENU_FILE == i );
        const char *marker = active ? ( editable && a_editing ? "*" : ">" ) : " ";
        const uint16_t color = active ? YELLOW : DIM;

        display_draw_text( 8, y, marker, color, 1U );
        display_draw_text( 22, y, labels[ i ], active ? TEXT : DIM, 1U );

        if( RECORDER_MENU_RECORD == i )
        {
            const char *value = ( RFSCOUT_RECORDER_RECORDING == state ) ? "STOP" : "START";
            display_draw_text( 110, y, value, color, 1U );
        }
        else if( RECORDER_MENU_FILE == i )
        {
            display_draw_text(
                110, y, audio_spectrum_channel_name( audio_spectrum_channel() ), color, 1U );
        }
        else if( RECORDER_MENU_PLAY == i )
        {
            const char *value = ( RFSCOUT_RECORDER_PLAYING == state ) ? "STOP" : "START";
            display_draw_text( 110, y, value, color, 1U );
        }
    }

    if( RFSCOUT_RECORDER_NO_STORAGE == state )
    {
        display_draw_text( 8, 142, "NO SD CARD - RECORD/PLAY DISABLED", RED, 1U );
    }
    else if( a_editing && RECORDER_MENU_FILE == a_selected )
    {
        display_draw_text( 8, 142, "TURN: FILE  CENTER: DONE", SUBDUED, 1U );
    }
    else if( a_editing )
    {
        display_draw_text( 8, 142, "ACTION COMPLETE - CENTER: DONE", SUBDUED, 1U );
    }
    else
    {
        display_draw_text( 8, 142, "TURN: SELECT  CENTER: ENTER", SUBDUED, 1U );
    }

    (void)display_present();
}
