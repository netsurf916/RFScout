/**
    device_config_recovery.c : RFScout configuration downgrade recovery
    Description: Restores validated newer-schema configuration quarantined by older firmware.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_config_recovery.h"

#include "alert.h"
#include "audio_policy.h"
#include "device_config.h"
#include "ui_theme.h"

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define CONFIG_BAD_PATH RFSCOUT_CONFIG_PATH ".bad"
#define CONFIG_BACKUP_PATH RFSCOUT_CONFIG_PATH ".bak"
#define CONFIG_DOWNGRADE_PATH RFSCOUT_CONFIG_PATH ".downgrade"
#define CONFIG_RECOVERY_PATH RFSCOUT_CONFIG_PATH ".recovering"
#define CONFIG_MAX_FILE_BYTES 16384L
#define CONFIG_DISPLAY_TIMEOUT_MAX_SECONDS 3600
#define CONFIG_MIN_VALID_EPOCH 1577836800LL
#define CONFIG_MAX_VALID_EPOCH 4133980799LL
#define LEGACY_THEME_NVS_NAMESPACE "rfscout_ui"
#define LEGACY_THEME_NVS_KEY "theme"
#define LEGACY_THEME_NVS_VALUE_CAPACITY 16U

static const char *TAG = "config_recovery";

typedef enum
{
    ACTIVE_CONFIG_MISSING = 0,
    ACTIVE_CONFIG_OLDER,
    ACTIVE_CONFIG_CURRENT,
    ACTIVE_CONFIG_INVALID,
} active_config_state_t;

static esp_err_t load_json_document( const char *a_path, cJSON **a_root )
{
    if( NULL == a_path || NULL == a_root )
    {
        return ESP_ERR_INVALID_ARG;
    }

    *a_root = NULL;
    FILE *file = fopen( a_path, "rb" );
    if( NULL == file )
    {
        return ( ENOENT == errno ) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    if( 0 != fseek( file, 0, SEEK_END ) )
    {
        fclose( file );
        return ESP_FAIL;
    }
    const long length = ftell( file );
    if( length <= 0L || length > CONFIG_MAX_FILE_BYTES || 0 != fseek( file, 0, SEEK_SET ) )
    {
        fclose( file );
        return ESP_ERR_INVALID_SIZE;
    }

    char *buffer = calloc( (size_t)length + 1U, 1U );
    if( NULL == buffer )
    {
        fclose( file );
        return ESP_ERR_NO_MEM;
    }
    const size_t read_count = fread( buffer, 1U, (size_t)length, file );
    fclose( file );
    if( read_count != (size_t)length )
    {
        free( buffer );
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse( buffer );
    free( buffer );
    if( !cJSON_IsObject( root ) )
    {
        cJSON_Delete( root );
        return ESP_ERR_INVALID_RESPONSE;
    }

    *a_root = root;
    return ESP_OK;
}

static esp_err_t save_json_document( const char *a_path, const cJSON *a_root )
{
    if( NULL == a_path || NULL == a_root )
    {
        return ESP_ERR_INVALID_ARG;
    }

    char *serialized = cJSON_PrintUnformatted( a_root );
    if( NULL == serialized )
    {
        return ESP_ERR_NO_MEM;
    }

    FILE *file = fopen( a_path, "wb" );
    if( NULL == file )
    {
        cJSON_free( serialized );
        return ESP_FAIL;
    }

    const size_t length = strlen( serialized );
    const bool written = fwrite( serialized, 1U, length, file ) == length;
    const bool flushed = written && 0 == fflush( file ) && 0 == fsync( fileno( file ) );
    const int close_result = fclose( file );
    cJSON_free( serialized );
    return ( flushed && 0 == close_result ) ? ESP_OK : ESP_FAIL;
}

static bool schema_version( const cJSON *a_root, int *a_version )
{
    if( NULL == a_root || NULL == a_version )
    {
        return false;
    }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive( a_root, "version" );
    if( !cJSON_IsNumber( version ) || version->valueint < 1 )
    {
        return false;
    }
    *a_version = version->valueint;
    return true;
}

static bool valid_schema_v3_candidate( const cJSON *a_root )
{
    int version = 0;
    if( !schema_version( a_root, &version ) || version != (int)RFSCOUT_CONFIG_SCHEMA_VERSION )
    {
        return false;
    }

    const cJSON *display = cJSON_GetObjectItemCaseSensitive( a_root, "display" );
    const cJSON *theme =
        cJSON_IsObject( display ) ? cJSON_GetObjectItemCaseSensitive( display, "theme" ) : NULL;
    rfscout_theme_id_t parsed_theme = RFSCOUT_THEME_COUNT;
    if( !cJSON_IsString( theme ) || NULL == theme->valuestring ||
        !ui_theme_from_config_name( theme->valuestring, &parsed_theme ) )
    {
        return false;
    }

    const cJSON *audio = cJSON_GetObjectItemCaseSensitive( a_root, "audio" );
    const cJSON *policy =
        cJSON_IsObject( audio ) ? cJSON_GetObjectItemCaseSensitive( audio, "policy" ) : NULL;
    const cJSON *interaction =
        cJSON_IsObject( audio )
            ? cJSON_GetObjectItemCaseSensitive( audio, "interaction_gain_percent" )
            : NULL;
    const cJSON *notification =
        cJSON_IsObject( audio )
            ? cJSON_GetObjectItemCaseSensitive( audio, "notification_gain_percent" )
            : NULL;
    rfscout_audio_policy_id_t parsed_policy = RFSCOUT_AUDIO_POLICY_COUNT;
    if( !cJSON_IsString( policy ) || NULL == policy->valuestring ||
        !audio_policy_from_config_name( policy->valuestring, &parsed_policy ) )
    {
        return false;
    }
    if( !cJSON_IsNumber( interaction ) || interaction->valueint < 0 ||
        interaction->valueint > 100 || !cJSON_IsNumber( notification ) ||
        notification->valueint < 0 || notification->valueint > 100 )
    {
        return false;
    }

    return true;
}

static esp_err_t load_active_config( cJSON **a_root, active_config_state_t *a_state )
{
    if( NULL == a_root || NULL == a_state )
    {
        return ESP_ERR_INVALID_ARG;
    }
    *a_root = NULL;

    const esp_err_t result = load_json_document( RFSCOUT_CONFIG_PATH, a_root );
    if( ESP_ERR_NOT_FOUND == result )
    {
        *a_state = ACTIVE_CONFIG_MISSING;
        return ESP_OK;
    }
    if( ESP_ERR_INVALID_SIZE == result || ESP_ERR_INVALID_RESPONSE == result )
    {
        *a_state = ACTIVE_CONFIG_INVALID;
        return ESP_OK;
    }
    if( ESP_OK != result )
    {
        return result;
    }

    int version = 0;
    if( !schema_version( *a_root, &version ) )
    {
        cJSON_Delete( *a_root );
        *a_root = NULL;
        *a_state = ACTIVE_CONFIG_INVALID;
        return ESP_OK;
    }
    *a_state = ( version < (int)RFSCOUT_CONFIG_SCHEMA_VERSION ) ? ACTIVE_CONFIG_OLDER
                                                                 : ACTIVE_CONFIG_CURRENT;
    return ESP_OK;
}

static bool string_equals_any( const char *a_value, const char *const *a_values, size_t a_count )
{
    if( NULL == a_value )
    {
        return false;
    }
    for( size_t index = 0U; index < a_count; ++index )
    {
        if( 0 == strcasecmp( a_value, a_values[ index ] ) )
        {
            return true;
        }
    }
    return false;
}

static bool app_name_valid( const char *a_name )
{
    static const char *const names[] = {
        "wifi_bt", "cc1101", "nrf24", "audio", "thermal", "device", "home", "recorder" };
    return string_equals_any( a_name, names, sizeof( names ) / sizeof( names[ 0 ] ) );
}

static bool power_action_valid( const cJSON *a_item, bool a_long_press, bool a_policy )
{
    if( !cJSON_IsString( a_item ) || NULL == a_item->valuestring )
    {
        return false;
    }
    static const char *const short_actions[] = { "none", "screen_off", "sleep", "power_off" };
    static const char *const long_actions[] = { "none", "power_off" };
    static const char *const policy_actions[] = { "none", "sleep", "power_off" };
    if( a_policy )
    {
        return string_equals_any( a_item->valuestring,
                                  policy_actions,
                                  sizeof( policy_actions ) / sizeof( policy_actions[ 0 ] ) );
    }
    if( a_long_press )
    {
        return string_equals_any( a_item->valuestring,
                                  long_actions,
                                  sizeof( long_actions ) / sizeof( long_actions[ 0 ] ) );
    }
    return string_equals_any( a_item->valuestring,
                              short_actions,
                              sizeof( short_actions ) / sizeof( short_actions[ 0 ] ) );
}

static bool timezone_entry_valid( const cJSON *a_entry )
{
    if( !cJSON_IsObject( a_entry ) )
    {
        return false;
    }
    const cJSON *name = cJSON_GetObjectItemCaseSensitive( a_entry, "name" );
    const cJSON *label = cJSON_GetObjectItemCaseSensitive( a_entry, "label" );
    const cJSON *posix = cJSON_GetObjectItemCaseSensitive( a_entry, "posix" );
    return cJSON_IsString( name ) && cJSON_IsString( label ) && cJSON_IsString( posix ) &&
           NULL != name->valuestring && NULL != label->valuestring && NULL != posix->valuestring &&
           '\0' != name->valuestring[ 0 ] && '\0' != posix->valuestring[ 0 ];
}

static bool timezone_options_valid( const cJSON *a_item )
{
    if( !cJSON_IsArray( a_item ) )
    {
        return false;
    }
    const cJSON *entry = NULL;
    cJSON_ArrayForEach( entry, a_item )
    {
        if( timezone_entry_valid( entry ) )
        {
            return true;
        }
    }
    return false;
}

static bool timezone_selection_in_options( const cJSON *a_options, const char *a_selected )
{
    if( !cJSON_IsArray( a_options ) || NULL == a_selected || '\0' == a_selected[ 0 ] )
    {
        return false;
    }
    size_t count = 0U;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach( entry, a_options )
    {
        if( count >= RFSCOUT_TIMEZONE_MAX )
        {
            break;
        }
        if( !timezone_entry_valid( entry ) )
        {
            continue;
        }
        const cJSON *name = cJSON_GetObjectItemCaseSensitive( entry, "name" );
        if( 0 == strcmp( name->valuestring, a_selected ) )
        {
            return true;
        }
        ++count;
    }
    return false;
}

static bool timezone_pair_mergeable( const cJSON *a_target, const cJSON *a_source )
{
    const cJSON *target_options = cJSON_GetObjectItemCaseSensitive( a_target, "options" );
    const cJSON *target_selected = cJSON_GetObjectItemCaseSensitive( a_target, "selected" );
    const cJSON *source_options = cJSON_GetObjectItemCaseSensitive( a_source, "options" );
    const cJSON *source_selected = cJSON_GetObjectItemCaseSensitive( a_source, "selected" );

    const cJSON *effective_options =
        timezone_options_valid( source_options ) ? source_options : target_options;
    const cJSON *effective_selected =
        cJSON_IsString( source_selected ) && NULL != source_selected->valuestring ? source_selected
                                                                                  : target_selected;
    return cJSON_IsString( effective_selected ) && NULL != effective_selected->valuestring &&
           timezone_selection_in_options( effective_options, effective_selected->valuestring );
}

static bool valid_morse_character( const cJSON *a_item )
{
    if( !cJSON_IsString( a_item ) || NULL == a_item->valuestring ||
        '\0' == a_item->valuestring[ 0 ] || '\0' != a_item->valuestring[ 1 ] )
    {
        return false;
    }
    const char character = (char)toupper( (unsigned char)a_item->valuestring[ 0 ] );
    return alert_morse_character_supported( character );
}

static bool legacy_item_valid( const char *a_path, const char *a_name, const cJSON *a_item )
{
    if( NULL == a_path || NULL == a_name || NULL == a_item )
    {
        return false;
    }

    if( '\0' == a_path[ 0 ] )
    {
        static const char *const objects[] = { "display",
                                               "power",
                                               "apps",
                                               "logging",
                                               "notifications",
                                               "recorder",
                                               "timezone",
                                               "time" };
        return cJSON_IsObject( a_item ) &&
               string_equals_any( a_name, objects, sizeof( objects ) / sizeof( objects[ 0 ] ) );
    }

    if( 0 == strcmp( a_path, "display" ) )
    {
        if( 0 == strcmp( a_name, "brightness" ) )
        {
            return cJSON_IsNumber( a_item ) && a_item->valueint >= 10 && a_item->valueint <= 100;
        }
        if( 0 == strcmp( a_name, "dim_timeout_seconds" ) ||
            0 == strcmp( a_name, "off_timeout_seconds" ) )
        {
            return cJSON_IsNumber( a_item ) && a_item->valueint >= 0 &&
                   a_item->valueint <= CONFIG_DISPLAY_TIMEOUT_MAX_SECONDS;
        }
        if( 0 == strcmp( a_name, "clock_24h" ) )
        {
            return cJSON_IsBool( a_item );
        }
        if( 0 == strcmp( a_name, "theme" ) && cJSON_IsString( a_item ) &&
            NULL != a_item->valuestring )
        {
            rfscout_theme_id_t parsed = RFSCOUT_THEME_COUNT;
            return 0 == strcasecmp( a_item->valuestring, "discipline" ) ||
                   ui_theme_from_config_name( a_item->valuestring, &parsed );
        }
        return false;
    }

    if( 0 == strcmp( a_path, "power" ) )
    {
        if( 0 == strcmp( a_name, "short_press" ) )
        {
            return power_action_valid( a_item, false, false );
        }
        if( 0 == strcmp( a_name, "long_press" ) )
        {
            return power_action_valid( a_item, true, false );
        }
        if( 0 == strcmp( a_name, "low_battery_percent" ) )
        {
            return cJSON_IsNumber( a_item ) && a_item->valueint >= 0 &&
                   a_item->valueint <= (int)RFSCOUT_LOW_BATTERY_THRESHOLD_MAX_PERCENT &&
                   ( 0 == a_item->valueint || 0 == ( a_item->valueint % 5 ) );
        }
        if( 0 == strcmp( a_name, "low_battery_action" ) ||
            0 == strcmp( a_name, "high_temperature_action" ) )
        {
            return power_action_valid( a_item, false, true );
        }
        if( 0 == strcmp( a_name, "high_temperature_c" ) )
        {
            return cJSON_IsNumber( a_item ) &&
                   ( 0 == a_item->valueint ||
                     ( a_item->valueint >= (int)RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_MIN_C &&
                       a_item->valueint <= (int)RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_MAX_C &&
                       0 == ( a_item->valueint %
                              (int)RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_STEP_C ) ) );
        }
        return false;
    }

    if( 0 == strcmp( a_path, "logging" ) )
    {
        return 0 == strcmp( a_name, "enabled" ) && cJSON_IsBool( a_item );
    }

    if( 0 == strcmp( a_path, "recorder" ) )
    {
        if( 0 == strcmp( a_name, "volume" ) )
        {
            return cJSON_IsNumber( a_item ) && a_item->valueint >= 0 && a_item->valueint <= 100;
        }
        return 0 == strcmp( a_name, "input_gain_db" ) && cJSON_IsNumber( a_item ) &&
               a_item->valueint >= 0 && a_item->valueint <= 24 && 0 == ( a_item->valueint % 3 );
    }

    if( 0 == strcmp( a_path, "notifications" ) )
    {
        if( 0 == strcmp( a_name, "mode" ) && cJSON_IsString( a_item ) &&
            NULL != a_item->valuestring )
        {
            static const char *const modes[] = { "OFF", "TONE", "MORSE" };
            return string_equals_any(
                a_item->valuestring, modes, sizeof( modes ) / sizeof( modes[ 0 ] ) );
        }
        if( 0 == strcmp( a_name, "volume" ) )
        {
            return cJSON_IsNumber( a_item ) && a_item->valueint >= 0 &&
                   a_item->valueint <= RFSCOUT_ALERT_VOLUME_MAX_PERCENT;
        }
        return 0 == strcmp( a_name, "characters" ) && cJSON_IsObject( a_item );
    }
    if( 0 == strcmp( a_path, "notifications.characters" ) )
    {
        return valid_morse_character( a_item );
    }

    if( 0 == strcmp( a_path, "timezone" ) )
    {
        if( 0 == strcmp( a_name, "selected" ) )
        {
            return cJSON_IsString( a_item ) && NULL != a_item->valuestring;
        }
        return 0 == strcmp( a_name, "options" ) && timezone_options_valid( a_item );
    }

    if( 0 == strcmp( a_path, "time" ) )
    {
        if( 0 == strcmp( a_name, "epoch" ) )
        {
            return cJSON_IsNumber( a_item ) &&
                   a_item->valuedouble >= (double)CONFIG_MIN_VALID_EPOCH &&
                   a_item->valuedouble <= (double)CONFIG_MAX_VALID_EPOCH;
        }
        return 0 == strcmp( a_name, "source" ) && cJSON_IsString( a_item ) &&
               NULL != a_item->valuestring;
    }

    if( 0 == strcmp( a_path, "apps" ) )
    {
        if( 0 == strcmp( a_name, "startup" ) )
        {
            return cJSON_IsString( a_item ) && NULL != a_item->valuestring &&
                   app_name_valid( a_item->valuestring );
        }
        static const char *const app_names[] = {
            "wifi_bt", "cc1101", "nrf24", "audio", "thermal", "device" };
        return string_equals_any(
                   a_name, app_names, sizeof( app_names ) / sizeof( app_names[ 0 ] ) ) &&
               ( cJSON_IsObject( a_item ) || cJSON_IsBool( a_item ) );
    }

    if( 0 == strncmp( a_path, "apps.", 5 ) )
    {
        const char *remainder = a_path + 5;
        const char *dot = strchr( remainder, '.' );
        if( NULL == dot )
        {
            if( 0 == strcmp( a_name, "enabled" ) )
            {
                return cJSON_IsBool( a_item );
            }
            if( 0 == strcmp( remainder, "wifi_bt" ) && 0 == strcmp( a_name, "retention" ) )
            {
                return cJSON_IsObject( a_item );
            }
            return 0 == strcmp( a_name, "notifications" ) && cJSON_IsObject( a_item );
        }

        if( NULL != strstr( a_path, ".retention" ) )
        {
            return ( 0 == strcmp( a_name, "wifi_seconds" ) ||
                     0 == strcmp( a_name, "bluetooth_seconds" ) ||
                     0 == strcmp( a_name, "surveillance_seconds" ) ) &&
                   cJSON_IsNumber( a_item ) &&
                   a_item->valueint >= (int)RFSCOUT_OBSERVATION_TIMEOUT_MIN_SECONDS &&
                   a_item->valueint <= (int)RFSCOUT_OBSERVATION_TIMEOUT_MAX_SECONDS &&
                   0 == ( a_item->valueint % (int)RFSCOUT_OBSERVATION_TIMEOUT_STEP_SECONDS );
        }
        if( NULL != strstr( a_path, ".notifications.characters" ) )
        {
            return valid_morse_character( a_item );
        }
        if( NULL != strstr( a_path, ".notifications.events" ) )
        {
            return ( 0 == strcmp( a_name, "wifi_device" ) ||
                     0 == strcmp( a_name, "ble_device" ) ) &&
                   cJSON_IsBool( a_item );
        }
        if( NULL != strstr( a_path, ".notifications" ) )
        {
            if( 0 == strcmp( a_name, "enabled" ) )
            {
                return cJSON_IsBool( a_item );
            }
            if( 0 == strcmp( a_name, "characters" ) || 0 == strcmp( a_name, "events" ) )
            {
                return cJSON_IsObject( a_item );
            }
        }
    }

    return false;
}

static bool display_timeout_value_valid( const cJSON *a_item )
{
    return cJSON_IsNumber( a_item ) && a_item->valueint >= 0 &&
           a_item->valueint <= CONFIG_DISPLAY_TIMEOUT_MAX_SECONDS;
}

static bool display_timeouts_mergeable( const cJSON *a_target, const cJSON *a_source )
{
    const cJSON *target_dim = cJSON_GetObjectItemCaseSensitive( a_target, "dim_timeout_seconds" );
    const cJSON *target_off = cJSON_GetObjectItemCaseSensitive( a_target, "off_timeout_seconds" );
    const cJSON *source_dim = cJSON_GetObjectItemCaseSensitive( a_source, "dim_timeout_seconds" );
    const cJSON *source_off = cJSON_GetObjectItemCaseSensitive( a_source, "off_timeout_seconds" );

    int dim = display_timeout_value_valid( target_dim ) ? target_dim->valueint : 0;
    int off = display_timeout_value_valid( target_off ) ? target_off->valueint : 0;
    if( display_timeout_value_valid( source_dim ) )
    {
        dim = source_dim->valueint;
    }
    if( display_timeout_value_valid( source_off ) )
    {
        off = source_off->valueint;
    }

    return 0 == dim || 0 == off || off > dim;
}

static bool merge_object( cJSON *a_target,
                          const cJSON *a_source,
                          const char *a_path,
                          bool a_protect_v3_top_level )
{
    if( !cJSON_IsObject( a_target ) || !cJSON_IsObject( a_source ) || NULL == a_path )
    {
        return false;
    }

    const bool merge_display_timeouts =
        0 != strcmp( a_path, "display" ) || display_timeouts_mergeable( a_target, a_source );
    const bool merge_timezone_pair =
        0 != strcmp( a_path, "timezone" ) || timezone_pair_mergeable( a_target, a_source );
    const cJSON *item = NULL;
    cJSON_ArrayForEach( item, a_source )
    {
        if( NULL == item->string )
        {
            continue;
        }
        if( a_protect_v3_top_level &&
            ( 0 == strcmp( item->string, "version" ) || 0 == strcmp( item->string, "audio" ) ) )
        {
            continue;
        }
        if( !legacy_item_valid( a_path, item->string, item ) )
        {
            continue;
        }
        if( 0 == strcmp( a_path, "display" ) && !merge_display_timeouts &&
            ( 0 == strcmp( item->string, "dim_timeout_seconds" ) ||
              0 == strcmp( item->string, "off_timeout_seconds" ) ) )
        {
            continue;
        }
        if( 0 == strcmp( a_path, "timezone" ) && !merge_timezone_pair &&
            ( 0 == strcmp( item->string, "selected" ) || 0 == strcmp( item->string, "options" ) ) )
        {
            continue;
        }

        cJSON *existing = cJSON_GetObjectItemCaseSensitive( a_target, item->string );
        if( 0 == strcmp( a_path, "apps" ) && cJSON_IsBool( item ) && cJSON_IsObject( existing ) )
        {
            cJSON *enabled = cJSON_CreateBool( cJSON_IsTrue( item ) );
            if( NULL == enabled )
            {
                return false;
            }
            cJSON *existing_enabled = cJSON_GetObjectItemCaseSensitive( existing, "enabled" );
            if( NULL != existing_enabled )
            {
                if( !cJSON_ReplaceItemInObjectCaseSensitive( existing, "enabled", enabled ) )
                {
                    cJSON_Delete( enabled );
                    return false;
                }
            }
            else if( !cJSON_AddItemToObject( existing, "enabled", enabled ) )
            {
                cJSON_Delete( enabled );
                return false;
            }
            continue;
        }
        if( cJSON_IsObject( item ) )
        {
            cJSON *target_object = existing;
            if( !cJSON_IsObject( target_object ) )
            {
                target_object = cJSON_CreateObject();
                if( NULL == target_object )
                {
                    return false;
                }
                if( NULL != existing )
                {
                    if( !cJSON_ReplaceItemInObjectCaseSensitive(
                            a_target, item->string, target_object ) )
                    {
                        cJSON_Delete( target_object );
                        return false;
                    }
                }
                else if( !cJSON_AddItemToObject( a_target, item->string, target_object ) )
                {
                    cJSON_Delete( target_object );
                    return false;
                }
            }

            char child_path[ 128 ];
            snprintf( child_path,
                      sizeof( child_path ),
                      "%s%s%s",
                      a_path,
                      '\0' != a_path[ 0 ] ? "." : "",
                      item->string );
            if( !merge_object( target_object, item, child_path, false ) )
            {
                return false;
            }
            continue;
        }

        cJSON *copy = cJSON_Duplicate( item, true );
        if( NULL == copy )
        {
            return false;
        }
        if( NULL != existing )
        {
            if( !cJSON_ReplaceItemInObjectCaseSensitive( a_target, item->string, copy ) )
            {
                cJSON_Delete( copy );
                return false;
            }
        }
        else if( !cJSON_AddItemToObject( a_target, item->string, copy ) )
        {
            cJSON_Delete( copy );
            return false;
        }
    }
    return true;
}

static void legacy_audio_gains_for_theme( rfscout_theme_id_t a_theme,
                                          int *a_interaction_gain,
                                          int *a_notification_gain )
{
    *a_interaction_gain = 35;
    *a_notification_gain = 100;
    if( RFSCOUT_THEME_OUTDOOR == a_theme )
    {
        *a_interaction_gain = 55;
    }
    else if( RFSCOUT_THEME_NIGHT == a_theme )
    {
        *a_interaction_gain = 20;
        *a_notification_gain = 60;
    }
}

static bool active_needs_legacy_nvs_theme( const cJSON *a_root )
{
    int version = 0;
    if( !schema_version( a_root, &version ) || 1 != version )
    {
        return false;
    }
    const cJSON *display = cJSON_GetObjectItemCaseSensitive( a_root, "display" );
    const cJSON *theme =
        cJSON_IsObject( display ) ? cJSON_GetObjectItemCaseSensitive( display, "theme" ) : NULL;
    return !cJSON_IsString( theme );
}

static esp_err_t read_legacy_nvs_theme( rfscout_theme_id_t *a_theme,
                                        bool *a_quiet,
                                        bool *a_found )
{
    if( NULL == a_theme || NULL == a_quiet || NULL == a_found )
    {
        return ESP_ERR_INVALID_ARG;
    }

    *a_found = false;
    nvs_handle_t handle;
    esp_err_t result = nvs_open( LEGACY_THEME_NVS_NAMESPACE, NVS_READONLY, &handle );
    if( ESP_ERR_NVS_NOT_FOUND == result )
    {
        return ESP_OK;
    }
    if( ESP_OK != result )
    {
        return result;
    }

    char stored[ LEGACY_THEME_NVS_VALUE_CAPACITY ];
    size_t stored_length = sizeof( stored );
    result = nvs_get_str( handle, LEGACY_THEME_NVS_KEY, stored, &stored_length );
    nvs_close( handle );
    if( ESP_ERR_NVS_NOT_FOUND == result )
    {
        return ESP_OK;
    }
    if( ESP_OK != result )
    {
        return result;
    }

    *a_quiet = false;
    if( 0 == strcasecmp( stored, "discipline" ) )
    {
        *a_theme = RFSCOUT_THEME_NIGHT;
        *a_quiet = true;
        *a_found = true;
        return ESP_OK;
    }
    if( ui_theme_from_config_name( stored, a_theme ) )
    {
        *a_found = true;
    }
    return ESP_OK;
}

static esp_err_t apply_legacy_nvs_theme( cJSON *a_candidate, const cJSON *a_active )
{
    if( !active_needs_legacy_nvs_theme( a_active ) )
    {
        return ESP_OK;
    }

    rfscout_theme_id_t theme = RFSCOUT_THEME_INDOOR;
    bool quiet = false;
    bool found = false;
    const esp_err_t read_result = read_legacy_nvs_theme( &theme, &quiet, &found );
    if( ESP_OK != read_result )
    {
        return read_result;
    }
    if( !found )
    {
        return ESP_OK;
    }

    cJSON *display = cJSON_GetObjectItemCaseSensitive( a_candidate, "display" );
    cJSON *theme_item =
        cJSON_IsObject( display ) ? cJSON_GetObjectItemCaseSensitive( display, "theme" ) : NULL;
    if( !cJSON_IsString( theme_item ) || NULL == theme_item->valuestring )
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    rfscout_theme_id_t candidate_theme = RFSCOUT_THEME_COUNT;
    if( !ui_theme_from_config_name( theme_item->valuestring, &candidate_theme ) )
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* A schema-v1 JSON file has no visual theme, so NVS will often simply repeat the
       visual theme that was already present in the quarantined v3 document. In that
       unchanged case the NVS value carries no evidence that the user changed the
       independent v3 audio policy or gains; preserve them. Discipline is different:
       it explicitly carries legacy quiet semantics and must still be imported. */
    if( !quiet && candidate_theme == theme )
    {
        return ESP_OK;
    }

    cJSON *audio = cJSON_GetObjectItemCaseSensitive( a_candidate, "audio" );
    cJSON *policy =
        cJSON_IsObject( audio ) ? cJSON_GetObjectItemCaseSensitive( audio, "policy" ) : NULL;
    cJSON *interaction = cJSON_IsObject( audio )
                             ? cJSON_GetObjectItemCaseSensitive( audio, "interaction_gain_percent" )
                             : NULL;
    cJSON *notification =
        cJSON_IsObject( audio )
            ? cJSON_GetObjectItemCaseSensitive( audio, "notification_gain_percent" )
            : NULL;
    if( !cJSON_IsString( policy ) || !cJSON_IsNumber( interaction ) ||
        !cJSON_IsNumber( notification ) ||
        !cJSON_SetValuestring( theme_item, ui_theme_config_name( theme ) ) ||
        !cJSON_SetValuestring( policy, quiet ? "quiet" : "normal" ) )
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    int interaction_gain = 35;
    int notification_gain = 100;
    legacy_audio_gains_for_theme( theme, &interaction_gain, &notification_gain );
    cJSON_SetNumberValue( interaction, interaction_gain );
    cJSON_SetNumberValue( notification, notification_gain );
    ESP_LOGW( TAG,
              "imported schema-v1 NVS theme=%s audio_policy=%s before downgrade recovery",
              ui_theme_config_name( theme ),
              quiet ? "quiet" : "normal" );
    return ESP_OK;
}

