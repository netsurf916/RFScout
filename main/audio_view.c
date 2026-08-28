/**
    audio_view.c : RFScout Recorder compatibility UI
    Description: State-driven Recorder UI in the former Audio application slot.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "audio_view.h"

#include "battery_indicator.h"
#include "device_config.h"
#include "display.h"
#include "recorder.h"
#include "status_bar.h"

#include "esp_timer.h"

#include <stdio.h>

#define BG 0x0000U
#define SUBDUED 0x03F0U
#define TEXT 0xBDF7U
#define DIM 0x7BEFU
#define YELLOW 0xFFE0U
#define RED 0xF800U
#define GREEN 0x07E0U
#define RECORDER_BROWSER_BACK 0U
#define RECORDER_BROWSER_NEW 1U
#define RECORDER_BROWSER_MONITOR 2U
#define RECORDER_BROWSER_FILE_BASE 3U

static void draw_status_chrome( void )
{
    status_bar_draw();
    battery_indicator_draw();
}

static char saving_spinner( void )
{
    static const char frames[] = "|/-\\";
    const uint64_t step = (uint64_t)( esp_timer_get_time() / 150000LL );
    return frames[ step % ( sizeof( frames ) - 1U ) ];
}

void audio_view_render( const uint8_t *a_current, const uint8_t *a_peak )
{
    (void)a_current;
    (void)a_peak;
    display_clear( BG );
    display_draw_text( 8, 7, "RECORDER", SUBDUED, 2U );
    draw_status_chrome();

    const rfscout_recorder_state_t state = recorder_state();
    char line[ 80 ];
    if( RFSCOUT_RECORDER_RECORDING == state )
    {
        display_draw_text( 8, 38, "● REC", RED, 2U );
        const uint32_t seconds = recorder_recording_elapsed_seconds();
        snprintf( line,
                  sizeof( line ),
                  "%02lu:%02lu  %s",
                  (unsigned long)( seconds / 60U ),
                  (unsigned long)( seconds % 60U ),
                  recorder_input_slot_name() );
        display_draw_text( 8, 69, line, TEXT, 1U );
        display_draw_text( 8, 88, recorder_active_name(), DIM, 1U );
        display_draw_text( 8, 114, "Recording audio...", DIM, 1U );
        display_draw_text( 8, 144, "CENTER: STOP", SUBDUED, 1U );
    }
    else if( RFSCOUT_RECORDER_SAVING == state )
    {
        char saving_line[ 24 ];
        snprintf( saving_line, sizeof( saving_line ), "SAVING... %c", saving_spinner() );
        display_draw_text( 8, 42, saving_line, YELLOW, 2U );
        display_draw_text( 8, 77, recorder_active_name(), TEXT, 1U );
        display_draw_text( 8, 104, "TRIM + NORMALIZE", DIM, 1U );
        display_draw_text( 8, 144, "PLEASE WAIT", SUBDUED, 1U );
    }
    else if( RFSCOUT_RECORDER_NO_STORAGE == state )
    {
        display_draw_text( 8, 42, "NO SD CARD", RED, 2U );
        display_draw_text( 8, 75, "Recorder needs /sdcard/audio", DIM, 1U );
        display_draw_text( 8, 144, "CENTER: OPEN / BACK", SUBDUED, 1U );
    }
    else if( RFSCOUT_RECORDER_ERROR == state )
    {
        display_draw_text( 8, 42, "RECORDER ERROR", RED, 2U );
        display_draw_text( 8, 75, "Check SD card and microphone", DIM, 1U );
        display_draw_text( 8, 144, "CENTER: OPEN", SUBDUED, 1U );
    }
    else
    {
        display_draw_text( 8, 42, "READY", YELLOW, 2U );
        snprintf( line, sizeof( line ), "%u RECORDINGS", (unsigned)recorder_file_count() );
        display_draw_text( 8, 78, line, TEXT, 1U );
        display_draw_text( 8, 105, "calibrated diagnostic WAV", DIM, 1U );
        display_draw_text( 8, 144, "CENTER: OPEN", SUBDUED, 1U );
    }
    (void)display_present();
}

void audio_view_menu( unsigned a_selected, bool a_editing, bool a_unused )
{
    const rfscout_recorder_state_t state = recorder_state();
    display_clear( BG );
    display_draw_text( 8, 7, "RECORDER", SUBDUED, 2U );
    draw_status_chrome();

    if( a_editing )
    {
        const size_t index = ( a_selected >= RECORDER_BROWSER_FILE_BASE )
                                 ? a_selected - RECORDER_BROWSER_FILE_BASE
                                 : 0U;
        const rfscout_recording_t *recording = recorder_file( index );
        display_draw_text( 8, 42, "DELETE RECORDING?", RED, 2U );
        display_draw_text( 8, 76, recording ? recording->name : "recording", TEXT, 1U );
        display_draw_text( 8,
                           108,
                           a_unused ? "  CANCEL   > DELETE" : "> CANCEL     DELETE",
                           a_unused ? RED : YELLOW,
                           1U );
        display_draw_text( 8, 144, "TURN: CHOOSE  CENTER: CONFIRM", SUBDUED, 1U );
        (void)display_present();
        return;
    }

    if( RFSCOUT_RECORDER_RECORDING == state )
    {
        char line[ 80 ];
        display_draw_text( 8, 38, "● REC", RED, 2U );
        const uint32_t seconds = recorder_recording_elapsed_seconds();
        snprintf( line,
                  sizeof( line ),
                  "%02lu:%02lu  INPUT %s",
                  (unsigned long)( seconds / 60U ),
                  (unsigned long)( seconds % 60U ),
                  recorder_input_slot_name() );
        display_draw_text( 8, 69, line, TEXT, 1U );
        display_draw_text( 8, 88, recorder_active_name(), DIM, 1U );
        display_draw_text( 8, 114, "Recording audio...", DIM, 1U );
        display_draw_text( 8, 144, "CENTER: STOP", SUBDUED, 1U );
        (void)display_present();
        return;
    }

    if( RFSCOUT_RECORDER_SAVING == state )
    {
        char saving_line[ 24 ];
        snprintf( saving_line, sizeof( saving_line ), "SAVING... %c", saving_spinner() );
        display_draw_text( 8, 42, saving_line, YELLOW, 2U );
        display_draw_text( 8, 77, recorder_active_name(), TEXT, 1U );
        display_draw_text( 8, 104, "TRIM + NORMALIZE", DIM, 1U );
        display_draw_text( 8, 130, "INPUT LOCKED", SUBDUED, 1U );
        (void)display_present();
        return;
    }

    if( RFSCOUT_RECORDER_MONITORING == state )
    {
        uint8_t bins[ RFSCOUT_RECORDER_MONITOR_BINS ];
        const size_t count = recorder_monitor_bins( bins, sizeof( bins ) );
        const device_config_t *config = device_config_get();
        char line[ 32 ];
        snprintf( line,
                  sizeof( line ),
                  "MIC MONITOR   GAIN +%u dB",
                  (unsigned)( NULL != config ? config->recorder_input_gain_db
                                             : recorder_input_gain_db() ) );
        display_draw_text( 8, 34, line, TEXT, 1U );
        const int graph_x = 8, graph_y = 58, graph_w = 224, graph_h = 66;
        for( size_t i = 0U; i < count; ++i )
        {
            const int x = graph_x + (int)( i * graph_w / count );
            const int h = (int)bins[ i ] * graph_h / 100;
            if( h > 0 )
                display_fill_rect( x, graph_y + graph_h - h, 2, h, GREEN );
        }
        display_draw_text( 8, 130, "TURN: INPUT GAIN", SUBDUED, 1U );
        display_draw_text( 8, 144, "CENTER: EXIT", SUBDUED, 1U );
        (void)display_present();
        return;
    }

    if( RFSCOUT_RECORDER_PLAYING == state )
    {
        const size_t index = ( a_selected >= RECORDER_BROWSER_FILE_BASE )
                                 ? a_selected - RECORDER_BROWSER_FILE_BASE
                                 : 0U;
        const rfscout_recording_t *recording = recorder_file( index );
        const device_config_t *config = device_config_get();
        const unsigned volume = ( NULL != config ) ? config->recorder_volume_percent : 50U;
        char line[ 80 ];
        display_draw_text( 8, 38, "PLAYING", GREEN, 2U );
        display_draw_text( 8, 67, recording ? recording->name : "recording", TEXT, 1U );
        const uint32_t elapsed = recorder_playback_elapsed_seconds();
        const uint32_t total = recording ? recording->duration_seconds : 0U;
        snprintf( line,
                  sizeof( line ),
                  "%02lu:%02lu / %02lu:%02lu   VOL %u%%",
                  (unsigned long)( elapsed / 60U ),
                  (unsigned long)( elapsed % 60U ),
                  (unsigned long)( total / 60U ),
                  (unsigned long)( total % 60U ),
                  volume );
        display_draw_text( 8, 96, line, DIM, 1U );
        display_draw_text( 8, 122, "TURN: VOLUME", SUBDUED, 1U );
        display_draw_text( 8, 144, "CENTER: STOP", SUBDUED, 1U );
        (void)display_present();
        return;
    }

    if( RFSCOUT_RECORDER_NO_STORAGE == state )
    {
        display_draw_text( 8, 44, "> BACK", YELLOW, 1U );
        display_draw_text( 8, 74, "NO SD CARD", RED, 2U );
        display_draw_text( 8, 110, "/sdcard/audio unavailable", DIM, 1U );
        display_draw_text( 8, 144, "CENTER: BACK", SUBDUED, 1U );
        (void)display_present();
        return;
    }

    const size_t count = recorder_file_count();
    const size_t item_count = RECORDER_BROWSER_FILE_BASE + count;
    if( a_selected >= item_count )
    {
        a_selected = 0U;
    }
    const unsigned first = ( a_selected > 2U ) ? a_selected - 2U : 0U;
    for( unsigned row = 0U; row < 5U && ( first + row ) < item_count; ++row )
    {
        const unsigned item = first + row;
        char line[ 96 ];
        if( RECORDER_BROWSER_BACK == item )
        {
            snprintf( line, sizeof( line ), "%c BACK", item == a_selected ? '>' : ' ' );
        }
        else if( RECORDER_BROWSER_NEW == item )
        {
            snprintf( line, sizeof( line ), "%c NEW RECORDING", item == a_selected ? '>' : ' ' );
        }
        else if( RECORDER_BROWSER_MONITOR == item )
        {
            snprintf( line, sizeof( line ), "%c MONITOR", item == a_selected ? '>' : ' ' );
        }
        else
        {
            const rfscout_recording_t *recording =
                recorder_file( item - RECORDER_BROWSER_FILE_BASE );
            snprintf( line,
                      sizeof( line ),
                      "%c %-19.19s %02lu:%02lu",
                      item == a_selected ? '>' : ' ',
                      recording ? recording->name : "?",
                      (unsigned long)( recording ? recording->duration_seconds / 60U : 0U ),
                      (unsigned long)( recording ? recording->duration_seconds % 60U : 0U ) );
        }
        display_draw_text( 8, 38 + (int)row * 20, line, item == a_selected ? YELLOW : TEXT, 1U );
    }
    display_draw_text( 8, 144, "CENTER: PLAY  HOLD: DELETE", SUBDUED, 1U );
    (void)display_present();
}
