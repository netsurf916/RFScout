/**
    ui_theme.c : RFScout semantic theme implementation
    Description: Supplies visual palettes independently from device audio policy.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui_theme.h"

#include "device_config.h"
#include "event.h"

#include "esp_log.h"

#include <strings.h>

static const char *TAG = "ui_theme";
static rfscout_theme_id_t active_theme = RFSCOUT_THEME_PIXEL_QUEST;
static rfscout_style_id_t active_style = RFSCOUT_STYLE_PIXEL_QUEST;
static rfscout_colors_id_t active_colors = RFSCOUT_COLORS_DEFAULT;

static const ui_theme_palette_t palettes[ RFSCOUT_THEME_COUNT ] = {
    [RFSCOUT_THEME_INDOOR] =
        {
            .background = 0x0000U,
            .text = 0xFFFFU,
            .secondary = 0xC618U,
            .divider = 0x8410U,
            .accent = 0xFC00U,
            .selected = 0xFC00U,
            .info = 0xA514U,
            .normal = 0xFFFFU,
            .success = 0xC618U,
            .warning = 0xFFE1U,
            .error = 0xF800U,
            .graph_primary = 0xFC00U,
            .graph_peak = 0xFFFFU,
            .logo_primary = 0xFC00U,
            .logo_secondary = 0xFFFFU,
            .surface = 0x1082U,
            .shadow = 0x0000U,
        },
    [RFSCOUT_THEME_OUTDOOR] =
        {
            .background = 0x0000U,
            .text = 0xFFFFU,
            .secondary = 0xFFFFU,
            .divider = 0xA75FU,
            .accent = 0x07FFU,
            .selected = 0xFFE0U,
            .info = 0xB7FFU,
            .normal = 0xFFFFU,
            .success = 0xFFFFU,
            .warning = 0xFFE1U,
            .error = 0xF800U,
            .graph_primary = 0x07FFU,
            .graph_peak = 0xFFFFU,
            .logo_primary = 0x07FFU,
            .logo_secondary = 0xFFFFU,
            .surface = 0x0843U,
            .shadow = 0x0000U,
        },
    [RFSCOUT_THEME_NIGHT] =
        {
            .background = 0x0000U,
            .text = 0xFBE0U,
            .secondary = 0xFA20U,
            .divider = 0x7800U,
            .accent = 0xF840U,
            .selected = 0xFC40U,
            .info = 0xFA20U,
            .normal = 0xFBE0U,
            .success = 0xFA40U,
            .warning = 0xFC40U,
            .error = 0xF800U,
            .graph_primary = 0xF840U,
            .graph_peak = 0xFC40U,
            .logo_primary = 0xF840U,
            .logo_secondary = 0xFC40U,
            .surface = 0x2000U,
            .shadow = 0x0000U,
        },
    [RFSCOUT_THEME_PIXEL_QUEST] =
        {
            .background = 0x0843U,
            .text = 0xEFFFU,
            .secondary = 0x8CD3U,
            .divider = 0x52D0U,
            .accent = 0x3E7DU,
            .selected = 0xFD6AU,
            .info = 0x6F3FU,
            .normal = 0xEFFFU,
            .success = 0x87B0U,
            .warning = 0xFD6AU,
            .error = 0xFA09U,
            .graph_primary = 0x3E7DU,
            .graph_peak = 0xFD6AU,
            .logo_primary = 0x3E7DU,
            .logo_secondary = 0xFD6AU,
            .surface = 0x10C6U,
            .shadow = 0x0001U,
        },
};

esp_err_t ui_theme_init( void )
{
    active_theme = device_config_theme();
    active_style = device_config_style();
    active_colors = device_config_colors();
    return ESP_OK;
}

esp_err_t ui_theme_set( rfscout_theme_id_t a_theme )
{
    if( a_theme >= RFSCOUT_THEME_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = device_config_set_theme( a_theme );
    if( ESP_OK != result )
    {
        return result;
    }
    active_theme = a_theme;
    active_style =
        a_theme == RFSCOUT_THEME_PIXEL_QUEST ? RFSCOUT_STYLE_PIXEL_QUEST : RFSCOUT_STYLE_SIMPLE;
    active_colors = a_theme == RFSCOUT_THEME_NIGHT     ? RFSCOUT_COLORS_NIGHT
                    : a_theme == RFSCOUT_THEME_OUTDOOR ? RFSCOUT_COLORS_OUTDOOR
                                                       : RFSCOUT_COLORS_DEFAULT;
    (void)device_config_set_style( active_style );
    (void)device_config_set_colors( active_colors );
    ESP_LOGI( TAG, "theme=%s", ui_theme_config_name( a_theme ) );
    rfscout_event_emit(
        RFSCOUT_EVENT_THEME_CHANGED, RFSCOUT_EVENT_SOURCE_SYSTEM, (int32_t)a_theme );
    const esp_err_t save_result = device_config_save();
    if( ESP_ERR_INVALID_STATE == save_result )
    {
        return ESP_OK;
    }
    if( ESP_OK != save_result )
    {
        ESP_LOGW( TAG, "theme save failed: %s", esp_err_to_name( save_result ) );
        return save_result;
    }
    return ESP_OK;
}

rfscout_theme_id_t ui_theme_current( void )
{
    return active_theme;
}

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
    return ui_theme_palette( active_colors == RFSCOUT_COLORS_NIGHT ? RFSCOUT_THEME_NIGHT
                             : active_colors == RFSCOUT_COLORS_OUTDOOR
                                 ? RFSCOUT_THEME_OUTDOOR
                                 : RFSCOUT_THEME_PIXEL_QUEST );
}

rfscout_style_id_t ui_style_current( void )
{
    return active_style;
}

rfscout_colors_id_t ui_colors_current( void )
{
    return active_colors;
}

ui_color_pair_t ui_theme_pair( ui_color_pair_id_t a_pair )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    switch( a_pair )
    {
        case UI_COLOR_PAIR_SECONDARY:
            return (ui_color_pair_t){ .fg = theme->secondary, .bg = theme->background };
        case UI_COLOR_PAIR_SURFACE:
            return (ui_color_pair_t){ .fg = theme->text, .bg = theme->surface };
        case UI_COLOR_PAIR_SELECTED:
            return (ui_color_pair_t){ .fg = theme->background, .bg = theme->selected };
        case UI_COLOR_PAIR_WARNING:
            return (ui_color_pair_t){ .fg = theme->warning, .bg = theme->background };
        case UI_COLOR_PAIR_ERROR:
            return (ui_color_pair_t){ .fg = theme->error, .bg = theme->background };
        case UI_COLOR_PAIR_NORMAL:
        default:
            return (ui_color_pair_t){ .fg = theme->text, .bg = theme->background };
    }
}

static esp_err_t persist_visual_preference( void )
{
    rfscout_event_emit( RFSCOUT_EVENT_THEME_CHANGED, RFSCOUT_EVENT_SOURCE_SYSTEM, 0 );
    const esp_err_t result = device_config_save();
    return result == ESP_ERR_INVALID_STATE ? ESP_OK : result;
}

esp_err_t ui_style_set( rfscout_style_id_t a_style )
{
    if( a_style >= RFSCOUT_STYLE_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = device_config_set_style( a_style );
    if( result != ESP_OK )
    {
        return result;
    }
    active_style = a_style;
    return persist_visual_preference();
}

esp_err_t ui_colors_set( rfscout_colors_id_t a_colors )
{
    if( a_colors >= RFSCOUT_COLORS_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = device_config_set_colors( a_colors );
    if( result != ESP_OK )
    {
        return result;
    }
    active_colors = a_colors;
    return persist_visual_preference();
}

const char *ui_style_config_name( rfscout_style_id_t a_style )
{
    return a_style == RFSCOUT_STYLE_PIXEL_QUEST ? "pixel_quest" : "simple";
}

const char *ui_colors_config_name( rfscout_colors_id_t a_colors )
{
    static const char *names[] = { "default", "outdoor", "night" };
    return a_colors < RFSCOUT_COLORS_COUNT ? names[ a_colors ] : names[ 0 ];
}

const char *ui_style_display_name( rfscout_style_id_t a_style )
{
    return a_style == RFSCOUT_STYLE_PIXEL_QUEST ? "Pixel Quest" : "Simple";
}

const char *ui_colors_display_name( rfscout_colors_id_t a_colors )
{
    static const char *names[] = { "Default", "Outdoor", "Night" };
    return a_colors < RFSCOUT_COLORS_COUNT ? names[ a_colors ] : names[ 0 ];
}

bool ui_style_from_config_name( const char *a_name, rfscout_style_id_t *a_style )
{
    if( NULL == a_name || NULL == a_style )
    {
        return false;
    }
    for( unsigned i = 0U; i < RFSCOUT_STYLE_COUNT; ++i )
    {
        if( 0 == strcasecmp( a_name, ui_style_config_name( (rfscout_style_id_t)i ) ) )
        {
            *a_style = (rfscout_style_id_t)i;
            return true;
        }
    }
    return false;
}

bool ui_colors_from_config_name( const char *a_name, rfscout_colors_id_t *a_colors )
{
    if( NULL == a_name || NULL == a_colors )
    {
        return false;
    }
    for( unsigned i = 0U; i < RFSCOUT_COLORS_COUNT; ++i )
    {
        if( 0 == strcasecmp( a_name, ui_colors_config_name( (rfscout_colors_id_t)i ) ) )
        {
            *a_colors = (rfscout_colors_id_t)i;
            return true;
        }
    }
    return false;
}

const char *ui_theme_config_name( rfscout_theme_id_t a_theme )
{
    static const char *names[ RFSCOUT_THEME_COUNT ] = {
        "indoor", "outdoor", "night", "pixel_quest" };
    return ( a_theme < RFSCOUT_THEME_COUNT ) ? names[ a_theme ] : names[ RFSCOUT_THEME_INDOOR ];
}

const char *ui_theme_display_name( rfscout_theme_id_t a_theme )
{
    static const char *names[ RFSCOUT_THEME_COUNT ] = {
        "Indoor", "Outdoor", "Night", "Pixel Quest" };
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