static bool normalize_legacy_theme( cJSON *a_root )
{
    cJSON *display = cJSON_GetObjectItemCaseSensitive( a_root, "display" );
    cJSON *theme =
        cJSON_IsObject( display ) ? cJSON_GetObjectItemCaseSensitive( display, "theme" ) : NULL;
    if( !cJSON_IsString( theme ) || NULL == theme->valuestring ||
        0 != strcasecmp( theme->valuestring, "discipline" ) )
    {
        return true;
    }

    if( !cJSON_SetValuestring( theme, "night" ) )
    {
        return false;
    }

    cJSON *audio = cJSON_GetObjectItemCaseSensitive( a_root, "audio" );
    cJSON *policy =
        cJSON_IsObject( audio ) ? cJSON_GetObjectItemCaseSensitive( audio, "policy" ) : NULL;
    cJSON *interaction = cJSON_IsObject( audio )
                             ? cJSON_GetObjectItemCaseSensitive( audio, "interaction_gain_percent" )
                             : NULL;
    cJSON *notification =
        cJSON_IsObject( audio )
            ? cJSON_GetObjectItemCaseSensitive( audio, "notification_gain_percent" )
            : NULL;
    if( !cJSON_IsString( policy ) || !cJSON_IsNumber( interaction ) ||
        !cJSON_IsNumber( notification ) || !cJSON_SetValuestring( policy, "quiet" ) )
    {
        return false;
    }

    cJSON_SetNumberValue( interaction, 20 );
    cJSON_SetNumberValue( notification, 60 );
    return true;
}

