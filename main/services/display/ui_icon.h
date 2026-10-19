/**
    ui_icon.h : Shared RFScout icon drawing interface
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_UI_ICON_H_
#define _RFSCOUT_UI_ICON_H_
#include "ui_game.h"
#include <stdbool.h>
#include <stdint.h>
/** @brief Draw existing original eight-pixel icon at integer scale. */
void ui_icon_draw_pixel(
    ui_game_icon_t a_icon, int a_x, int a_y, unsigned a_scale, uint16_t a_color );
/** @brief Draw the common 12x14 satellite icon. */
void ui_icon_draw_gps( int a_x, int a_y, uint16_t a_color );
/** @brief Draw the common 12x14 padlock; application lock bears an A. */
void ui_icon_draw_lock( int a_x, int a_y, bool a_application_lock, uint16_t a_color );
/** @brief Draw SD icon with optional error mark. */
void ui_icon_draw_sd( int a_x, int a_y, uint16_t a_color, bool a_error );
/** @brief Draw thermal status icon. */
void ui_icon_draw_thermal( int a_x, int a_y, uint16_t a_color );
#endif // _RFSCOUT_UI_ICON_H_
