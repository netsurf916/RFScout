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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "psa/crypto.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PERSISTENT_SYSTEM_ROOT RFSCOUT_INTERNAL_ROOT "/system"
#define PERSISTENT_APP_ROOT RFSCOUT_INTERNAL_ROOT "/app"
#define PERSISTENT_HASH_BYTES 32U
#define PERSISTENT_HASH_HEX_LENGTH ( PERSISTENT_HASH_BYTES * 2U )
#define PERSISTENT_PATH_MAX 192U

static const char *TAG = "persistent_store";
static bool initialized;
static bool available;
static StaticSemaphore_t store_mutex_buffer;
static SemaphoreHandle_t store_mutex;

static esp_err_t ensure_directory( const char *a_path )
{
    struct stat info;
    if( 0 == stat( a_path, &info ) )
    {
        return S_ISDIR( info.st_mode ) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if( 0 == mkdir( a_path, 0755 ) || EEXIST == errno )
    {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static bool temp_name( const char *a_name )
{
    const size_t length = strlen( a_name );
    return length > 4U && 0 == strcmp( a_name + length - 4U, ".tmp" );
}

/** @brief Remove only interrupted-write temporaries when the persistence service starts. */
static void cleanup_temp_directory( const char *a_path, bool a_recurse_one_level )
{
    DIR *directory = opendir( a_path );
    if( NULL == directory )
    {
        return;
    }

    struct dirent *entry = NULL;
    while( NULL != ( entry = readdir( directory ) ) )
    {
        if( 0 == strcmp( entry->d_name, "." ) || 0 == strcmp( entry->d_name, ".." ) )
        {
            continue;
        }

        char path[ PERSISTENT_PATH_MAX ];
        const int length = snprintf( path, sizeof( path ), "%s/%s", a_path, entry->d_name );
        if( length < 0 || length >= (int)sizeof( path ) )
        {
            continue;
        }

        struct stat info;
        if( 0 != stat( path, &info ) )
        {
            continue;
        }
        if( S_ISREG( info.st_mode ) && temp_name( entry->d_name ) )
        {
            if( 0 != unlink( path ) && ENOENT != errno )
            {
                ESP_LOGW( TAG, "could not remove stale temporary %s", path );
            }
        }
        else if( a_recurse_one_level && S_ISDIR( info.st_mode ) )
        {
            cleanup_temp_directory( path, false );
        }
    }
    closedir( directory );
}

static esp_err_t hash_string( const char *a_value, char a_hex[ PERSISTENT_HASH_HEX_LENGTH + 1U ] )
{
    if( NULL == a_value || '\0' == a_value[ 0 ] )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = strnlen( a_value, RFSCOUT_PERSISTENT_KEY_MAX_BYTES + 1U );
    if( 0U == length || length > RFSCOUT_PERSISTENT_KEY_MAX_BYTES )
    {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t digest[ PERSISTENT_HASH_BYTES ];
    size_t digest_length = 0U;
    const psa_status_t status = psa_hash_compute( PSA_ALG_SHA_256,
                                                  (const uint8_t *)a_value,
                                                  length,
                                                  digest,
                                                  sizeof( digest ),
                                                  &digest_length );
    if( PSA_SUCCESS != status || sizeof( digest ) != digest_length )
    {
        return ESP_FAIL;
    }
    for( size_t index = 0U; index < PERSISTENT_HASH_BYTES; ++index )
    {
        (void)snprintf( &a_hex[ index * 2U ], 3U, "%02x", digest[ index ] );
    }
    a_hex[ PERSISTENT_HASH_HEX_LENGTH ] = '\0';
    return ESP_OK;
}

static esp_err_t app_scope( rfscout_app_id_t a_app, char *a_scope )
{
    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_app );
    if( NULL == descriptor || NULL == descriptor->persistence_key ||
        '\0' == descriptor->persistence_key[ 0 ] )
    {
        return ESP_ERR_INVALID_ARG;
    }

    char hash[ PERSISTENT_HASH_HEX_LENGTH + 1U ];
    esp_err_t result = hash_string( descriptor->persistence_key, hash );
    if( ESP_OK != result )
    {
        return result;
    }
    const int length = snprintf( a_scope, PERSISTENT_PATH_MAX, "%s/%s", PERSISTENT_APP_ROOT, hash );
    return ( length < 0 || length >= (int)PERSISTENT_PATH_MAX ) ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

static esp_err_t
build_record_paths( const char *a_scope, const char *a_key, char *a_path, char *a_temp_path )
{
    char hash[ PERSISTENT_HASH_HEX_LENGTH + 1U ];
    esp_err_t result = hash_string( a_key, hash );
    if( ESP_OK != result )
    {
        return result;
    }
    const int path_length = snprintf( a_path, PERSISTENT_PATH_MAX, "%s/%s", a_scope, hash );
    const int temp_length = snprintf( a_temp_path, PERSISTENT_PATH_MAX, "%s.tmp", a_path );
    if( path_length < 0 || temp_length < 0 || path_length >= (int)PERSISTENT_PATH_MAX ||
        temp_length >= (int)PERSISTENT_PATH_MAX )
    {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static bool store_lock( void )
{
    return NULL != store_mutex && pdTRUE == xSemaphoreTake( store_mutex, portMAX_DELAY );
}

static void store_unlock( void )
{
    if( NULL != store_mutex )
    {
        (void)xSemaphoreGive( store_mutex );
    }
}

static esp_err_t record_write( const char *a_scope,
                               bool a_create_scope,
                               const char *a_key,
                               const void *a_data,
                               size_t a_length )
{
    if( !available )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( a_length > RFSCOUT_PERSISTENT_RECORD_MAX_BYTES )
    {
        return ESP_ERR_INVALID_SIZE;
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
    if( !store_lock() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    if( a_create_scope )
    {
        result = ensure_directory( a_scope );
        if( ESP_OK != result )
        {
            store_unlock();
            return result;
        }
    }

    (void)unlink( temp_path );
    FILE *file = fopen( temp_path, "wb" );
    if( NULL == file )
    {
        const int saved_errno = errno;
        store_unlock();
        return ( ENOSPC == saved_errno ) ? ESP_ERR_NO_MEM : ESP_FAIL;
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
        store_unlock();
        return ( ENOSPC == saved_errno ) ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    if( 0 != rename( temp_path, path ) )
    {
        const int saved_errno = errno;
        (void)unlink( temp_path );
        store_unlock();
        return ( ENOSPC == saved_errno ) ? ESP_ERR_NO_MEM : ESP_FAIL;
    }

    store_unlock();
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
    if( !store_lock() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen( path, "rb" );
    if( NULL == file )
    {
        const int saved_errno = errno;
        store_unlock();
        return ( ENOENT == saved_errno ) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    if( 0 != fseek( file, 0, SEEK_END ) )
    {
        fclose( file );
        store_unlock();
        return ESP_FAIL;
    }
    const long length = ftell( file );
    if( length < 0L || length > (long)RFSCOUT_PERSISTENT_RECORD_MAX_BYTES ||
        0 != fseek( file, 0, SEEK_SET ) )
    {
        fclose( file );
        store_unlock();
        return ( length > (long)RFSCOUT_PERSISTENT_RECORD_MAX_BYTES ) ? ESP_ERR_INVALID_SIZE
                                                                      : ESP_FAIL;
    }
    *a_length = (size_t)length;
    if( NULL == a_data && 0U == a_capacity )
    {
        fclose( file );
        store_unlock();
        return ESP_OK;
    }
    if( NULL == a_data || (size_t)length > a_capacity )
    {
        fclose( file );
        store_unlock();
        return ESP_ERR_INVALID_SIZE;
    }
    if( length > 0L && (size_t)length != fread( a_data, 1U, (size_t)length, file ) )
    {
        fclose( file );
        store_unlock();
        return ESP_FAIL;
    }
    fclose( file );
    store_unlock();
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
    esp_err_t result = build_record_paths( a_scope, a_key, path, temp_path );
    if( ESP_OK != result )
    {
        return result;
    }
    if( !store_lock() )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( 0 == unlink( path ) )
    {
        store_unlock();
        return ESP_OK;
    }
    const int saved_errno = errno;
    store_unlock();
    return ( ENOENT == saved_errno ) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
}

static bool record_exists( const char *a_scope, const char *a_key )
{
    if( !available )
    {
        return false;
    }
    char path[ PERSISTENT_PATH_MAX ];
    char temp_path[ PERSISTENT_PATH_MAX ];
    if( ESP_OK != build_record_paths( a_scope, a_key, path, temp_path ) || !store_lock() )
    {
        return false;
    }
    const bool exists = 0 == access( path, F_OK );
    store_unlock();
    return exists;
}

static esp_err_t record_load( const char *a_scope,
                              const char *a_key,
                              void *a_record,
                              size_t a_length,
                              persistent_record_valid_fn a_valid )
{
    if( NULL == a_record || 0U == a_length )
    {
        return ESP_ERR_INVALID_ARG;
    }
    size_t length = 0U;
    const esp_err_t result = record_read( a_scope, a_key, a_record, a_length, &length );
    if( ESP_ERR_NOT_FOUND == result || ESP_ERR_INVALID_STATE == result ||
        ESP_ERR_INVALID_ARG == result )
    {
        return result;
    }
    if( ESP_OK == result && a_length == length && ( NULL == a_valid || a_valid( a_record ) ) )
    {
        return ESP_OK;
    }
    ESP_LOGW( TAG, "removing rejected record %s", a_key );
    (void)record_remove( a_scope, a_key );
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t persistent_store_init( void )
{
    if( initialized )
    {
        return available ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    initialized = true;
    store_mutex = xSemaphoreCreateMutexStatic( &store_mutex_buffer );
    if( NULL == store_mutex )
    {
        return ESP_ERR_NO_MEM;
    }
    if( PSA_SUCCESS != psa_crypto_init() )
    {
        return ESP_FAIL;
    }

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

    cleanup_temp_directory( PERSISTENT_SYSTEM_ROOT, false );
    cleanup_temp_directory( PERSISTENT_APP_ROOT, true );

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
    return record_write( PERSISTENT_SYSTEM_ROOT, false, a_key, a_data, a_length );
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

esp_err_t system_store_load( const char *a_key,
                             void *a_record,
                             size_t a_length,
                             persistent_record_valid_fn a_valid )
{
    return record_load( PERSISTENT_SYSTEM_ROOT, a_key, a_record, a_length, a_valid );
}

esp_err_t persistent_store_app_write( rfscout_app_id_t a_app,
                                      const char *a_key,
                                      const void *a_data,
                                      size_t a_length )
{
    char scope[ PERSISTENT_PATH_MAX ];
    const esp_err_t result = app_scope( a_app, scope );
    return ( ESP_OK == result ) ? record_write( scope, true, a_key, a_data, a_length ) : result;
}

esp_err_t persistent_store_app_read(
    rfscout_app_id_t a_app, const char *a_key, void *a_data, size_t a_capacity, size_t *a_length )
{
    char scope[ PERSISTENT_PATH_MAX ];
    const esp_err_t result = app_scope( a_app, scope );
    return ( ESP_OK == result ) ? record_read( scope, a_key, a_data, a_capacity, a_length )
                                : result;
}

esp_err_t persistent_store_app_load( rfscout_app_id_t a_app,
                                     const char *a_key,
                                     void *a_record,
                                     size_t a_length,
                                     persistent_record_valid_fn a_valid )
{
    char scope[ PERSISTENT_PATH_MAX ];
    const esp_err_t result = app_scope( a_app, scope );
    return ( ESP_OK == result ) ? record_load( scope, a_key, a_record, a_length, a_valid ) : result;
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

esp_err_t app_store_context_current( rfscout_app_store_context_t *a_context )
{
    if( NULL == a_context )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const rfscout_app_id_t app = app_session_app();
    if( app < 0 || app >= RFSCOUT_APP_COUNT )
    {
        return ESP_ERR_INVALID_STATE;
    }
    a_context->app = app;
    a_context->generation = app_session_generation();
    return ESP_OK;
}

static bool app_context_current( const rfscout_app_store_context_t *a_context )
{
    return NULL != a_context && a_context->app >= 0 && a_context->app < RFSCOUT_APP_COUNT &&
           a_context->app == app_session_app() && a_context->generation == app_session_generation();
}

esp_err_t app_store_write_context( const rfscout_app_store_context_t *a_context,
                                   const char *a_key,
                                   const void *a_data,
                                   size_t a_length )
{
    return app_context_current( a_context )
               ? persistent_store_app_write( a_context->app, a_key, a_data, a_length )
               : ESP_ERR_INVALID_STATE;
}

esp_err_t app_store_read_context( const rfscout_app_store_context_t *a_context,
                                  const char *a_key,
                                  void *a_data,
                                  size_t a_capacity,
                                  size_t *a_length )
{
    return app_context_current( a_context )
               ? persistent_store_app_read( a_context->app, a_key, a_data, a_capacity, a_length )
               : ESP_ERR_INVALID_STATE;
}

esp_err_t app_store_remove_context( const rfscout_app_store_context_t *a_context,
                                    const char *a_key )
{
    return app_context_current( a_context ) ? persistent_store_app_remove( a_context->app, a_key )
                                            : ESP_ERR_INVALID_STATE;
}

bool app_store_exists_context( const rfscout_app_store_context_t *a_context, const char *a_key )
{
    return app_context_current( a_context ) && persistent_store_app_exists( a_context->app, a_key );
}

esp_err_t app_store_write( const char *a_key, const void *a_data, size_t a_length )
{
    rfscout_app_store_context_t context;
    const esp_err_t result = app_store_context_current( &context );
    return ESP_OK == result ? app_store_write_context( &context, a_key, a_data, a_length ) : result;
}

esp_err_t app_store_read( const char *a_key, void *a_data, size_t a_capacity, size_t *a_length )
{
    rfscout_app_store_context_t context;
    const esp_err_t result = app_store_context_current( &context );
    return ESP_OK == result
               ? app_store_read_context( &context, a_key, a_data, a_capacity, a_length )
               : result;
}

esp_err_t app_store_remove( const char *a_key )
{
    rfscout_app_store_context_t context;
    const esp_err_t result = app_store_context_current( &context );
    return ESP_OK == result ? app_store_remove_context( &context, a_key ) : result;
}

bool app_store_exists( const char *a_key )
{
    rfscout_app_store_context_t context;
    return ESP_OK == app_store_context_current( &context ) &&
           app_store_exists_context( &context, a_key );
}

esp_err_t app_store_load( const char *a_key,
                          void *a_record,
                          size_t a_length,
                          persistent_record_valid_fn a_valid )
{
    rfscout_app_store_context_t context;
    char scope[ PERSISTENT_PATH_MAX ];
    esp_err_t result = app_store_context_current( &context );
    if( ESP_OK == result )
    {
        result = app_scope( context.app, scope );
    }
    return ( ESP_OK == result ) ? record_load( scope, a_key, a_record, a_length, a_valid ) : result;
}