static esp_err_t resume_interrupted_recovery( void )
{
    if( 0 != access( CONFIG_RECOVERY_PATH, F_OK ) )
    {
        return ESP_OK;
    }

    if( 0 == access( RFSCOUT_CONFIG_PATH, F_OK ) )
    {
        (void)unlink( CONFIG_RECOVERY_PATH );
        return ESP_OK;
    }

    cJSON *recovery = NULL;
    const esp_err_t load_result = load_json_document( CONFIG_RECOVERY_PATH, &recovery );
    const bool recovery_valid =
        ESP_OK == load_result && NULL != recovery && valid_schema_v3_candidate( recovery );
    cJSON_Delete( recovery );

    if( recovery_valid )
    {
        if( 0 != rename( CONFIG_RECOVERY_PATH, RFSCOUT_CONFIG_PATH ) )
        {
            return ESP_FAIL;
        }
        (void)unlink( CONFIG_BAD_PATH );
        (void)unlink( CONFIG_DOWNGRADE_PATH );
        ESP_LOGW( TAG, "completed interrupted downgrade-recovery transaction" );
        return ESP_OK;
    }

    if( 0 == access( CONFIG_DOWNGRADE_PATH, F_OK ) )
    {
        if( 0 != rename( CONFIG_DOWNGRADE_PATH, RFSCOUT_CONFIG_PATH ) )
        {
            return ESP_FAIL;
        }
        (void)unlink( CONFIG_RECOVERY_PATH );
        ESP_LOGW( TAG, "rolled back invalid interrupted downgrade-recovery transaction" );
        return ESP_OK;
    }

    ESP_LOGW( TAG, "leaving invalid recovery document in place; no rollback copy is available" );
    return ESP_FAIL;
}

