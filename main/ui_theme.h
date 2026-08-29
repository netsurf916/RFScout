/**
    ui_theme.h : RFScout semantic display theme interface
    Description: Defines persisted display themes and semantic RGB565 color roles shared by all views.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_UI_THEME_H_
#define _RFSCOUT_UI_THEME_H_

#include <stdint.h>

typedef enum
{
    RFSCOUT_THEME_INDOOR = 0,
    RFSCOUT_THEME_OUTDOOR,
    RFSCOUT_THEME_NIGHT,
    RFSCOUT_THEME_COUNT
} rfscout_theme_id_t;

typedef struct
{
    uint16_t background;
    uint16_t text;
    uint16_t secondary;
    uint16_t divider;
    uint16_t accent;
    uint16_t selected;
    uint16_t info;
    uint16_t success;
    uint16_t warning;
    uint16_t error;
    uint16_t graph_primary;
    uint16_t graph_peak;
    uint16_t logo_primary;
    uint16_t logo_secondary;
} ui_theme_palette_t;

/** @brief Return the palette for one theme, falling back to Indoor for invalid IDs. */
const ui_theme_palette_t *ui_theme_palette( rfscout_theme_id_t a_theme );

/** @brief Return the palette selected by Device configuration. */
const ui_theme_palette_t *ui_theme_get( void );

/** @brief Return the stable configuration token for one theme. */
const char *ui_theme_config_name( rfscout_theme_id_t a_theme );

/** @brief Return the compact user-facing name for one theme. */
const char *ui_theme_display_name( rfscout_theme_id_t a_theme );

/** @brief Parse a stable configuration token, case-insensitively. */
bool ui_theme_from_config_name( const char *a_name, rfscout_theme_id_t *a_theme );

#endif // _RFSCOUT_UI_THEME_H_
