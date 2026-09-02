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
    uint16_t normal;
    uint16_t success;
    uint16_t warning;
    uint16_t error;
    uint16_t graph_primary;
    uint16_t graph_peak;
    uint16_t logo_primary;
    uint16_t logo_secondary;
} ui_theme_palette_t;

/** @brief Load the persisted theme preference, defaulting to Indoor. */
esp_err_t ui_theme_init( void );
/** @brief Persist and activate one visual theme immediately. */
esp_err_t ui_theme_set( rfscout_theme_id_t a_theme );
/** @brief Return the active visual theme identifier. */
rfscout_theme_id_t ui_theme_current( void );
/** @brief Return the palette for one theme, falling back to Indoor for invalid IDs. */
const ui_theme_palette_t *ui_theme_palette( rfscout_theme_id_t a_theme );
/** @brief Return the active semantic palette. */
const ui_theme_palette_t *ui_theme_get( void );
uint16_t ui_theme_resolve_legacy_color( uint16_t a_color );
const char *ui_theme_config_name( rfscout_theme_id_t a_theme );
const char *ui_theme_display_name( rfscout_theme_id_t a_theme );
bool ui_theme_from_config_name( const char *a_name, rfscout_theme_id_t *a_theme );

#endif // _RFSCOUT_UI_THEME_H_
