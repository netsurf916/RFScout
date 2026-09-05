/**
    config_store.c : RFScout internal configuration persistence
    Description: Mounts internal LittleFS and exchanges configuration through explicit SD files.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "config_store.h"

#include "device_config.h"
#include "storage.h"

#include "cJSON.h"
#include "esp_littlefs.h"
#include "esp_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_FILE_MAX_BYTES 16384L
#define CONFIG_COPY_BUFFER_BYTES 512U
#define INTERNAL_CONFIG_TEMP_PATH RFSCOUT_INTERNAL_CONFIG_PATH ".tmp"
#define SD_CONFIG_EXAMPLE_TEMP_PATH RFSCOUT_SD_CONFIG_EXAMPLE_PATH ".tmp"

static const char *TAG = "config_store";
static bool initialized;
static bool available;
static bool imported;

static void cleanup_internal_artifacts( void )
{
    (void)unlink( INTERNAL_CONFIG_TEMP_PATH );

    /* Earlier configuration persistence experimented with recovery/quarantine files. The internal
       store intentionally retains only the live configuration, so remove any such legacy artifacts
       if a development image ever created them on this partition. */
    (void)unlink( RFSCOUT_INTERNAL_CONFIG_PATH ".bak" );
    (void)unlink( RFSCOUT_INTERNAL_CONFIG_PATH ".bad" );
    (void)unlink( RFSCOUT_INTERNAL_CONFIG_PATH ".downgrade" );
    (void)unlink( RFSCOUT_INTERNAL_CONFIG_PATH ".recovering" );
}

