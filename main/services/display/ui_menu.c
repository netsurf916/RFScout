/**
    ui_menu.c : RFScout semantic menu-state presentation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "ui_menu.h"
#include "ui_game.h"
#include "ui_icon.h"
#include "ui_theme.h"

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
        ui_icon_draw_lock( a_x + 12, a_y - 2, false, color );
    }
}
