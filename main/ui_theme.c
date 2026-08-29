/**
    ui_theme.c : RFScout semantic display theme implementation
    Description: Supplies high-visibility indoor, outdoor, and night palettes to every application view.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui_theme.h"

#include "device_config.h"

#include <stddef.h>
#include <strings.h>

static const ui_theme_palette_t palettes[ RFSCOUT_THEME_COUNT ] = {
    [ RFSCOUT_THEME_INDOOR ] = {
        .background = 0x0000U,
        .text = 0xFFFFU,
        .secondary = 0xC618U,
        .divider = 0x8410U,
        .accent = 0xFC00U,
        .selected = 0xFFE0U,
        .info = 0x07FFU,
        .success = 0x07E0U,
        .warning = 0xFFE0U,
        .error = 0xF800U,
        .graph_primary = 0x07E0U,
        .graph_peak = 0x07FFU,
        .logo_primary = 0xFC00U,
        .logo_secondary = 0xFFFFU,
    },
    [ RFSCOUT_THEME_OUTDOOR ] = {
        .background = 0x0000U,
        .text = 0xFFFFU,
        .secondary = 0xFFFFU,
        .divider = 0xC618U,
        .accent = 0xFFE0U,
        .selected = 0xFC00U,
        .info = 0x07FFU,
        .success = 0x07E0U,
        .warning = 0xFFE0U,
        .error = 0xF81FU,
        .graph_primary = 0xFFE0U,
        .graph_peak = 0x07FFU,
        .logo_primary = 0xFFE0U,
        .logo_secondary = 0xFFFFU,
    },
    [ RFSCOUT_THEME_NIGHT ] = {
        .background = 0x0000U,
        .text = 0xFBE0U,
        .secondary = 0xFA20U,
        .divider = 0x7800U,
        .accent = 0xF800U,
        .selected = 0xFC00U,
        .info = 0xFA20U,
        .success = 0xF800U,
        .warning = 0xFC00U,
        .error = 0xFFE0U,
        .graph_primary = 0xF800U,
        .graph_peak = 0xFC00U,
        .logo_primary = 0xF800U,
        .logo_secondary = 0xFC00U,
    },
};

const ui_theme_palette_t *ui_theme_palette( rfscout_theme_id_t a_theme )
{
    if( a_theme >= RFSCOUT_THEME_COUNT )
    {
        a_theme = RFSCOUT_THEME_INDOOR;
    }
    return &palettes[ a_theme ];
}

const ui_theme_palette_t *ui_theme_get( void )
{
    const device_config_t *config = device_config_get();
    return ui_theme_palette( ( NULL != config ) ? config->display_theme : RFSCOUT_THEME_INDOOR );
}

const char *ui_theme_config_name( rfscout_theme_id_t a_theme )
{
    static const char *names[ RFSCOUT_THEME_COUNT ] = { "indoor", "outdoor", "night" };
    return ( a_theme < RFSCOUT_THEME_COUNT ) ? names[ a_theme ] : names[ RFSCOUT_THEME_INDOOR ];
}

const char *ui_theme_display_name( rfscout_theme_id_t a_theme )
{
    static const char *names[ RFSCOUT_THEME_COUNT ] = { "Indoor", "Outdoor", "Night" };
    return ( a_theme < RFSCOUT_THEME_COUNT ) ? names[ a_theme ] : names[ RFSCOUT_THEME_INDOOR ];
}

bool ui_theme_from_config_name( const char *a_name, rfscout_theme_id_t *a_theme )
{
    if( ( NULL == a_name ) || ( NULL == a_theme ) )
    {
        return false;
    }

    for( unsigned index = 0U; index < RFSCOUT_THEME_COUNT; ++index )
    {
        if( 0 == strcasecmp( a_name, ui_theme_config_name( (rfscout_theme_id_t)index ) ) )
        {
            *a_theme = (rfscout_theme_id_t)index;
            return true;
        }
    }
    return false;
}
