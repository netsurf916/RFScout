/**
    ui_menu.c : RFScout semantic menu-state presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "ui_menu.h"
#include "ui_game.h"
#include "ui_layout.h"
#include "ui_icon.h"
#include "ui_theme.h"
#include "display.h"

uint16_t ui_menu_label_color( ui_menu_state_t a_state )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    if( !a_state.available || a_state.locked )
    {
        return theme->secondary;
    }
    return a_state.selected ? theme->selected : theme->text;
}

uint16_t ui_menu_value_color( ui_menu_state_t a_state )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    if( !a_state.available || a_state.locked )
    {
        return theme->secondary;
    }
    return a_state.selected ? theme->accent : theme->secondary;
}

void ui_menu_draw_marker( int a_x, int a_y, ui_menu_state_t a_state )
{
    if( !a_state.selected && !a_state.locked )
    {
        return;
    }
    const uint16_t color = ui_menu_label_color( a_state );
    if( a_state.selected )
    {
        ui_game_draw_menu_marker( a_x, a_y, a_state.editing ? "*" : ">", color );
    }
    if( a_state.locked )
    {
        ui_icon_draw_lock(
            a_x + ( UI_LAYOUT_MENU_LOCK_X - UI_LAYOUT_LEFT_X ), a_y - 2, false, color );
    }
}

unsigned ui_menu_window_start( unsigned a_selected, unsigned a_count, unsigned a_visible )
{
    if( 0U == a_visible || a_count <= a_visible || a_selected < a_visible )
    {
        return 0U;
    }
    const unsigned start = a_selected - a_visible + 1U;
    const unsigned maximum = a_count - a_visible;
    return start > maximum ? maximum : start;
}

void ui_menu_draw_row( int a_y, const char *a_label, const char *a_value, ui_menu_state_t a_state )
{
    ui_menu_draw_marker( UI_LAYOUT_LEFT_X, a_y, a_state );
    if( NULL != a_label )
    {
        display_draw_text( UI_LAYOUT_LABEL_X, a_y, a_label, ui_menu_label_color( a_state ), 1U );
    }
    if( NULL != a_value && '\0' != a_value[ 0 ] )
    {
        display_draw_text(
            UI_LAYOUT_MENU_VALUE_X, a_y, a_value, ui_menu_value_color( a_state ), 1U );
    }
}

unsigned ui_menu_draw_rows( const ui_menu_row_t *a_rows,
                            size_t a_count,
                            unsigned a_selected,
                            unsigned a_visible,
                            int a_start_y )
{
    const unsigned start = ui_menu_window_start( a_selected, (unsigned)a_count, a_visible );
    for( unsigned row = 0U; NULL != a_rows && row < a_visible && start + row < a_count; ++row )
    {
        ui_menu_state_t state = a_rows[ start + row ].state;
        state.selected = start + row == a_selected;
        ui_menu_draw_row( a_start_y + (int)row * UI_LAYOUT_MENU_ROW_HEIGHT,
                          a_rows[ start + row ].label,
                          a_rows[ start + row ].value,
                          state );
    }
    return start;
}
