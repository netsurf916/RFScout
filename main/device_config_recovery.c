/**
    device_config_recovery.c : RFScout configuration downgrade recovery
    Description: Restores validated newer-schema configuration quarantined by older firmware.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "device_config_recovery.h"

#include "audio_policy.h"
#include "device_config.h"
#include "ui_theme.h"

#include "cJSON.h"
#include "esp_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_BAD_PATH RFSCOUT_CONFIG_PATH ".bad"
#define CONFIG_BACKUP_PATH RFSCOUT_CONFIG_PATH ".bak"
#define CONFIG_DOWNGRADE_PATH RFSCOUT_CONFIG_PATH ".downgrade"
#define CONFIG_RECOVERY_PATH RFSCOUT_CONFIG_PATH ".recovering"
#define CONFIG_MAX_FILE_BYTES 16384L

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

static active_config_state_t load_active_config( cJSON **a_root )
{
    if( NULL == a_root )
    {
        return ACTIVE_CONFIG_INVALID;
    }
    *a_root = NULL;

    const esp_err_t result = load_json_document( RFSCOUT_CONFIG_PATH, a_root );
    if( ESP_ERR_NOT_FOUND == result )
    {
        return ACTIVE_CONFIG_MISSING;
    }
    if( ESP_OK != result )
    {
        return ACTIVE_CONFIG_INVALID;
    }

    int version = 0;
    if( !schema_version( *a_root, &version ) )
    {
        cJSON_Delete( *a_root );
        *a_root = NULL;
        return ACTIVE_CONFIG_INVALID;
    }
    if( version < (int)RFSCOUT_CONFIG_SCHEMA_VERSION )
    {
        return ACTIVE_CONFIG_OLDER;
    }
    return ACTIVE_CONFIG_CURRENT;
}

static bool merge_object( cJSON *a_target, const cJSON *a_source, bool a_protect_v3_top_level )
{
    if( !cJSON_IsObject( a_target ) || !cJSON_IsObject( a_source ) )
    {
        return false;
    }

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

        cJSON *existing = cJSON_GetObjectItemCaseSensitive( a_target, item->string );
        if( cJSON_IsObject( existing ) && cJSON_IsObject( item ) )
        {
            if( !merge_object( existing, item, false ) )
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

static bool normalize_legacy_theme( cJSON *a_root )
{
    cJSON *display = cJSON_GetObjectItemCaseSensitive( a_root, "display" );
    cJSON *theme =
        cJSON_IsObject( display ) ? cJSON_GetObjectItemCaseSensitive( display, "theme" ) : NULL;
    if( !cJSON_IsString( theme ) || NULL == theme->valuestring ||
        0 != strcmp( theme->valuestring, "discipline" ) )
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

    /* An absent active document plus .bak means a normal atomic save was interrupted
       after moving the previous active configuration aside. device_config_init() owns
       that recovery and must restore .bak before an older quarantined .bad candidate
       is considered. */
    if( 0 != access( RFSCOUT_CONFIG_PATH, F_OK ) && 0 == access( CONFIG_BACKUP_PATH, F_OK ) )
    {
        return ESP_OK;
    }

    cJSON *candidate = NULL;
    const esp_err_t candidate_result = load_json_document( CONFIG_BAD_PATH, &candidate );
    if( ESP_ERR_NOT_FOUND == candidate_result )
    {
        return ESP_OK;
    }
    if( ESP_OK != candidate_result )
    {
        ESP_LOGW( TAG, "ignoring unreadable quarantined configuration" );
        return ESP_OK;
    }
    if( !valid_schema_v3_candidate( candidate ) )
    {
        cJSON_Delete( candidate );
        ESP_LOGW( TAG, "ignoring quarantined configuration that is not valid schema v3" );
        return ESP_OK;
    }

    cJSON *active = NULL;
    const active_config_state_t active_state = load_active_config( &active );
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
        if( !merge_object( candidate, active, true ) )
        {
            result = ESP_ERR_NO_MEM;
        }
        else if( !normalize_legacy_theme( candidate ) )
        {
            result = ESP_ERR_INVALID_RESPONSE;
        }
        else if( !valid_schema_v3_candidate( candidate ) )
        {
            result = ESP_ERR_INVALID_RESPONSE;
        }
        else
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