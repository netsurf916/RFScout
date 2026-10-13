/**
    ui_game.h : RFScout pixel-console presentation primitives
    Description: Original fixed-size pixel art and optional Pixel Quest widgets.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once
#ifndef _RFSCOUT_UI_GAME_H_
#define _RFSCOUT_UI_GAME_H_

#include <stdbool.h>
#include <stdint.h>

/** @brief Original eight-pixel silhouettes used by console-style application chrome. */
typedef enum
{
    UI_GAME_ICON_SCOUT = 0,
    UI_GAME_ICON_RADIO,
    UI_GAME_ICON_NETWORK,
    UI_GAME_ICON_TAPE,
    UI_GAME_ICON_THERMAL,
    UI_GAME_ICON_AUDIO,
    UI_GAME_ICON_COMPASS,
    UI_GAME_ICON_DRIVE,
    UI_GAME_ICON_EVENTS,
    UI_GAME_ICON_SETTINGS,
    UI_GAME_ICON_STAR,
    UI_GAME_ICON_COUNT
} ui_game_icon_t;

/** @brief Return whether Pixel Quest presentation is active. */
bool ui_game_active( void );
/** @brief Draw one original 8x8 icon at integer scale; invalid icons/scales draw nothing. */
void ui_game_draw_icon(
    ui_game_icon_t a_icon, int a_x, int a_y, unsigned a_scale, uint16_t a_color );
/** @brief Choose an app icon from a borrowed, non-retained chrome title. */
ui_game_icon_t ui_game_icon_for_title( const char *a_title );
/** @brief Draw a filled double-border panel entirely inside the supplied rectangle. */
void ui_game_draw_panel( int a_x, int a_y, int a_width, int a_height, uint16_t a_border );
/** @brief Draw a selection/edit marker and console row band, or the ordinary text marker.
 * @param a_marker Borrowed existing menu marker (">", "*", ">*", or " "); never retained.
 */
void ui_game_draw_menu_marker( int a_x, int a_y, const char *a_marker, uint16_t a_color );
/** @brief Draw a proportional meter with pixel gaps in Pixel Quest, continuous in other themes.
 * @param a_value Current value, clamped to a_maximum; zero maximum draws an empty meter.
 */
void ui_game_draw_meter( int a_x,
                         int a_y,
                         int a_width,
                         int a_height,
                         uint32_t a_value,
                         uint32_t a_maximum,
                         uint16_t a_color );
/** @brief Draw a graph column with pixel gaps in Pixel Quest, continuous in other themes. */
void ui_game_draw_column( int a_x, int a_y, int a_width, int a_height, uint16_t a_color );
/** @brief Draw the Pixel Quest Home title screen below the global status region. */
void ui_game_draw_home( void );

#endif // _RFSCOUT_UI_GAME_H_
