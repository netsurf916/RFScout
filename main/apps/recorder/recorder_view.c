/**
    recorder_view.c : RFScout Recorder application UI
    Description: Renders recording, playback, and the SD-backed recording browser.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "recorder_view.h"

#include "display.h"
#include "recorder.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_theme.h"

#include "esp_timer.h"

#include <stddef.h>
#include <stdio.h>

#define RECORDER_BROWSER_BACK 0U
#define RECORDER_BROWSER_NEW 1U
#define RECORDER_BROWSER_FILE_BASE 2U

static char saving_spinner( void )
{
    static const char frames[] = "|/-\\";
    const uint64_t step = (uint64_t)( esp_timer_get_time() / 150000LL );
    return frames[ step % ( sizeof( frames ) - 1U ) ];
}

void recorder_view_landing( const char *a_playback_name, uint8_t a_playback_volume )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "RECORDER", theme->secondary );

    if( ui_game_active() )
    {
        ui_game_draw_panel(
            UI_LAYOUT_PANEL_LEFT_X, UI_LAYOUT_PANEL_TOP_Y, 312, 110, theme->divider );
        ui_game_draw_icon( UI_GAME_ICON_TAPE, 254, 34, 5U, theme->info );
    }
    const rfscout_recorder_state_t state = recorder_state();
    char line[ 80 ];
    if( RFSCOUT_RECORDER_RECORDING == state )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 38, "REC", theme->error, 2U );
        const uint32_t seconds = recorder_recording_elapsed_seconds();
        snprintf( line,
                  sizeof( line ),
                  "%02lu:%02lu  %s",
                  (unsigned long)( seconds / 60U ),
                  (unsigned long)( seconds % 60U ),
                  recorder_input_slot_name() );
        display_draw_text( UI_LAYOUT_LEFT_X, 69, line, theme->text, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 91, recorder_active_name(), theme->divider, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_FOOTER_Y,
                           "CENTER: STOP  NAVIGATION LOCKED",
                           theme->secondary,
                           1U );
    }
    else if( RFSCOUT_RECORDER_SAVING == state )
    {
        char saving[ 24 ];
        snprintf( saving, sizeof( saving ), "SAVING... %c", saving_spinner() );
        display_draw_text( UI_LAYOUT_LEFT_X, 42, saving, theme->warning, 2U );
        display_draw_text( UI_LAYOUT_LEFT_X, 78, recorder_active_name(), theme->text, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_FOOTER_Y,
                           "PLEASE WAIT - NAVIGATION LOCKED",
                           theme->secondary,
                           1U );
    }
    else if( RFSCOUT_RECORDER_PLAYING == state )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 40, "PLAYING", theme->success, 2U );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           72,
                           ( NULL != a_playback_name ) ? a_playback_name : "",
                           theme->text,
                           1U );
        snprintf( line, sizeof( line ), "VOLUME: %u%%", (unsigned)a_playback_volume );
        display_draw_text( UI_LAYOUT_LEFT_X, 94, line, theme->divider, 1U );
        if( ui_game_active() )
        {
            ui_game_draw_meter(
                UI_LAYOUT_LEFT_X, 113, 180, 12, a_playback_volume, 100U, theme->success );
        }
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_FOOTER_Y,
                           "CENTER: STOP  TURN: VOLUME",
                           theme->secondary,
                           1U );
    }
    else if( RFSCOUT_RECORDER_NO_STORAGE == state )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 42, "NO SD CARD", theme->error, 2U );
        display_draw_text( UI_LAYOUT_LEFT_X, 76, "SD storage unavailable", theme->divider, 1U );
    }
    else if( RFSCOUT_RECORDER_ERROR == state )
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 42, "RECORDER ERROR", theme->error, 2U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, 76, "Check SD card and microphone", theme->divider, 1U );
    }
    else
    {
        display_draw_text( UI_LAYOUT_LEFT_X, 42, "READY", theme->warning, 2U );
        snprintf( line, sizeof( line ), "%u RECORDINGS", (unsigned)recorder_file_count() );
        display_draw_text( UI_LAYOUT_LEFT_X, 78, line, theme->text, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X, 105, "16 kHz / 16-bit mono WAV", theme->divider, 1U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, UI_LAYOUT_FOOTER_Y, "CENTER: OPEN", theme->secondary, 1U );
    }
    (void)display_present();
}

void recorder_view_browser( unsigned a_selected,
                            bool a_delete_mode,
                            bool a_delete_confirm,
                            const char *a_playback_name,
                            uint8_t a_playback_volume )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( "RECORDER", theme->secondary );

    const rfscout_recorder_state_t state = recorder_state();
    if( ( RFSCOUT_RECORDER_RECORDING == state ) || ( RFSCOUT_RECORDER_SAVING == state ) ||
        ( RFSCOUT_RECORDER_PLAYING == state ) )
    {
        recorder_view_landing( a_playback_name, a_playback_volume );
        return;
    }

    if( a_delete_mode )
    {
        const size_t index = a_selected >= RECORDER_BROWSER_FILE_BASE
                                 ? (size_t)( a_selected - RECORDER_BROWSER_FILE_BASE )
                                 : 0U;
        const rfscout_recording_t *recording = recorder_file( index );
        display_draw_text( UI_LAYOUT_LEFT_X, 42, "DELETE RECORDING?", theme->error, 2U );
        display_draw_text(
            UI_LAYOUT_LEFT_X, 76, recording ? recording->name : "recording", theme->text, 1U );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           108,
                           a_delete_confirm ? "  CANCEL   > DELETE" : "> CANCEL     DELETE",
                           a_delete_confirm ? theme->error : theme->selected,
                           1U );
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_FOOTER_Y,
                           "TURN: CHOOSE  CENTER: CONFIRM",
                           theme->secondary,
                           1U );
        (void)display_present();
        return;
    }

    const unsigned item_count = RECORDER_BROWSER_FILE_BASE + (unsigned)recorder_file_count();
    const unsigned first = a_selected > 3U ? a_selected - 3U : 0U;
    for( unsigned row = 0U; row < 6U && first + row < item_count; ++row )
    {
        const unsigned item = first + row;
        const bool selected = item == a_selected;
        char line[ 96 ];
        if( RECORDER_BROWSER_BACK == item )
        {
            snprintf( line, sizeof( line ), "%c BACK", selected ? '>' : ' ' );
        }
        else if( RECORDER_BROWSER_NEW == item )
        {
            snprintf( line, sizeof( line ), "%c NEW RECORDING", selected ? '>' : ' ' );
        }
        else
        {
            const rfscout_recording_t *recording =
                recorder_file( (size_t)( item - RECORDER_BROWSER_FILE_BASE ) );
            if( NULL == recording )
            {
                continue;
            }
            snprintf( line,
                      sizeof( line ),
                      "%c %-22.22s %02lu:%02lu",
                      selected ? '>' : ' ',
                      recording->name,
                      (unsigned long)( recording->duration_seconds / 60U ),
                      (unsigned long)( recording->duration_seconds % 60U ) );
        }
        const int y = 38 + (int)row * 18;
        if( ui_game_active() )
        {
            ui_game_draw_menu_marker( UI_LAYOUT_LEFT_X, y, selected ? ">" : " ", theme->selected );
            line[ 0 ] = ' ';
        }
        display_draw_text(
            UI_LAYOUT_LEFT_X, y, line, selected ? theme->selected : theme->text, 1U );
    }

    display_draw_text( UI_LAYOUT_LEFT_X,
                       UI_LAYOUT_FOOTER_Y,
                       "CENTER: SELECT  LONG PRESS: DELETE",
                       theme->secondary,
                       1U );
    (void)display_present();
}
