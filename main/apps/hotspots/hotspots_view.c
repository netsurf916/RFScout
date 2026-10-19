/**
    apps/hotspots/hotspots_view.c : Hotspots presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "hotspots_view.h"

#include "display.h"
#include "ui_chrome.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_theme.h"
#include "ui_menu.h"

#include <stdio.h>
#include <string.h>

#define HOTSPOTS_TITLE "HOTSPOTS"
#define HOTSPOTS_LANDING_ROWS 6U
#define HOTSPOTS_LANDING_ROW_HEIGHT 18
#define HOTSPOTS_SSID_MAX_CHARS 26U
#define HOTSPOTS_TAG_X 196
#define HOTSPOTS_RSSI_X 274
#define HOTSPOTS_DETAIL_LINE_HEIGHT 10
#define HOTSPOTS_DETAIL_ACTION_Y 144
#define HOTSPOTS_DETAIL_ACTION_HEIGHT 12
#define HOTSPOTS_DETAIL_TEXT_MAX_CHARS 51U

/** @brief Map a semantic tone onto the active theme palette. */
static uint16_t tone_color( hotspots_view_tone_t a_tone )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    switch( a_tone )
    {
        case HOTSPOTS_TONE_GOOD:
            return theme->success;
        case HOTSPOTS_TONE_LIMITED:
            return theme->warning;
        case HOTSPOTS_TONE_BAD:
            return theme->error;
        case HOTSPOTS_TONE_ACTIVE:
            return theme->accent;
        case HOTSPOTS_TONE_NEUTRAL:
        default:
            return theme->secondary;
    }
}

/** @brief Draw text truncated to a character budget. */
static void draw_clipped( int a_x, int a_y, const char *a_text, uint16_t a_color, size_t a_max )
{
    char buffer[ 64 ];
    const size_t limit = ( a_max < sizeof( buffer ) - 1U ) ? a_max : sizeof( buffer ) - 1U;
    snprintf( buffer, sizeof( buffer ), "%.*s", (int)limit, ( NULL != a_text ) ? a_text : "" );
    display_draw_text( a_x, a_y, buffer, a_color, 1U );
}

/** @brief Draw the status tag and RSSI columns shared by landing and browser rows. */
static void draw_row_columns( int a_y, const hotspots_view_row_t *a_row )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    draw_clipped( HOTSPOTS_TAG_X, a_y, a_row->tag, tone_color( a_row->tone ), 6U );

    char rssi[ 8 ];
    if( a_row->visible )
    {
        snprintf( rssi, sizeof( rssi ), "%d", (int)a_row->rssi );
    }
    else
    {
        snprintf( rssi, sizeof( rssi ), "--" );
    }
    display_draw_text(
        HOTSPOTS_RSSI_X, a_y, rssi, a_row->visible ? theme->text : theme->divider, 1U );
}

static void draw_footer( const char *a_footer )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    draw_clipped( UI_LAYOUT_LEFT_X,
                  UI_LAYOUT_FOOTER_Y,
                  a_footer,
                  theme->secondary,
                  HOTSPOTS_DETAIL_TEXT_MAX_CHARS );
}

void hotspots_view_landing( const hotspots_view_row_t *a_rows,
                            size_t a_count,
                            const char *a_footer )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( HOTSPOTS_TITLE, theme->secondary );

    if( 0U == a_count )
    {
        display_draw_text( UI_LAYOUT_LEFT_X,
                           UI_LAYOUT_MENU_START_Y + HOTSPOTS_LANDING_ROW_HEIGHT,
                           "No open networks visible",
                           theme->secondary,
                           1U );
    }

    for( size_t row = 0U; ( row < a_count ) && ( row < HOTSPOTS_LANDING_ROWS ); ++row )
    {
        const int y = UI_LAYOUT_MENU_START_Y + (int)row * HOTSPOTS_LANDING_ROW_HEIGHT;
        const hotspots_view_row_t *item = &a_rows[ row ];
        draw_clipped( UI_LAYOUT_LEFT_X,
                      y,
                      item->ssid,
                      item->visible ? theme->text : theme->divider,
                      HOTSPOTS_SSID_MAX_CHARS );
        draw_row_columns( y, item );
    }

    draw_footer( a_footer );
    (void)display_present();
}

