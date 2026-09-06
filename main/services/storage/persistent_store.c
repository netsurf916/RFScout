/**
    persistent_store.c : RFScout internal persistent record service
    Description: Owns internal LittleFS and enforces system/app persistence namespaces.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "persistent_store.h"

#include "app_framework.h"
#include "app_session.h"
#include "event.h"

#include "esp_littlefs.h"
#include "esp_log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PERSISTENT_SYSTEM_ROOT RFSCOUT_INTERNAL_ROOT "/system"
#define PERSISTENT_APP_ROOT RFSCOUT_INTERNAL_ROOT "/app"
#define PERSISTENT_PATH_MAX 192U

static const char *TAG = "persistent_store";
static bool initialized;
static bool available;

static bool valid_name( const char *a_name, size_t a_max_length )
{
    if( NULL == a_name || '\0' == a_name[ 0 ] )
    {
        return false;
    }
    size_t length = 0U;
    for( ; '\0' != a_name[ length ]; ++length )
    {
        const char c = a_name[ length ];
        const bool valid = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
                           ( c >= '0' && c <= '9' ) || '_' == c || '-' == c || '.' == c;
        if( !valid || length >= a_max_length )
        {
            return false;
        }
    }
    return length <= a_max_length;
}

static esp_err_t ensure_directory( const char *a_path )
{
    if( 0 == mkdir( a_path, 0755 ) || EEXIST == errno )
    {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t
build_record_paths( const char *a_scope, const char *a_key, char *a_path, char *a_temp_path )
{
    if( !valid_name( a_key, RFSCOUT_PERSISTENT_KEY_MAX ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const int path_length = snprintf( a_path, PERSISTENT_PATH_MAX, "%s/%s", a_scope, a_key );
    const int temp_length = snprintf( a_temp_path, PERSISTENT_PATH_MAX, "%s.tmp", a_path );
    if( path_length < 0 || temp_length < 0 || path_length >= (int)PERSISTENT_PATH_MAX ||
        temp_length >= (int)PERSISTENT_PATH_MAX )
    {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t app_scope( rfscout_app_id_t a_app, char *a_scope )
{
    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_app );
    if( NULL == descriptor ||
        !valid_name( descriptor->persistence_key, RFSCOUT_PERSISTENT_KEY_MAX ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const int length = snprintf(
        a_scope, PERSISTENT_PATH_MAX, "%s/%s", PERSISTENT_APP_ROOT, descriptor->persistence_key );
    if( length < 0 || length >= (int)PERSISTENT_PATH_MAX )
    {
        return ESP_ERR_INVALID_SIZE;
    }
    return ensure_directory( a_scope );
}

static esp_err_t
record_write( const char *a_scope, const char *a_key, const void *a_data, size_t a_length )
{
    if( !available )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( NULL == a_data && a_length > 0U )
    {
        return ESP_ERR_INVALID_ARG;
    }

    char path[ PERSISTENT_PATH_MAX ];
    char temp_path[ PERSISTENT_PATH_MAX ];
    esp_err_t result = build_record_paths( a_scope, a_key, path, temp_path );
    if( ESP_OK != result )
    {
        return result;
    }

    (void)unlink( temp_path );
    FILE *file = fopen( temp_path, "wb" );
    if( NULL == file )
    {
        return ( ENOSPC == errno ) ? ESP_ERR_NO_MEM : ESP_FAIL;
    }

    bool ok = ( 0U == a_length ) || ( a_length == fwrite( a_data, 1U, a_length, file ) );
    if( ok )
    {
        ok = ( 0 == fflush( file ) ) && ( 0 == fsync( fileno( file ) ) );
    }
    if( 0 != fclose( file ) )
    {
        ok = false;
    }
    if( !ok )
    {
        const int saved_errno = errno;
        (void)unlink( temp_path );
        return ( ENOSPC == saved_errno ) ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    if( 0 != rename( temp_path, path ) )
    {
        const int saved_errno = errno;
        (void)unlink( temp_path );
        return ( ENOSPC == saved_errno ) ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t record_read(
    const char *a_scope, const char *a_key, void *a_data, size_t a_capacity, size_t *a_length )
{
    if( !available )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( NULL == a_length )
    {
        return ESP_ERR_INVALID_ARG;
    }

    char path[ PERSISTENT_PATH_MAX ];
    char temp_path[ PERSISTENT_PATH_MAX ];
    esp_err_t result = build_record_paths( a_scope, a_key, path, temp_path );
    if( ESP_OK != result )
    {
        return result;
    }

    FILE *file = fopen( path, "rb" );
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
    if( length < 0L || 0 != fseek( file, 0, SEEK_SET ) )
    {
        fclose( file );
        return ESP_FAIL;
    }
    *a_length = (size_t)length;
    if( NULL == a_data && 0U == a_capacity )
    {
        fclose( file );
        return ESP_OK;
    }
    if( NULL == a_data || (size_t)length > a_capacity )
    {
        fclose( file );
        return ESP_ERR_INVALID_SIZE;
    }
    if( length > 0L && (size_t)length != fread( a_data, 1U, (size_t)length, file ) )
    {
        fclose( file );
        return ESP_FAIL;
    }
    fclose( file );
    return ESP_OK;
}

static esp_err_t record_remove( const char *a_scope, const char *a_key )
{
    if( !available )
    {
        return ESP_ERR_INVALID_STATE;
    }
    char path[ PERSISTENT_PATH_MAX ];
    char temp_path[ PERSISTENT_PATH_MAX ];
    const esp_err_t result = build_record_paths( a_scope, a_key, path, temp_path );
    if( ESP_OK != result )
    {
        return result;
    }
    if( 0 == unlink( path ) )
    {
        return ESP_OK;
    }
    return ( ENOENT == errno ) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
}

static bool record_exists( const char *a_scope, const char *a_key )
{
    char path[ PERSISTENT_PATH_MAX ];
    char temp_path[ PERSISTENT_PATH_MAX ];
    return available && ESP_OK == build_record_paths( a_scope, a_key, path, temp_path ) &&
           0 == access( path, F_OK );
}

esp_err_t persistent_store_init( void )
{
    if( initialized )
    {
        return available ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    initialized = true;

    const esp_vfs_littlefs_conf_t config = {
        .base_path = RFSCOUT_INTERNAL_ROOT,
        .partition_label = "storage",
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    esp_err_t result = esp_vfs_littlefs_register( &config );
    if( ESP_FAIL == result )
    {
        ESP_LOGW( TAG, "internal LittleFS mount failed; attempting format" );
        result = esp_littlefs_format( config.partition_label );
        if( ESP_OK == result )
        {
            rfscout_event_emit( RFSCOUT_EVENT_INTERNAL_STORAGE_FORMATTED,
                                RFSCOUT_EVENT_SOURCE_SYSTEM,
                                (int32_t)ESP_OK );
            result = esp_vfs_littlefs_register( &config );
        }
    }
    if( ESP_OK != result )
    {
        rfscout_event_emit( RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED,
                            RFSCOUT_EVENT_SOURCE_SYSTEM,
                            (int32_t)result );
        ESP_LOGE( TAG, "internal LittleFS mount failed: %s", esp_err_to_name( result ) );
        return result;
    }
    if( ESP_OK != ( result = ensure_directory( PERSISTENT_SYSTEM_ROOT ) ) ||
        ESP_OK != ( result = ensure_directory( PERSISTENT_APP_ROOT ) ) )
    {
        ESP_LOGE( TAG, "could not create persistent ownership roots" );
        return result;
    }

    available = true;
    rfscout_event_emit(
        RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNTED, RFSCOUT_EVENT_SOURCE_SYSTEM, (int32_t)ESP_OK );
    ESP_LOGI( TAG, "internal persistent store available" );
    return ESP_OK;
}

bool persistent_store_is_available( void )
{
    return available;
}

esp_err_t system_store_write( const char *a_key, const void *a_data, size_t a_length )
{
    return record_write( PERSISTENT_SYSTEM_ROOT, a_key, a_data, a_length );
}

esp_err_t system_store_read( const char *a_key, void *a_data, size_t a_capacity, size_t *a_length )
{
    return record_read( PERSISTENT_SYSTEM_ROOT, a_key, a_data, a_capacity, a_length );
}

esp_err_t system_store_remove( const char *a_key )
{
    return record_remove( PERSISTENT_SYSTEM_ROOT, a_key );
}

bool system_store_exists( const char *a_key )
{
    return record_exists( PERSISTENT_SYSTEM_ROOT, a_key );
}

esp_err_t persistent_store_app_write( rfscout_app_id_t a_app,
                                      const char *a_key,
                                      const void *a_data,
                                      size_t a_length )
{
    char scope[ PERSISTENT_PATH_MAX ];
    const esp_err_t result = app_scope( a_app, scope );
    return ( ESP_OK == result ) ? record_write( scope, a_key, a_data, a_length ) : result;
}

esp_err_t persistent_store_app_read(
    rfscout_app_id_t a_app, const char *a_key, void *a_data, size_t a_capacity, size_t *a_length )
{
    char scope[ PERSISTENT_PATH_MAX ];
    const esp_err_t result = app_scope( a_app, scope );
    return ( ESP_OK == result ) ? record_read( scope, a_key, a_data, a_capacity, a_length )
                                : result;
}

esp_err_t persistent_store_app_remove( rfscout_app_id_t a_app, const char *a_key )
{
    char scope[ PERSISTENT_PATH_MAX ];
    const esp_err_t result = app_scope( a_app, scope );
    return ( ESP_OK == result ) ? record_remove( scope, a_key ) : result;
}

bool persistent_store_app_exists( rfscout_app_id_t a_app, const char *a_key )
{
    char scope[ PERSISTENT_PATH_MAX ];
    return ESP_OK == app_scope( a_app, scope ) && record_exists( scope, a_key );
}

static esp_err_t current_app( rfscout_app_id_t *a_app )
{
    if( NULL == a_app )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const rfscout_app_id_t app = app_session_app();
    if( app >= RFSCOUT_APP_COUNT )
    {
        return ESP_ERR_INVALID_STATE;
    }
    *a_app = app;
    return ESP_OK;
}

esp_err_t app_store_write( const char *a_key, const void *a_data, size_t a_length )
{
    rfscout_app_id_t app;
    const esp_err_t result = current_app( &app );
    return ESP_OK == result ? persistent_store_app_write( app, a_key, a_data, a_length ) : result;
}

esp_err_t app_store_read( const char *a_key, void *a_data, size_t a_capacity, size_t *a_length )
{
    rfscout_app_id_t app;
    const esp_err_t result = current_app( &app );
    return ESP_OK == result ? persistent_store_app_read( app, a_key, a_data, a_capacity, a_length )
                            : result;
}

esp_err_t app_store_remove( const char *a_key )
{
    rfscout_app_id_t app;
    const esp_err_t result = current_app( &app );
    return ESP_OK == result ? persistent_store_app_remove( app, a_key ) : result;
}

bool app_store_exists( const char *a_key )
{
    rfscout_app_id_t app;
    return ESP_OK == current_app( &app ) && persistent_store_app_exists( app, a_key );
}
