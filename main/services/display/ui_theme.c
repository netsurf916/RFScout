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

#define PALETTE_INDOOR                                                                             \
    { .background = 0x0000U,                                                                       \
      .text = 0xFFFFU,                                                                             \
      .secondary = 0xC618U,                                                                        \
      .divider = 0x8410U,                                                                          \
      .accent = 0xFC00U,                                                                           \
      .selected = 0xFC00U,                                                                         \
      .info = 0xA514U,                                                                             \
      .normal = 0xFFFFU,                                                                           \
      .success = 0xC618U,                                                                          \
      .warning = 0xFFE1U,                                                                          \
      .error = 0xF800U,                                                                            \
      .graph_primary = 0xFC00U,                                                                    \
      .graph_peak = 0xFFFFU,                                                                       \
      .logo_primary = 0xFC00U,                                                                     \
      .logo_secondary = 0xFFFFU }

#define PALETTE_NIGHT                                                                              \
    { .background = 0x0000U,                                                                       \
      .text = 0xFBE0U,                                                                             \
      .secondary = 0xFA20U,                                                                        \
      .divider = 0x7800U,                                                                          \
      .accent = 0xF840U,                                                                           \
      .selected = 0xFC40U,                                                                         \
      .info = 0xFA20U,                                                                             \
      .normal = 0xFBE0U,                                                                           \
      .success = 0xFA40U,                                                                          \
      .warning = 0xFFE1U,                                                                          \
      .error = 0xF800U,                                                                            \
      .graph_primary = 0xF840U,                                                                    \
      .graph_peak = 0xFC40U,                                                                       \
      .logo_primary = 0xF840U,                                                                     \
      .logo_secondary = 0xFC40U }

static const ui_theme_palette_t palettes[ RFSCOUT_THEME_COUNT ] = {
    [RFSCOUT_THEME_INDOOR] = PALETTE_INDOOR,
    [RFSCOUT_THEME_OUTDOOR] =
        {
            .background = 0x0000U,
            .text = 0xFFFFU,
            .secondary = 0xFFFFU,
            .divider = 0xC618U,
            .accent = 0xFC00U,
            .selected = 0xFC00U,
            .info = 0xC618U,
            .normal = 0xFFFFU,
            .success = 0xFFFFU,
            .warning = 0xFFE1U,
            .error = 0xF800U,
            .graph_primary = 0xFC00U,
            .graph_peak = 0xFFFFU,
            .logo_primary = 0xFC00U,
            .logo_secondary = 0xFFFFU,
        },
    [RFSCOUT_THEME_NIGHT] = PALETTE_NIGHT,
    [RFSCOUT_THEME_PIXEL_QUEST] =
        {
            .background = 0x0865U,
            .text = 0xF73BU,
            .secondary = 0xADDDU,
            .divider = 0x53B3U,
            .accent = 0xF9C9U,
            .selected = 0xFF0AU,
            .info = 0x55FFU,
            .normal = 0xF73BU,
            .success = 0x6E4FU,
            .warning = 0xFF0AU,
            .error = 0xFA09U,
            .graph_primary = 0x6E4FU,
            .graph_peak = 0xFF0AU,
            .logo_primary = 0xF9C9U,
            .logo_secondary = 0xFF0AU,
            .surface = 0x194BU,
            .shadow = 0x0021U,
        },
};

esp_err_t ui_theme_init( void )
{
    active_theme = device_config_theme();
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
    return ui_theme_palette( active_theme );
}

uint16_t ui_theme_resolve_legacy_color( uint16_t a_color )
{
    const ui_theme_palette_t *theme = ui_theme_get();
    switch( a_color )
    {
        case 0x0000U:
            return theme->background;
        case 0xFFFFU:
        case 0xBDF7U:
            return theme->normal;
        case 0x7BEFU:
        case 0x8410U:
        case 0x03F0U:
            return ( 0x8410U == a_color ) ? theme->divider : theme->secondary;
        case 0xFC00U:
            return theme->accent;
        case 0xFFE0U:
        case 0xFFE1U:
        case 0xFD20U:
            return theme->warning;
        case 0xF800U:
        case 0xF81FU:
            return theme->error;
        case 0x03E0U:
        case 0x07E0U:
            return theme->success;
        case 0x07FFU:
            return theme->info;
        default:
            return a_color;
    }
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
