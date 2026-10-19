/**
    ui_menu.h : Shared menu state and semantic color interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_UI_MENU_H_
#define _RFSCOUT_UI_MENU_H_
#include <stdbool.h>
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
/** @brief Render shared focus/edit marker and optional locked icon. */
void ui_menu_draw_marker( int a_x, int a_y, ui_menu_state_t a_state );
#endif // _RFSCOUT_UI_MENU_H_