static esp_err_t restore_interrupted_save_before_downgrade( void )
{
    if( 0 == access( RFSCOUT_CONFIG_PATH, F_OK ) || 0 != access( CONFIG_BACKUP_PATH, F_OK ) )
    {
        return ESP_OK;
    }
    if( 0 != rename( CONFIG_BACKUP_PATH, RFSCOUT_CONFIG_PATH ) )
    {
        ESP_LOGW( TAG, "failed to restore interrupted configuration save before reconciliation" );
        return ESP_FAIL;
    }
    ESP_LOGW( TAG, "restored interrupted configuration save before downgrade reconciliation" );
    return ESP_OK;
}

static esp_err_t restore_quarantine_over_active( bool a_preserve_displaced )
{
    (void)unlink( CONFIG_DOWNGRADE_PATH );
    if( 0 == access( RFSCOUT_CONFIG_PATH, F_OK ) &&
        0 != rename( RFSCOUT_CONFIG_PATH, CONFIG_DOWNGRADE_PATH ) )
    {
        return ESP_FAIL;
    }

    if( 0 != rename( CONFIG_BAD_PATH, RFSCOUT_CONFIG_PATH ) )
    {
        (void)rename( CONFIG_DOWNGRADE_PATH, RFSCOUT_CONFIG_PATH );
        return ESP_FAIL;
    }

    if( !a_preserve_displaced )
    {
        (void)unlink( CONFIG_DOWNGRADE_PATH );
    }
    return ESP_OK;
}

