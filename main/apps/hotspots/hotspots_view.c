/**
    apps/hotspots/hotspots_view.c : Hotspots presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "hotspots_view.h"

#include "display.h"
#include "ui_chrome.h"
#include "ui_layout.h"
#include "ui_menu.h"
#include "ui_theme.h"

#include <stdio.h>
#include <string.h>

#define HOTSPOTS_FONT_WIDTH 6
#define HOTSPOTS_LANDING_ROWS 6U
#define HOTSPOTS_LANDING_ROW_HEIGHT 18
#define HOTSPOTS_TAG_X 166
#define HOTSPOTS_COUNT_RIGHT_X 218
#define HOTSPOTS_VALUE_RIGHT_X 314
#define HOTSPOTS_DETAIL_Y 26
#define HOTSPOTS_DETAIL_LINE_HEIGHT 10
#define HOTSPOTS_DETAIL_ACTION_Y 130
#define HOTSPOTS_DETAIL_ACTION_HEIGHT 12
#define HOTSPOTS_TEXT_MAX_CHARS 51U

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

static void draw_right( int a_right_x, int a_y, const char *a_text, uint16_t a_color )
{
    display_draw_text(
        a_right_x - (int)strlen( a_text ) * HOTSPOTS_FONT_WIDTH, a_y, a_text, a_color, 1U );
}

/** @brief Draw label, tag, member count and value columns for one row. */
static void draw_row( int a_label_x, int a_y, const hotspots_view_row_t *a_row, uint16_t a_label )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    draw_clipped( a_label_x,
                  a_y,
                  a_row->label,
                  a_label,
                  (size_t)( HOTSPOTS_TAG_X - a_label_x ) / HOTSPOTS_FONT_WIDTH - 1U );
    draw_clipped( HOTSPOTS_TAG_X, a_y, a_row->tag, tone_color( a_row->tone ), 5U );
    if( a_row->members > 0U )
    {
        char members[ 8 ];
        snprintf( members, sizeof( members ), "%u", (unsigned)a_row->members );
        draw_right( HOTSPOTS_COUNT_RIGHT_X, a_y, members, theme->secondary );
    }
    draw_right( HOTSPOTS_VALUE_RIGHT_X,
                a_y,
                NULL != a_row->value ? a_row->value : "--",
                a_row->visible ? theme->text : theme->divider );
}

static void draw_footer( const char *a_footer )
{
    draw_clipped( UI_LAYOUT_LEFT_X,
                  UI_LAYOUT_FOOTER_Y,
                  a_footer,
                  ui_theme_get()->secondary,
                  HOTSPOTS_TEXT_MAX_CHARS );
}

void hotspots_view_landing( const char *a_title,
                            const hotspots_view_row_t *a_rows,
                            size_t a_count,
                            const char *a_empty,
                            const char *a_footer )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( a_title, theme->secondary );

    if( 0U == a_count )
    {
        draw_clipped( UI_LAYOUT_LEFT_X,
                      UI_LAYOUT_MENU_START_Y + HOTSPOTS_LANDING_ROW_HEIGHT,
                      a_empty,
                      theme->secondary,
                      HOTSPOTS_TEXT_MAX_CHARS );
    }
    for( size_t row = 0U; ( row < a_count ) && ( row < HOTSPOTS_LANDING_ROWS ); ++row )
    {
        const hotspots_view_row_t *item = &a_rows[ row ];
        draw_row( UI_LAYOUT_LEFT_X,
                  UI_LAYOUT_MENU_START_Y + (int)row * HOTSPOTS_LANDING_ROW_HEIGHT,
                  item,
                  item->visible ? theme->text : theme->divider );
    }

    draw_footer( a_footer );
    (void)display_present();
}

void hotspots_view_menu( const char *a_title,
                         const ui_menu_row_t *a_fixed,
                         size_t a_fixed_count,
                         const hotspots_view_row_t *a_rows,
                         size_t a_count,
                         unsigned a_selected,
                         const char *a_footer )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    const unsigned item_count = (unsigned)( a_fixed_count + a_count );
    const unsigned start =
        ui_menu_window_start( a_selected, item_count, UI_LAYOUT_MENU_VISIBLE_ROWS );

    display_clear( theme->background );
    ui_chrome_draw( a_title, theme->secondary );

    for( unsigned row = 0U; row < UI_LAYOUT_MENU_VISIBLE_ROWS && start + row < item_count; ++row )
    {
        const unsigned item = start + row;
        const int y = UI_LAYOUT_MENU_START_Y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT;
        if( item < a_fixed_count )
        {
            ui_menu_state_t state = a_fixed[ item ].state;
            state.selected = item == a_selected;
            ui_menu_draw_row( y, a_fixed[ item ].label, a_fixed[ item ].value, state );
            continue;
        }
        const hotspots_view_row_t *entry = &a_rows[ item - a_fixed_count ];
        const ui_menu_state_t state = { .selected = item == a_selected,
                                        .available = entry->visible };
        ui_menu_draw_marker( UI_LAYOUT_LEFT_X, y, state );
        draw_row( UI_LAYOUT_LABEL_X, y, entry, ui_menu_label_color( state ) );
    }

    draw_footer( a_footer );
    (void)display_present();
}

void hotspots_view_detail( const char *a_title,
                           const hotspots_view_detail_t *a_detail,
                           unsigned a_selected_action )
{
    if( NULL == a_detail )
    {
        return;
    }

    const ui_theme_palette_t *theme = ui_theme_get();
    display_clear( theme->background );
    ui_chrome_draw( a_title, theme->secondary );

    for( size_t index = 0U; index < a_detail->line_count && index < HOTSPOTS_VIEW_DETAIL_LINES;
         ++index )
    {
        uint16_t color = ( 0U == index ) ? theme->text : theme->secondary;
        if( a_detail->status_line == index )
        {
            color = tone_color( a_detail->status_tone );
        }
        draw_clipped( UI_LAYOUT_LEFT_X,
                      HOTSPOTS_DETAIL_Y + (int)index * HOTSPOTS_DETAIL_LINE_HEIGHT,
                      a_detail->lines[ index ],
                      color,
                      HOTSPOTS_TEXT_MAX_CHARS );
    }

    for( size_t action = 0U;
         action < a_detail->action_count && action < HOTSPOTS_VIEW_DETAIL_ACTIONS;
         ++action )
    {
        ui_menu_state_t state = a_detail->actions[ action ].state;
        state.selected = action == a_selected_action;
        ui_menu_draw_row( HOTSPOTS_DETAIL_ACTION_Y + (int)action * HOTSPOTS_DETAIL_ACTION_HEIGHT,
                          a_detail->actions[ action ].label,
                          a_detail->actions[ action ].value,
                          state );
    }
    (void)display_present();
}
