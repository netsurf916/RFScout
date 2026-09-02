/**
    ui_theme.c : RFScout semantic theme implementation
    Description: Supplies visual palettes independently from device audio policy.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "ui_theme.h"

#include "device_config.h"
#include "event.h"
#include "storage.h"

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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
};

static void legacy_audio_gains_for_theme( rfscout_theme_id_t a_theme,
                                          uint8_t *a_interaction_percent,
                                          uint8_t *a_notification_percent )
{
    uint8_t interaction = 35U;
    uint8_t notification = 100U;
    if( RFSCOUT_THEME_OUTDOOR == a_theme )
    {
        interaction = 55U;
    }
    else if( RFSCOUT_THEME_NIGHT == a_theme )
    {
        interaction = 20U;
        notification = 60U;
    }
    *a_interaction_percent = interaction;
    *a_notification_percent = notification;
}

static bool legacy_theme_read( rfscout_theme_id_t *a_theme, bool *a_quiet )
{
    if( ( NULL == a_theme ) || ( NULL == a_quiet ) )
    {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t result = nvs_open( THEME_NVS_NAMESPACE, NVS_READONLY, &handle );
    if( ESP_OK != result )
    {
        return false;
    }

    char stored[ THEME_NVS_VALUE_CAPACITY ];
    size_t stored_length = sizeof( stored );
    result = nvs_get_str( handle, THEME_NVS_KEY, stored, &stored_length );
    nvs_close( handle );
    if( ESP_OK != result )
    {
        return false;
    }

    *a_quiet = false;
    if( 0 == strcasecmp( stored, "discipline" ) )
    {
        *a_theme = RFSCOUT_THEME_NIGHT;
        *a_quiet = true;
        return true;
    }
    return ui_theme_from_config_name( stored, a_theme );
}

static void legacy_theme_write( rfscout_theme_id_t a_theme )
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open( THEME_NVS_NAMESPACE, NVS_READWRITE, &handle );
    if( ESP_OK != result )
    {
        return;
    }
    result = nvs_set_str( handle, THEME_NVS_KEY, ui_theme_config_name( a_theme ) );
    if( ESP_OK == result )
    {
        (void)nvs_commit( handle );
    }
    nvs_close( handle );
}

static bool config_needs_legacy_theme( void )
{
    FILE *file = fopen( RFSCOUT_CONFIG_PATH, "rb" );
    if( NULL == file )
    {
        return false;
    }
    if( 0 != fseek( file, 0, SEEK_END ) )
    {
        fclose( file );
        return false;
    }
    const long length = ftell( file );
    if( ( length <= 0L ) || ( length > 16384L ) || ( 0 != fseek( file, 0, SEEK_SET ) ) )
    {
        fclose( file );
        return false;
    }

    char *buffer = calloc( (size_t)length + 1U, 1U );
    if( NULL == buffer )
    {
        fclose( file );
        return false;
    }
    const bool read_ok = ( (size_t)length == fread( buffer, 1U, (size_t)length, file ) );
    fclose( file );
    if( !read_ok )
    {
        free( buffer );
        return false;
    }

    cJSON *root = cJSON_Parse( buffer );
    free( buffer );
    if( NULL == root )
    {
        return false;
    }

    const cJSON *version = cJSON_GetObjectItemCaseSensitive( root, "version" );
    const cJSON *display = cJSON_GetObjectItemCaseSensitive( root, "display" );
    const cJSON *theme =
        cJSON_IsObject( display ) ? cJSON_GetObjectItemCaseSensitive( display, "theme" ) : NULL;
    const bool needs_legacy =
        cJSON_IsNumber( version ) && ( 1 == version->valueint ) && !cJSON_IsString( theme );
    cJSON_Delete( root );
    return needs_legacy;
}

esp_err_t ui_theme_init( void )
{
    active_theme = device_config_theme();

    const bool use_legacy = !storage_is_available() || config_needs_legacy_theme();
    rfscout_theme_id_t legacy_theme = RFSCOUT_THEME_INDOOR;
    bool legacy_quiet = false;
    if( use_legacy && legacy_theme_read( &legacy_theme, &legacy_quiet ) )
    {
        uint8_t interaction_gain = 35U;
        uint8_t notification_gain = 100U;
        legacy_audio_gains_for_theme( legacy_theme, &interaction_gain, &notification_gain );

        (void)device_config_set_theme( legacy_theme );
        (void)device_config_set_audio_policy( legacy_quiet ? RFSCOUT_AUDIO_POLICY_QUIET
                                                           : RFSCOUT_AUDIO_POLICY_NORMAL );
        (void)device_config_set_audio_gains( interaction_gain, notification_gain );
        active_theme = legacy_theme;
        ESP_LOGI( TAG,
                  "migrated legacy theme=%s audio_policy=%s gains=%u/%u",
                  ui_theme_config_name( legacy_theme ),
                  legacy_quiet ? "quiet" : "normal",
                  (unsigned)interaction_gain,
                  (unsigned)notification_gain );

        if( storage_is_available() )
        {
            const esp_err_t save_result = device_config_save( false, 0, NULL );
            if( ESP_OK != save_result )
            {
                ESP_LOGW(
                    TAG, "legacy theme migration save failed: %s", esp_err_to_name( save_result ) );
            }
        }
    }
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
    legacy_theme_write( a_theme );
    ESP_LOGI( TAG, "theme=%s", ui_theme_config_name( a_theme ) );
    rfscout_event_emit(
        RFSCOUT_EVENT_THEME_CHANGED, RFSCOUT_EVENT_SOURCE_SYSTEM, (int32_t)a_theme );
    const esp_err_t save_result = device_config_save( false, 0, NULL );
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
