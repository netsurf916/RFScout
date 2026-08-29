/**
    ui_theme.c : RFScout semantic theme implementation
    Description: Supplies visual palettes and environmental sound policy to every application.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui_theme.h"

#include "event.h"

#include "esp_log.h"
#include "nvs.h"

#include <stddef.h>
#include <strings.h>

#define THEME_NVS_NAMESPACE "rfscout_ui"
#define THEME_NVS_KEY "theme"
#define THEME_NVS_VALUE_CAPACITY 16U

static const char *TAG = "ui_theme";
static rfscout_theme_id_t active_theme = RFSCOUT_THEME_INDOOR;

#define PALETTE_INDOOR                                                                             \
    {                                                                                              \
        .background = 0x0000U, .text = 0xFFFFU, .secondary = 0xC618U, .divider = 0x8410U,          \
        .accent = 0xFC00U, .selected = 0xFC00U, .info = 0xA514U, .normal = 0xFFFFU,                \
        .success = 0xC618U, .warning = 0xFFE1U, .error = 0xF800U, .graph_primary = 0xFC00U,        \
        .graph_peak = 0xFFFFU, .logo_primary = 0xFC00U, .logo_secondary = 0xFFFFU                  \
    }

#define PALETTE_NIGHT                                                                              \
    {                                                                                              \
        .background = 0x0000U, .text = 0xFBE0U, .secondary = 0xFA20U, .divider = 0x7800U,          \
        .accent = 0xF840U, .selected = 0xFC40U, .info = 0xFA20U, .normal = 0xFBE0U,                \
        .success = 0xFA40U, .warning = 0xFFE1U, .error = 0xF800U, .graph_primary = 0xF840U,        \
        .graph_peak = 0xFC40U, .logo_primary = 0xF840U, .logo_secondary = 0xFC40U                  \
    }

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
    /* Discipline is deliberately pixel-for-pixel identical to Night. Its only
       additional behavior is the sound policy below, making it both light and
       noise discipline rather than a separate visual aesthetic. */
    [RFSCOUT_THEME_DISCIPLINE] = PALETTE_NIGHT,
};

static const ui_theme_sound_policy_t sound_policies[ RFSCOUT_THEME_COUNT ] = {
    [RFSCOUT_THEME_INDOOR] =
        {
            .interaction_audio = true,
            .notification_audio = true,
            .interaction_volume_percent = 35U,
            .notification_volume_percent = 100U,
        },
    [RFSCOUT_THEME_OUTDOOR] =
        {
            .interaction_audio = true,
            .notification_audio = true,
            .interaction_volume_percent = 55U,
            .notification_volume_percent = 100U,
        },
    [RFSCOUT_THEME_NIGHT] =
        {
            .interaction_audio = true,
            .notification_audio = true,
            .interaction_volume_percent = 20U,
            .notification_volume_percent = 60U,
        },
    [RFSCOUT_THEME_DISCIPLINE] =
        {
            .interaction_audio = false,
            .notification_audio = false,
            .interaction_volume_percent = 0U,
            .notification_volume_percent = 0U,
        },
};

esp_err_t ui_theme_init( void )
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open( THEME_NVS_NAMESPACE, NVS_READONLY, &handle );
    if( ESP_ERR_NVS_NOT_FOUND == result )
    {
        active_theme = RFSCOUT_THEME_INDOOR;
        return ESP_OK;
    }
    if( ESP_OK != result )
    {
        return result;
    }

    char stored[ THEME_NVS_VALUE_CAPACITY ];
    size_t stored_length = sizeof( stored );
    result = nvs_get_str( handle, THEME_NVS_KEY, stored, &stored_length );
    nvs_close( handle );
    if( ESP_ERR_NVS_NOT_FOUND == result )
    {
        active_theme = RFSCOUT_THEME_INDOOR;
        return ESP_OK;
    }
    if( ESP_OK != result )
    {
        return result;
    }

    rfscout_theme_id_t parsed = RFSCOUT_THEME_INDOOR;
    if( !ui_theme_from_config_name( stored, &parsed ) )
    {
        ESP_LOGW( TAG, "invalid stored theme '%s'; using indoor", stored );
        active_theme = RFSCOUT_THEME_INDOOR;
        return ESP_OK;
    }

    active_theme = parsed;
    return ESP_OK;
}

esp_err_t ui_theme_set( rfscout_theme_id_t a_theme )
{
    if( a_theme >= RFSCOUT_THEME_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t result = nvs_open( THEME_NVS_NAMESPACE, NVS_READWRITE, &handle );
    if( ESP_OK != result )
    {
        return result;
    }

    result = nvs_set_str( handle, THEME_NVS_KEY, ui_theme_config_name( a_theme ) );
    if( ESP_OK == result )
    {
        result = nvs_commit( handle );
    }
    nvs_close( handle );

    if( ESP_OK == result )
    {
        active_theme = a_theme;
        ESP_LOGI( TAG, "theme=%s", ui_theme_config_name( a_theme ) );
        rfscout_event_emit(
            RFSCOUT_EVENT_THEME_CHANGED, RFSCOUT_EVENT_SOURCE_SYSTEM, (int32_t)a_theme );
    }
    return result;
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

const ui_theme_sound_policy_t *ui_theme_sound_policy( rfscout_theme_id_t a_theme )
{
    if( a_theme >= RFSCOUT_THEME_COUNT )
    {
        a_theme = RFSCOUT_THEME_INDOOR;
    }
    return &sound_policies[ a_theme ];
}

const ui_theme_sound_policy_t *ui_theme_sound_get( void )
{
    return ui_theme_sound_policy( active_theme );
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
        "indoor", "outdoor", "night", "discipline" };
    return ( a_theme < RFSCOUT_THEME_COUNT ) ? names[ a_theme ] : names[ RFSCOUT_THEME_INDOOR ];
}

const char *ui_theme_display_name( rfscout_theme_id_t a_theme )
{
    static const char *names[ RFSCOUT_THEME_COUNT ] = {
        "Indoor", "Outdoor", "Night", "Discipline" };
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
