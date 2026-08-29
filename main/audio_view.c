/**
    audio_view.c : RFScout Recorder compatibility UI
    Description: State-driven Recorder UI in the former Audio application slot.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "audio_view.h"

#include "device_config.h"
#include "display.h"
#include "recorder.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include "esp_timer.h"

#include <stdio.h>

#define RECORDER_BROWSER_BACK 0U
#define RECORDER_BROWSER_NEW 1U
#define RECORDER_BROWSER_MONITOR 2U
#define RECORDER_BROWSER_FILE_BASE 3U

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
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "RECORDER", theme->secondary );

    const rfscout_recorder_state_t state = recorder_state();
    char line[ 80 ];
    if( RFSCOUT_RECORDER_RECORDING == state )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 38, "● REC", theme->error, 2U );
        const uint32_t seconds = recorder_recording_elapsed_seconds();
        snprintf( line,
                  sizeof( line ),
                  "%02lu:%02lu  %s",
                  (unsigned long)( seconds / 60U ),
                  (unsigned long)( seconds % 60U ),
                  recorder_input_slot_name() );
        display_draw_text( UI_LAYOUT_LEFT_X, 69, line, theme->text, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 88, recorder_active_name(), theme->divider, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 114, "Recording audio...", theme->divider, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: STOP", theme->secondary, 1U );
    }
    else if( RFSCOUT_RECORDER_SAVING == state )
    {
        char saving_line[ 24 ];
        snprintf( saving_line, sizeof( saving_line ), "SAVING... %c", saving_spinner() );
        display_draw_text( UI_LAYOUT_LEFT_X, 42, saving_line, theme->warning, 2U );
        display_draw_text( UI_LAYOUT_LEFT_X, 77, recorder_active_name(), theme->text, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 104, "TRIM + NORMALIZE", theme->divider, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "PLEASE WAIT", theme->secondary, 1U );
    }
    else if( RFSCOUT_RECORDER_NO_STORAGE == state )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 42, "NO SD CARD", theme->error, 2U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, 75, "Recorder needs /sdcard/audio", theme->divider, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_FOOTER_Y,
                           "CENTER: OPEN / BACK",
                           theme->secondary,
                           1U );
    }
    else if( RFSCOUT_RECORDER_ERROR == state )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 42, "RECORDER ERROR", theme->error, 2U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, 75, "Check SD card and microphone", theme->divider, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: OPEN", theme->secondary, 1U );
    }
    else
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 42, "READY", theme->warning, 2U );
        snprintf( line, sizeof( line ), "%u RECORDINGS", (unsigned)recorder_file_count() );
        display_draw_text( UI_LAYOUT_LEFT_X, 78, line, theme->text, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, 105, "calibrated diagnostic WAV", theme->divider, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: OPEN", theme->secondary, 1U );
    }
    (void)display_present();
}

void audio_view_menu( unsigned a_selected, bool a_editing, bool a_unused )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const rfscout_recorder_state_t state = recorder_state();
    display_clear( theme->background );
    ui_chrome_draw( "RECORDER", theme->secondary );

    if( a_editing )
    {
        const size_t index = ( a_selected >= RECORDER_BROWSER_FILE_BASE )
                                 ? a_selected - RECORDER_BROWSER_FILE_BASE
                                 : 0U;
        const rfscout_recording_t *recording = recorder_file( index );
        display_draw_text( UI_LAYOUT_LEFT_X, 42, "DELETE RECORDING?", theme->error, 2U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, 76, recording ? recording->name : "recording", theme->text, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           108,
                           a_unused ? "  CANCEL   > DELETE" : "> CANCEL     DELETE",
                           a_unused ? theme->error : theme->selected,
                           1U );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_FOOTER_Y,
                           "TURN: CHOOSE  CENTER: CONFIRM",
                           theme->secondary,
                           1U );
        (void)display_present();
        return;
    }

    if( RFSCOUT_RECORDER_RECORDING == state )
    {
        char line[ 80 ];
        display_draw_text( UI_LAYOUT_LEFT_X, 38, "● REC", theme->error, 2U );
        const uint32_t seconds = recorder_recording_elapsed_seconds();
        snprintf( line,
                  sizeof( line ),
                  "%02lu:%02lu  INPUT %s",
                  (unsigned long)( seconds / 60U ),
                  (unsigned long)( seconds % 60U ),
                  recorder_input_slot_name() );
        display_draw_text( UI_LAYOUT_LEFT_X, 69, line, theme->text, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 88, recorder_active_name(), theme->divider, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 114, "Recording audio...", theme->divider, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: STOP", theme->secondary, 1U );
        (void)display_present();
        return;
    }

    if( RFSCOUT_RECORDER_SAVING == state )
    {
        char saving_line[ 24 ];
        snprintf( saving_line, sizeof( saving_line ), "SAVING... %c", saving_spinner() );
        display_draw_text( UI_LAYOUT_LEFT_X, 42, saving_line, theme->warning, 2U );
        display_draw_text( UI_LAYOUT_LEFT_X, 77, recorder_active_name(), theme->text, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 104, "TRIM + NORMALIZE", theme->divider, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 130, "INPUT LOCKED", theme->secondary, 1U );
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
        display_draw_text( UI_LAYOUT_LEFT_X, 34, line, theme->text, 1U );
        const int graph_x = UI_LAYOUT_LEFT_X, graph_y = 58, graph_w = 224, graph_h = 66;
        for( size_t i = 0U; i < count; ++i )
        {
            const int x = graph_x + (int)( i * graph_w / count );
            const int h = (int)bins[ i ] * graph_h / 100;
            if( h > 0 )
                display_fill_rect( x, graph_y + graph_h - h, 2, h, theme->graph_primary );
        }
        display_draw_text( UI_LAYOUT_LEFT_X, 130, "TURN: INPUT GAIN", theme->secondary, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: EXIT", theme->secondary, 1U );
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
        display_draw_text( UI_LAYOUT_LEFT_X, 38, "PLAYING", theme->success, 2U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, 67, recording ? recording->name : "recording", theme->text, 1U );
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
        display_draw_text( UI_LAYOUT_LEFT_X, 96, line, theme->divider, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 122, "TURN: VOLUME", theme->secondary, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: STOP", theme->secondary, 1U );
        (void)display_present();
        return;
    }

    if( RFSCOUT_RECORDER_NO_STORAGE == state )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 44, "> BACK", theme->selected, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 74, "NO SD CARD", theme->error, 2U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, 110, "/sdcard/audio unavailable", theme->divider, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: BACK", theme->secondary, 1U );
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
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_MENU_START_Y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT,
                           line,
                           item == a_selected ? theme->selected : theme->text,
                           1U );
    }
    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       "CENTER: PLAY  HOLD: DELETE",
                       theme->secondary,
                       1U );
    (void)display_present();
}