void hotspots_view_browser( const hotspots_view_row_t *a_rows,
                            size_t a_count,
                            unsigned a_selected,
                            bool a_auto_enabled,
                            const char *a_footer )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned item_count = HOTSPOTS_BROWSER_FIXED_ITEMS + (unsigned)a_count;
    const unsigned start = ui_layout_menu_window_start( a_selected, item_count );

    display_clear( theme->background );
    ui_chrome_draw( HOTSPOTS_TITLE, theme->secondary );

    for( unsigned row = 0U; row < UI_LAYOUT_MENU_VISIBLE_ROWS; ++row )
    {
        const unsigned item = start + row;
        if( item >= item_count )
        {
            break;
        }
        const int y = UI_LAYOUT_MENU_START_Y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT;
        const bool active = item == a_selected;
        const uint16_t label_color =
            ui_menu_label_color( ( ui_menu_state_t ){ .selected = active, .available = true } );
        if( active )
        {
            ui_game_draw_menu_marker( UI_LAYOUT_LEFT_X, y, ">", label_color );
        }

        if( HOTSPOTS_BROWSER_ITEM_BACK == item )
        {
            display_draw_text( UI_LAYOUT_LABEL_X, y, "Back", label_color, 1U );
            continue;
        }
        if( HOTSPOTS_BROWSER_ITEM_AUTO == item )
        {
            display_draw_text( UI_LAYOUT_LABEL_X, y, "Auto check", label_color, 1U );
            display_draw_text( HOTSPOTS_TAG_X,
                               y,
                               a_auto_enabled ? "On" : "Off",
                               active ? theme->accent : theme->secondary,
                               1U );
            continue;
        }

        const hotspots_view_row_t *entry = &a_rows[ item - HOTSPOTS_BROWSER_FIXED_ITEMS ];
        draw_clipped( UI_LAYOUT_LABEL_X,
                      y,
                      entry->ssid,
                      active ? theme->selected : ( entry->visible ? theme->text : theme->divider ),
                      HOTSPOTS_SSID_MAX_CHARS - 3U );
        draw_row_columns( y, entry );
    }

    draw_footer( a_footer );
    (void)display_present();
}

void hotspots_view_detail( const hotspots_view_detail_t *a_detail, unsigned a_selected_action )
{
    if( NULL == a_detail )
    {
        return;
    }

    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( HOTSPOTS_TITLE, theme->secondary );

    const char *lines[] = {
        a_detail->ssid,
        a_detail->bssid,
        a_detail->radio,
        a_detail->security,
        a_detail->status,
        a_detail->last_attempt,
        a_detail->addresses,
        a_detail->network,
        a_detail->dns,
        a_detail->egress,
    };
    for( size_t index = 0U; index < sizeof( lines ) / sizeof( lines[ 0 ] ); ++index )
    {
        uint16_t color = ( 0U == index ) ? theme->text : theme->secondary;
        if( 4U == index )
        {
            color = tone_color( a_detail->status_tone );
        }
        draw_clipped( UI_LAYOUT_LEFT_X,
                      UI_LAYOUT_MENU_START_Y + (int)index * HOTSPOTS_DETAIL_LINE_HEIGHT,
                      lines[ index ],
                      color,
                      HOTSPOTS_DETAIL_TEXT_MAX_CHARS );
    }

    const char *actions[ HOTSPOTS_DETAIL_ACTION_COUNT ] = {
        "Back",
        a_detail->recheck_available ? "Recheck" : "Recheck (not visible)",
    };
    for( unsigned action = 0U; action < HOTSPOTS_DETAIL_ACTION_COUNT; ++action )
    {
        const int y = HOTSPOTS_DETAIL_ACTION_Y + (int)action * HOTSPOTS_DETAIL_ACTION_HEIGHT;
        const bool active = action == a_selected_action;
        const bool enabled =
            ( HOTSPOTS_DETAIL_ACTION_RECHECK != action ) || a_detail->recheck_available;
        const uint16_t color =
            ui_menu_label_color( ( ui_menu_state_t ){ .selected = active, .available = enabled } );
        if( active )
        {
            ui_game_draw_menu_marker( UI_LAYOUT_LEFT_X, y, ">", color );
        }
        display_draw_text( UI_LAYOUT_LABEL_X, y, actions[ action ], color, 1U );
    }
    (void)display_present();
}