static esp_err_t restore_merged_candidate( cJSON *a_candidate )
{
    (void)unlink( CONFIG_RECOVERY_PATH );
    if( ESP_OK != save_json_document( CONFIG_RECOVERY_PATH, a_candidate ) )
    {
        (void)unlink( CONFIG_RECOVERY_PATH );
        return ESP_FAIL;
    }

    (void)unlink( CONFIG_DOWNGRADE_PATH );
    if( 0 != rename( RFSCOUT_CONFIG_PATH, CONFIG_DOWNGRADE_PATH ) )
    {
        (void)unlink( CONFIG_RECOVERY_PATH );
        return ESP_FAIL;
    }
    if( 0 != rename( CONFIG_RECOVERY_PATH, RFSCOUT_CONFIG_PATH ) )
    {
        (void)rename( CONFIG_DOWNGRADE_PATH, RFSCOUT_CONFIG_PATH );
        (void)unlink( CONFIG_RECOVERY_PATH );
        return ESP_FAIL;
    }

    (void)unlink( CONFIG_BAD_PATH );
    (void)unlink( CONFIG_DOWNGRADE_PATH );
    return ESP_OK;
}

esp_err_t device_config_recover_downgrade( void )
{
    const esp_err_t resume_result = resume_interrupted_recovery();
    if( ESP_OK != resume_result )
    {
        return resume_result;
    }

    const esp_err_t backup_result = restore_interrupted_save_before_downgrade();
    if( ESP_OK != backup_result )
    {
        return backup_result;
    }

    cJSON *candidate = NULL;
    const esp_err_t candidate_result = load_json_document( CONFIG_BAD_PATH, &candidate );
    if( ESP_ERR_NOT_FOUND == candidate_result )
    {
        return ESP_OK;
    }
    if( ESP_ERR_INVALID_SIZE == candidate_result || ESP_ERR_INVALID_RESPONSE == candidate_result )
    {
        ESP_LOGW( TAG, "ignoring malformed quarantined configuration" );
        return ESP_OK;
    }
    if( ESP_OK != candidate_result )
    {
        ESP_LOGW( TAG,
                  "quarantined configuration read failed; preserving recovery state: %s",
                  esp_err_to_name( candidate_result ) );
        return candidate_result;
    }
    if( !valid_schema_v3_candidate( candidate ) )
    {
        cJSON_Delete( candidate );
        ESP_LOGW( TAG, "ignoring quarantined configuration that is not valid schema v3" );
        return ESP_OK;
    }

    cJSON *active = NULL;
    active_config_state_t active_state = ACTIVE_CONFIG_INVALID;
    const esp_err_t active_result = load_active_config( &active, &active_state );
    if( ESP_OK != active_result )
    {
        cJSON_Delete( candidate );
        ESP_LOGW( TAG,
                  "active downgraded configuration read failed; preserving recovery state: %s",
                  esp_err_to_name( active_result ) );
        return active_result;
    }
    if( ACTIVE_CONFIG_CURRENT == active_state )
    {
        cJSON_Delete( active );
        cJSON_Delete( candidate );
        return ESP_OK;
    }

    esp_err_t result = ESP_OK;
    if( ACTIVE_CONFIG_MISSING == active_state )
    {
        result = restore_quarantine_over_active( false );
    }
    else if( ACTIVE_CONFIG_INVALID == active_state )
    {
        result = restore_quarantine_over_active( true );
        if( ESP_OK == result )
        {
            ESP_LOGW( TAG, "restored validated schema-v3 configuration over corrupt active file" );
        }
    }
    else
    {
        result = apply_legacy_nvs_theme( candidate, active );
        if( ESP_OK == result && !merge_object( candidate, active, "", true ) )
        {
            result = ESP_ERR_NO_MEM;
        }
        else if( ESP_OK == result && !normalize_legacy_theme( candidate ) )
        {
            result = ESP_ERR_INVALID_RESPONSE;
        }
        else if( ESP_OK == result && !valid_schema_v3_candidate( candidate ) )
        {
            result = ESP_ERR_INVALID_RESPONSE;
        }
        else if( ESP_OK == result )
        {
            result = restore_merged_candidate( candidate );
        }
        if( ESP_OK == result )
        {
            ESP_LOGW( TAG,
                      "restored schema-v3 configuration with settings changed during downgrade" );
        }
    }

    cJSON_Delete( active );
    cJSON_Delete( candidate );
    if( ESP_OK == result && ACTIVE_CONFIG_MISSING == active_state )
    {
        ESP_LOGW( TAG, "restored validated schema-v3 configuration after firmware downgrade" );
    }
    return result;
}
