/**
    ui_layout.h : RFScout shared UI layout metrics
    Description: Defines common screen gutters, title origin, menu spacing, and footer baseline.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#define UI_LAYOUT_LEFT_X 6
#define UI_LAYOUT_MENU_LOCK_X ( UI_LAYOUT_LEFT_X + 12 )
/* Fixed lock-icon slot keeps every row label aligned, locked or not. */
#define UI_LAYOUT_LABEL_X ( UI_LAYOUT_MENU_LOCK_X + 16 )
#define UI_LAYOUT_TITLE_X UI_LAYOUT_LEFT_X
#define UI_LAYOUT_TITLE_Y 5
#define UI_LAYOUT_MENU_START_Y 38
#define UI_LAYOUT_MENU_ROW_HEIGHT 20
#define UI_LAYOUT_MENU_VISIBLE_ROWS 6U
#define UI_LAYOUT_FOOTER_Y 150
/** Shared instrument panel origin and text insets (see GPS and Thermal). */
#define UI_LAYOUT_PANEL_TOP_Y 24
#define UI_LAYOUT_PANEL_LEFT_X 4
#define UI_LAYOUT_PANEL_RIGHT_X ( RFSCOUT_DISPLAY_WIDTH - 4 )
#define UI_LAYOUT_PANEL_GUTTER 4
#define UI_LAYOUT_PANEL_BOTTOM_Y ( RFSCOUT_DISPLAY_HEIGHT - 6 )
#define UI_LAYOUT_PANEL_TEXT_INSET_X 6
#define UI_LAYOUT_PANEL_TITLE_INSET_Y 4

/** @brief Return the first item for a standard scrolling menu viewport. */
static inline unsigned ui_layout_menu_window_start( unsigned a_selected, unsigned a_item_count )
{
    if( ( a_item_count <= UI_LAYOUT_MENU_VISIBLE_ROWS ) ||
        ( a_selected < UI_LAYOUT_MENU_VISIBLE_ROWS ) )
    {
        return 0U;
    }

    unsigned start = a_selected - UI_LAYOUT_MENU_VISIBLE_ROWS + 1U;
    const unsigned max_start = a_item_count - UI_LAYOUT_MENU_VISIBLE_ROWS;
    if( start > max_start )
    {
        start = max_start;
    }
    return start;
}
