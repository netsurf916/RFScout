/**
    ui_theme.h : RFScout semantic theme interface
    Description: Defines persisted visual themes independently from device audio policy.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_UI_THEME_H_
#define _RFSCOUT_UI_THEME_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Stable persisted theme identifiers; append new themes to preserve existing IDs. */
typedef enum
{
    RFSCOUT_THEME_INDOOR = 0,
    RFSCOUT_THEME_OUTDOOR,
    RFSCOUT_THEME_NIGHT,
    RFSCOUT_THEME_PIXEL_QUEST,
    RFSCOUT_THEME_COUNT
} rfscout_theme_id_t;

/** @brief Independently selected presentation geometry. */
typedef enum
{
    RFSCOUT_STYLE_SIMPLE = 0,
    RFSCOUT_STYLE_PIXEL_QUEST,
    RFSCOUT_STYLE_COUNT
} rfscout_style_id_t;

/** @brief Independently selected semantic palette. */
typedef enum
{
    RFSCOUT_COLORS_DEFAULT = 0,
    RFSCOUT_COLORS_OUTDOOR,
    RFSCOUT_COLORS_NIGHT,
    RFSCOUT_COLORS_COUNT
} rfscout_colors_id_t;

/** @brief Semantic RGB565 colors shared by all application views. */
typedef struct
{
    uint16_t background;
    uint16_t text;
    uint16_t secondary;
    uint16_t divider;
    uint16_t accent;
    uint16_t selected;
    uint16_t info;
    uint16_t normal;
    uint16_t success;
    uint16_t warning;
    uint16_t error;
    uint16_t graph_primary;
    uint16_t graph_peak;
    uint16_t logo_primary;
    uint16_t logo_secondary;
    uint16_t surface;
    uint16_t shadow;
} ui_theme_palette_t;

/** Theme-resolved foreground and background pair; reverse for inverse video. */
typedef struct
{
    uint16_t fg;
    uint16_t bg;
} ui_color_pair_t;

typedef enum
{
    UI_COLOR_PAIR_NORMAL = 0,
    UI_COLOR_PAIR_SECONDARY,
    UI_COLOR_PAIR_SURFACE,
    UI_COLOR_PAIR_SELECTED,
    UI_COLOR_PAIR_WARNING,
    UI_COLOR_PAIR_ERROR
} ui_color_pair_id_t;

ui_color_pair_t ui_theme_pair( ui_color_pair_id_t a_pair );
static inline ui_color_pair_t ui_color_pair_reverse( ui_color_pair_t a_pair )
{
    return (ui_color_pair_t){ .fg = a_pair.bg, .bg = a_pair.fg };
}

/** @brief Load the persisted theme preference, defaulting to Pixel Quest for fresh configurations. */
esp_err_t ui_theme_init( void );
/** @brief Persist and activate one visual theme immediately. */
esp_err_t ui_theme_set( rfscout_theme_id_t a_theme );
/** @brief Return the active visual theme identifier. */
rfscout_theme_id_t ui_theme_current( void );
/** @brief Return the palette for one theme, falling back to Indoor for invalid IDs. */
const ui_theme_palette_t *ui_theme_palette( rfscout_theme_id_t a_theme );
/** @brief Return the active semantic palette. */
const ui_theme_palette_t *ui_theme_get( void );
rfscout_style_id_t ui_style_current( void );
rfscout_colors_id_t ui_colors_current( void );
esp_err_t ui_style_set( rfscout_style_id_t a_style );
esp_err_t ui_colors_set( rfscout_colors_id_t a_colors );
const char *ui_style_display_name( rfscout_style_id_t a_style );
const char *ui_colors_display_name( rfscout_colors_id_t a_colors );
const char *ui_style_config_name( rfscout_style_id_t a_style );
const char *ui_colors_config_name( rfscout_colors_id_t a_colors );
bool ui_style_from_config_name( const char *a_name, rfscout_style_id_t *a_style );
bool ui_colors_from_config_name( const char *a_name, rfscout_colors_id_t *a_colors );
const char *ui_theme_config_name( rfscout_theme_id_t a_theme );
const char *ui_theme_display_name( rfscout_theme_id_t a_theme );
bool ui_theme_from_config_name( const char *a_name, rfscout_theme_id_t *a_theme );

#endif // _RFSCOUT_UI_THEME_H_
