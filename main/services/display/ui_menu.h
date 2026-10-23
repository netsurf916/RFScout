/**
    ui_menu.h : Shared menu state and semantic color interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_UI_MENU_H_
#define _RFSCOUT_UI_MENU_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/** @brief Independent menu navigation and availability properties. */
typedef struct
{
    bool selected;
    bool editing;
    bool available;
    bool locked;
} ui_menu_state_t;
/** @brief Resolve foreground for a menu label; unavailable takes precedence over focus. */
uint16_t ui_menu_label_color( ui_menu_state_t a_state );
/** @brief Resolve foreground for the row's value without encoding ON/OFF as availability. */
uint16_t ui_menu_value_color( ui_menu_state_t a_state );
/** @brief Render focus/edit marker and lock in a reserved column before UI_LAYOUT_LABEL_X. */
void ui_menu_draw_marker( int a_x, int a_y, ui_menu_state_t a_state );

/** @brief One generic label/value menu row; the list helper sets `selected`. */
typedef struct
{
    const char *label;
    const char *value;
    ui_menu_state_t state;
} ui_menu_row_t;

/** @brief First visible item of a scrolling list that keeps a_selected on screen. */
unsigned ui_menu_window_start( unsigned a_selected, unsigned a_count, unsigned a_visible );

/** @brief Draw marker, label and optional value (at UI_LAYOUT_MENU_VALUE_X) for one row. */
void ui_menu_draw_row( int a_y, const char *a_label, const char *a_value, ui_menu_state_t a_state );

/**
 * @brief Draw a scrolling window of rows starting at a_start_y.
 * @return The first visible item, for callers that draw extra per-row content.
 */
unsigned ui_menu_draw_rows( const ui_menu_row_t *a_rows,
                            size_t a_count,
                            unsigned a_selected,
                            unsigned a_visible,
                            int a_start_y );
#endif // _RFSCOUT_UI_MENU_H_