static esp_err_t validate_config_document( const char *a_path )
{
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
    if( ( length <= 0L ) || ( length > CONFIG_FILE_MAX_BYTES ) ||
        ( 0 != fseek( file, 0, SEEK_SET ) ) )
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
    if( NULL == root )
    {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive( root, "version" );
    const bool valid =
        cJSON_IsNumber( version ) && ( version->valueint == (int)RFSCOUT_CONFIG_SCHEMA_VERSION );
    cJSON_Delete( root );
    return valid ? ESP_OK : ESP_ERR_INVALID_VERSION;
}

static esp_err_t copy_file_to_temp( const char *a_source, const char *a_temp )
{
    FILE *source = fopen( a_source, "rb" );
    if( NULL == source )
    {
        return ( ENOENT == errno ) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    FILE *destination = fopen( a_temp, "wb" );
    if( NULL == destination )
    {
        fclose( source );
        return ESP_FAIL;
    }

    uint8_t buffer[ CONFIG_COPY_BUFFER_BYTES ];
    bool ok = true;
    while( ok )
    {
        const size_t count = fread( buffer, 1U, sizeof( buffer ), source );
        if( count > 0U && count != fwrite( buffer, 1U, count, destination ) )
        {
            ok = false;
            break;
        }
        if( count < sizeof( buffer ) )
        {
            if( ferror( source ) )
            {
                ok = false;
            }
            break;
        }
    }

    if( ok )
    {
        ok = ( 0 == fflush( destination ) ) && ( 0 == fsync( fileno( destination ) ) );
    }
    if( 0 != fclose( destination ) )
    {
        ok = false;
    }
    fclose( source );

    if( !ok )
    {
        (void)unlink( a_temp );
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t atomic_copy( const char *a_source, const char *a_temp, const char *a_destination )
{
    const esp_err_t result = copy_file_to_temp( a_source, a_temp );
    if( ESP_OK != result )
    {
        return result;
    }
    if( 0 != rename( a_temp, a_destination ) )
    {
        (void)unlink( a_temp );
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool files_equal( const char *a_first, const char *a_second )
{
    FILE *first = fopen( a_first, "rb" );
    FILE *second = fopen( a_second, "rb" );
    if( NULL == first || NULL == second )
    {
        if( NULL != first )
        {
            fclose( first );
        }
        if( NULL != second )
        {
            fclose( second );
        }
        return false;
    }

    uint8_t first_buffer[ CONFIG_COPY_BUFFER_BYTES ];
    uint8_t second_buffer[ CONFIG_COPY_BUFFER_BYTES ];
    bool equal = true;
    while( equal )
    {
        const size_t first_count = fread( first_buffer, 1U, sizeof( first_buffer ), first );
        const size_t second_count = fread( second_buffer, 1U, sizeof( second_buffer ), second );
        if( first_count != second_count ||
            ( first_count > 0U && 0 != memcmp( first_buffer, second_buffer, first_count ) ) )
        {
            equal = false;
            break;
        }
        if( first_count < sizeof( first_buffer ) )
        {
            if( ferror( first ) || ferror( second ) )
            {
                equal = false;
            }
            break;
        }
    }

    fclose( first );
    fclose( second );
    return equal;
}

static void quarantine_sd_import( void )
{
    (void)unlink( RFSCOUT_SD_CONFIG_BAD_PATH );
    if( 0 != rename( RFSCOUT_SD_CONFIG_IMPORT_PATH, RFSCOUT_SD_CONFIG_BAD_PATH ) )
    {
        ESP_LOGW( TAG, "could not quarantine invalid SD configuration import" );
    }
}

static void import_sd_config( void )
{
    if( !storage_is_available() || ( 0 != access( RFSCOUT_SD_CONFIG_IMPORT_PATH, F_OK ) ) )
    {
        return;
    }

    const esp_err_t validation = validate_config_document( RFSCOUT_SD_CONFIG_IMPORT_PATH );
    if( ESP_OK != validation )
    {
        ESP_LOGW( TAG, "SD configuration import rejected: %s", esp_err_to_name( validation ) );
        quarantine_sd_import();
        return;
    }

    const esp_err_t result = atomic_copy(
        RFSCOUT_SD_CONFIG_IMPORT_PATH, INTERNAL_CONFIG_TEMP_PATH, RFSCOUT_INTERNAL_CONFIG_PATH );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG,
                  "SD configuration import could not be committed internally: %s",
                  esp_err_to_name( result ) );
        return;
    }
    imported = true;

    if( 0 != unlink( RFSCOUT_SD_CONFIG_IMPORT_PATH ) )
    {
        ESP_LOGW( TAG, "imported SD configuration but could not remove the source file" );
    }
    else
    {
        ESP_LOGI( TAG, "imported SD configuration into internal flash" );
    }
}

esp_err_t config_store_init( void )
{
    if( initialized )
    {
        return available ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    initialized = true;

    const esp_vfs_littlefs_conf_t config = {
        .base_path = RFSCOUT_INTERNAL_CONFIG_ROOT,
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    const esp_err_t result = esp_vfs_littlefs_register( &config );
    if( ESP_OK != result )
    {
        ESP_LOGE( TAG, "internal LittleFS mount failed: %s", esp_err_to_name( result ) );
        return result;
    }

    available = true;
    cleanup_internal_artifacts();
    import_sd_config();
    return ESP_OK;
}

bool config_store_is_available( void )
{
    return available;
}

bool config_store_imported( void )
{
    return imported;
}

esp_err_t config_store_sync_example( void )
{
    if( !available || !storage_is_available() )
    {
        return ESP_OK;
    }
    if( 0 != access( RFSCOUT_INTERNAL_CONFIG_PATH, F_OK ) )
    {
        return ESP_ERR_NOT_FOUND;
    }

    (void)unlink( SD_CONFIG_EXAMPLE_TEMP_PATH );
    if( files_equal( RFSCOUT_INTERNAL_CONFIG_PATH, RFSCOUT_SD_CONFIG_EXAMPLE_PATH ) )
    {
        return ESP_OK;
    }

    const esp_err_t result = atomic_copy(
        RFSCOUT_INTERNAL_CONFIG_PATH, SD_CONFIG_EXAMPLE_TEMP_PATH, RFSCOUT_SD_CONFIG_EXAMPLE_PATH );
    if( ESP_OK == result )
    {
        ESP_LOGI( TAG, "updated %s", RFSCOUT_SD_CONFIG_EXAMPLE_PATH );
    }
    return result;
}
