/**
    media_store.c : RFScout removable media store
    Description: Maps owner/collection/name media addresses onto the private SD mount and arbitrates
                 every filesystem transaction against raw card-health probes.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "media_store.h"

#include "app_framework.h"
#include "app_session.h"
#include "storage.h"
#include "storage_internal.h"

#include "esp_log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MEDIA_IO_TIMEOUT_MS 10000U
#define MEDIA_PATH_MAX                                                                             \
    ( sizeof( RFSCOUT_STORAGE_MOUNT_POINT ) + RFSCOUT_MEDIA_OWNER_MAX +                            \
      RFSCOUT_MEDIA_COLLECTION_MAX + RFSCOUT_MEDIA_NAME_MAX + 4U )
#define MEDIA_SLOT_COUNT RFSCOUT_STORAGE_MAX_OPEN_FILES

/** @brief One pooled open file and the owner that may legitimately reach it. */
typedef struct
{
    FILE *file;
    uint32_t serial;
    bool system;
    rfscout_app_id_t app;
} media_slot_t;

static const char *TAG = "media_store";
static media_slot_t slots[ MEDIA_SLOT_COUNT ];
static uint32_t next_serial;

/** @brief Translate a filesystem errno into the documented media-store error vocabulary. */
static esp_err_t errno_result( int a_errno )
{
    switch( a_errno )
    {
        case ENOENT:
            return ESP_ERR_NOT_FOUND;
        case EEXIST:
            return ESP_ERR_NOT_ALLOWED;
        case ENOSPC:
            return ESP_ERR_NO_MEM;
        case ENAMETOOLONG:
            return ESP_ERR_INVALID_SIZE;
        default:
            return ESP_FAIL;
    }
}

/** @brief Validate an owner/collection token: 1..a_max of [a-z0-9_-]. */
static bool token_valid( const char *a_token, size_t a_max )
{
    if( NULL == a_token )
    {
        return false;
    }
    const size_t length = strnlen( a_token, a_max + 1U );
    if( 0U == length || length > a_max )
    {
        return false;
    }
    for( size_t index = 0U; index < length; ++index )
    {
        const char character = a_token[ index ];
        const bool allowed = ( character >= 'a' && character <= 'z' ) ||
                             ( character >= '0' && character <= '9' ) || '_' == character ||
                             '-' == character;
        if( !allowed )
        {
            return false;
        }
    }
    return true;
}

bool media_name_valid( const char *a_name )
{
    if( NULL == a_name )
    {
        return false;
    }
    const size_t length = strnlen( a_name, RFSCOUT_MEDIA_NAME_MAX + 1U );
    if( 0U == length || length > RFSCOUT_MEDIA_NAME_MAX || '.' == a_name[ 0 ] ||
        ' ' == a_name[ length - 1U ] || '.' == a_name[ length - 1U ] )
    {
        return false;
    }
    for( size_t index = 0U; index < length; ++index )
    {
        const unsigned char character = (unsigned char)a_name[ index ];
        if( character < 0x20U || 0x7fU == character || NULL != strchr( "/\\:*?\"<>|", character ) )
        {
            return false;
        }
    }
    return true;
}

bool media_owner_name_valid( const char *a_owner )
{
    return token_valid( a_owner, RFSCOUT_MEDIA_OWNER_MAX ) &&
           0 != strcmp( a_owner, RFSCOUT_MEDIA_SYSTEM_OWNER );
}

/** @brief Return the owner directory name for a resolved collection, or NULL when invalid. */
static const char *collection_owner( const rfscout_media_collection_t *a_collection )
{
    if( a_collection->system )
    {
        return RFSCOUT_MEDIA_SYSTEM_OWNER;
    }
    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_collection->app );
    if( NULL == descriptor || !media_owner_name_valid( descriptor->persistence_key ) )
    {
        return NULL;
    }
    return descriptor->persistence_key;
}

/** @brief Check that a collection is well formed and, for apps, still belongs to the live session. */
static esp_err_t collection_check( const rfscout_media_collection_t *a_collection )
{
    if( NULL == a_collection ||
        !token_valid( a_collection->collection, RFSCOUT_MEDIA_COLLECTION_MAX ) ||
        NULL == collection_owner( a_collection ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( !a_collection->system && ( ( a_collection->app != app_session_app() ) ||
                                   ( a_collection->generation != app_session_generation() ) ) )
    {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/** @brief Build the owner directory, collection directory, or file path for a collection. */
static bool build_path( const rfscout_media_collection_t *a_collection,
                        const char *a_name,
                        bool a_owner_only,
                        char *a_path,
                        size_t a_capacity )
{
    const char *owner = collection_owner( a_collection );
    if( NULL == owner )
    {
        return false;
    }

    int length;
    if( a_owner_only )
    {
        length = snprintf( a_path, a_capacity, RFSCOUT_STORAGE_MOUNT_POINT "/%s", owner );
    }
    else if( NULL == a_name )
    {
        length = snprintf( a_path,
                           a_capacity,
                           RFSCOUT_STORAGE_MOUNT_POINT "/%s/%s",
                           owner,
                           a_collection->collection );
    }
    else
    {
        length = snprintf( a_path,
                           a_capacity,
                           RFSCOUT_STORAGE_MOUNT_POINT "/%s/%s/%s",
                           owner,
                           a_collection->collection,
                           a_name );
    }
    return length > 0 && (size_t)length < a_capacity;
}

/** @brief Create one directory when missing; an existing non-directory is an error. */
static esp_err_t ensure_directory( const char *a_path )
{
    struct stat info;
    if( 0 == stat( a_path, &info ) )
    {
        return S_ISDIR( info.st_mode ) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    if( 0 == mkdir( a_path, 0775 ) || EEXIST == errno )
    {
        return ESP_OK;
    }
    ESP_LOGE( TAG, "directory creation failed for %s: errno=%d", a_path, errno );
    return errno_result( errno );
}

/** @brief Create the owner and collection directories for a write-mode open. */
static esp_err_t ensure_collection_directories( const rfscout_media_collection_t *a_collection )
{
    char path[ MEDIA_PATH_MAX ];
    if( !build_path( a_collection, NULL, true, path, sizeof( path ) ) )
    {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t result = ensure_directory( path );
    if( ESP_OK != result )
    {
        return result;
    }
    if( !build_path( a_collection, NULL, false, path, sizeof( path ) ) )
    {
        return ESP_ERR_INVALID_SIZE;
    }
    return ensure_directory( path );
}

/** @brief Acquire storage arbitration for one bounded media transaction. */
static bool media_lock( void )
{
    if( storage_io_acquire( MEDIA_IO_TIMEOUT_MS ) )
    {
        return true;
    }
    ESP_LOGW( TAG, "SD arbitration timed out after %u ms", (unsigned)MEDIA_IO_TIMEOUT_MS );
    return false;
}

/** @brief Resolve a handle to its live slot; caller holds arbitration. */
static media_slot_t *slot_for( const rfscout_media_file_t *a_file )
{
    if( NULL == a_file || a_file->slot >= MEDIA_SLOT_COUNT || 0U == a_file->serial )
    {
        return NULL;
    }
    media_slot_t *slot = &slots[ a_file->slot ];
    if( NULL == slot->file || slot->serial != a_file->serial )
    {
        return NULL;
    }
    return slot;
}

/** @brief Close one slot and invalidate every outstanding copy of its handle. */
static int close_slot( media_slot_t *a_slot )
{
    int result = 0;
    if( 0 != fflush( a_slot->file ) )
    {
        result = errno;
    }
    if( 0 != fclose( a_slot->file ) && 0 == result )
    {
        result = errno;
    }
    a_slot->file = NULL;
    a_slot->serial = 0U;
    return result;
}

bool media_store_available( void )
{
    return storage_is_available();
}

esp_err_t media_store_probe( void )
{
    if( !storage_is_available() && ( ESP_OK != storage_init() ) )
    {
        return ESP_ERR_NOT_FOUND;
    }
    return ( ESP_OK == storage_probe_card() ) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t media_store_space( uint64_t *a_total_bytes, uint64_t *a_free_bytes )
{
    return storage_get_capacity( a_total_bytes, a_free_bytes );
}

esp_err_t media_system_collection( const char *a_collection,
                                   rfscout_media_collection_t *a_resolved )
{
    if( NULL == a_resolved || !token_valid( a_collection, RFSCOUT_MEDIA_COLLECTION_MAX ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset( a_resolved, 0, sizeof( *a_resolved ) );
    a_resolved->system = true;
    a_resolved->app = RFSCOUT_APP_COUNT;
    strlcpy( a_resolved->collection, a_collection, sizeof( a_resolved->collection ) );
    return ESP_OK;
}

esp_err_t media_app_collection( const char *a_collection, rfscout_media_collection_t *a_resolved )
{
    if( NULL == a_resolved || !token_valid( a_collection, RFSCOUT_MEDIA_COLLECTION_MAX ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const rfscout_app_id_t app = app_session_app();
    if( app < 0 || app >= RFSCOUT_APP_COUNT )
    {
        return ESP_ERR_INVALID_STATE;
    }
    memset( a_resolved, 0, sizeof( *a_resolved ) );
    a_resolved->system = false;
    a_resolved->app = app;
    a_resolved->generation = app_session_generation();
    strlcpy( a_resolved->collection, a_collection, sizeof( a_resolved->collection ) );
    return NULL != collection_owner( a_resolved ) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

/** @brief Open a FILE stream for the requested media semantics. */
static FILE *open_stream( const char *a_path, rfscout_media_open_mode_t a_mode )
{
    switch( a_mode )
    {
        case RFSCOUT_MEDIA_OPEN_READ:
            return fopen( a_path, "rb" );
        case RFSCOUT_MEDIA_OPEN_CREATE_NEW:
        {
            const int descriptor = open( a_path, O_CREAT | O_EXCL | O_RDWR, 0664 );
            if( descriptor < 0 )
            {
                return NULL;
            }
            FILE *file = fdopen( descriptor, "wb+" );
            if( NULL == file )
            {
                const int saved = errno;
                (void)close( descriptor );
                (void)unlink( a_path );
                errno = saved;
            }
            return file;
        }
        case RFSCOUT_MEDIA_OPEN_APPEND:
            return fopen( a_path, "ab+" );
        case RFSCOUT_MEDIA_OPEN_REPLACE:
            return fopen( a_path, "wb+" );
        default:
            errno = EINVAL;
            return NULL;
    }
}

esp_err_t media_open( const rfscout_media_collection_t *a_collection,
                      const char *a_name,
                      rfscout_media_open_mode_t a_mode,
                      rfscout_media_file_t *a_file )
{
    if( NULL == a_file || !media_name_valid( a_name ) || a_mode > RFSCOUT_MEDIA_OPEN_REPLACE )
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset( a_file, 0, sizeof( *a_file ) );
    if( !media_lock() )
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = collection_check( a_collection );
    if( ESP_OK == result && !storage_is_available() )
    {
        result = ESP_ERR_INVALID_STATE;
    }

    media_slot_t *slot = NULL;
    size_t slot_index = 0U;
    if( ESP_OK == result )
    {
        for( ; slot_index < MEDIA_SLOT_COUNT; ++slot_index )
        {
            if( NULL == slots[ slot_index ].file )
            {
                slot = &slots[ slot_index ];
                break;
            }
        }
        if( NULL == slot )
        {
            ESP_LOGW( TAG, "no free media handle" );
            result = ESP_ERR_NO_MEM;
        }
    }

    if( ESP_OK == result && RFSCOUT_MEDIA_OPEN_READ != a_mode )
    {
        result = ensure_collection_directories( a_collection );
    }

    char path[ MEDIA_PATH_MAX ];
    if( ESP_OK == result && !build_path( a_collection, a_name, false, path, sizeof( path ) ) )
    {
        result = ESP_ERR_INVALID_SIZE;
    }

    if( ESP_OK == result )
    {
        FILE *file = open_stream( path, a_mode );
        if( NULL == file )
        {
            result = errno_result( errno );
        }
        else
        {
            ++next_serial;
            if( 0U == next_serial )
            {
                ++next_serial;
            }
            slot->file = file;
            slot->serial = next_serial;
            slot->system = a_collection->system;
            slot->app = a_collection->app;
            a_file->slot = (uint8_t)slot_index;
            a_file->serial = slot->serial;
        }
    }

    storage_io_release();
    return result;
}

esp_err_t
media_read( const rfscout_media_file_t *a_file, void *a_data, size_t a_capacity, size_t *a_read )
{
    if( NULL == a_read || ( NULL == a_data && 0U != a_capacity ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    *a_read = 0U;
    if( !media_lock() )
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;
    media_slot_t *slot = slot_for( a_file );
    if( NULL == slot || !storage_is_available() )
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else if( a_capacity > 0U )
    {
        *a_read = fread( a_data, 1U, a_capacity, slot->file );
        if( *a_read < a_capacity && 0 != ferror( slot->file ) )
        {
            clearerr( slot->file );
            result = ESP_FAIL;
        }
    }

    storage_io_release();
    return result;
}

esp_err_t media_write( const rfscout_media_file_t *a_file, const void *a_data, size_t a_length )
{
    if( NULL == a_data && 0U != a_length )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( !media_lock() )
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;
    media_slot_t *slot = slot_for( a_file );
    if( NULL == slot || !storage_is_available() )
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else if( a_length > 0U && a_length != fwrite( a_data, 1U, a_length, slot->file ) )
    {
        result = errno_result( errno );
        clearerr( slot->file );
    }

    storage_io_release();
    return result;
}

esp_err_t media_seek( const rfscout_media_file_t *a_file,
                      int64_t a_offset,
                      rfscout_media_seek_origin_t a_origin )
{
    if( a_origin > RFSCOUT_MEDIA_SEEK_END || a_offset > (int64_t)LONG_MAX ||
        a_offset < (int64_t)LONG_MIN )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( !media_lock() )
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;
    media_slot_t *slot = slot_for( a_file );
    if( NULL == slot || !storage_is_available() )
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else
    {
        const int whence = ( RFSCOUT_MEDIA_SEEK_END == a_origin ) ? SEEK_END : SEEK_SET;
        if( 0 != fseek( slot->file, (long)a_offset, whence ) )
        {
            result = errno_result( errno );
        }
    }

    storage_io_release();
    return result;
}

esp_err_t media_size( const rfscout_media_file_t *a_file, uint64_t *a_size_bytes )
{
    if( NULL == a_size_bytes )
    {
        return ESP_ERR_INVALID_ARG;
    }
    *a_size_bytes = 0U;
    if( !media_lock() )
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;
    media_slot_t *slot = slot_for( a_file );
    if( NULL == slot || !storage_is_available() )
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else
    {
        struct stat info;
        if( 0 != fflush( slot->file ) || 0 != fstat( fileno( slot->file ), &info ) ||
            info.st_size < 0 )
        {
            result = errno_result( errno );
        }
        else
        {
            *a_size_bytes = (uint64_t)info.st_size;
        }
    }

    storage_io_release();
    return result;
}

esp_err_t media_truncate( const rfscout_media_file_t *a_file, uint64_t a_size_bytes )
{
    if( a_size_bytes > (uint64_t)INT32_MAX )
    {
        return ESP_ERR_INVALID_SIZE;
    }
    if( !media_lock() )
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;
    media_slot_t *slot = slot_for( a_file );
    if( NULL == slot || !storage_is_available() )
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else if( 0 != fflush( slot->file ) ||
             0 != ftruncate( fileno( slot->file ), (off_t)a_size_bytes ) )
    {
        result = errno_result( errno );
    }

    storage_io_release();
    return result;
}

esp_err_t media_sync( const rfscout_media_file_t *a_file )
{
    if( !media_lock() )
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;
    media_slot_t *slot = slot_for( a_file );
    if( NULL == slot || !storage_is_available() )
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else if( 0 != fflush( slot->file ) || 0 != fsync( fileno( slot->file ) ) )
    {
        result = errno_result( errno );
    }

    storage_io_release();
    return result;
}

esp_err_t media_close( rfscout_media_file_t *a_file )
{
    if( NULL == a_file )
    {
        return ESP_ERR_INVALID_ARG;
    }
    /* Closing must always release the slot, so wait for arbitration rather than leak it. */
    if( !storage_io_acquire( RFSCOUT_STORAGE_WAIT_FOREVER ) )
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;
    media_slot_t *slot = slot_for( a_file );
    if( NULL == slot )
    {
        result = ESP_ERR_INVALID_STATE;
    }
    else
    {
        const int close_errno = close_slot( slot );
        if( 0 != close_errno )
        {
            result = errno_result( close_errno );
        }
    }
    memset( a_file, 0, sizeof( *a_file ) );

    storage_io_release();
    return result;
}

/** @brief Validate, lock, and build a file path for a by-name collection operation. */
static esp_err_t begin_named_operation( const rfscout_media_collection_t *a_collection,
                                        const char *a_name,
                                        char *a_path,
                                        size_t a_capacity )
{
    if( ( NULL != a_name ) && !media_name_valid( a_name ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( !media_lock() )
    {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = collection_check( a_collection );
    if( ESP_OK == result && !storage_is_available() )
    {
        result = ESP_ERR_INVALID_STATE;
    }
    if( ESP_OK == result && !build_path( a_collection, a_name, false, a_path, a_capacity ) )
    {
        result = ESP_ERR_INVALID_SIZE;
    }
    if( ESP_OK != result )
    {
        storage_io_release();
    }
    return result;
}

esp_err_t media_stat( const rfscout_media_collection_t *a_collection,
                      const char *a_name,
                      rfscout_media_entry_t *a_entry )
{
    if( NULL == a_entry || NULL == a_name )
    {
        return ESP_ERR_INVALID_ARG;
    }
    memset( a_entry, 0, sizeof( *a_entry ) );

    char path[ MEDIA_PATH_MAX ];
    esp_err_t result = begin_named_operation( a_collection, a_name, path, sizeof( path ) );
    if( ESP_OK != result )
    {
        return result;
    }

    struct stat info;
    if( 0 != stat( path, &info ) )
    {
        result = errno_result( errno );
    }
    else if( !S_ISREG( info.st_mode ) || info.st_size < 0 )
    {
        result = ESP_ERR_NOT_FOUND;
    }
    else
    {
        strlcpy( a_entry->name, a_name, sizeof( a_entry->name ) );
        a_entry->size_bytes = (uint64_t)info.st_size;
        a_entry->modified_time = (int64_t)info.st_mtime;
    }

    storage_io_release();
    return result;
}

esp_err_t media_remove( const rfscout_media_collection_t *a_collection, const char *a_name )
{
    if( NULL == a_name )
    {
        return ESP_ERR_INVALID_ARG;
    }

    char path[ MEDIA_PATH_MAX ];
    esp_err_t result = begin_named_operation( a_collection, a_name, path, sizeof( path ) );
    if( ESP_OK != result )
    {
        return result;
    }

    if( 0 != unlink( path ) )
    {
        result = errno_result( errno );
    }

    storage_io_release();
    return result;
}

esp_err_t media_list( const rfscout_media_collection_t *a_collection,
                      rfscout_media_list_fn a_visit,
                      void *a_context )
{
    if( NULL == a_visit )
    {
        return ESP_ERR_INVALID_ARG;
    }

    char directory_path[ MEDIA_PATH_MAX ];
    esp_err_t result =
        begin_named_operation( a_collection, NULL, directory_path, sizeof( directory_path ) );
    if( ESP_OK != result )
    {
        return result;
    }

    DIR *directory = opendir( directory_path );
    if( NULL == directory )
    {
        /* A collection that has never been written is an empty collection, not an error. */
        result = ( ENOENT == errno ) ? ESP_OK : errno_result( errno );
        storage_io_release();
        return result;
    }

    struct dirent *item;
    while( NULL != ( item = readdir( directory ) ) )
    {
        if( !media_name_valid( item->d_name ) )
        {
            continue;
        }

        char path[ MEDIA_PATH_MAX ];
        struct stat info;
        if( !build_path( a_collection, item->d_name, false, path, sizeof( path ) ) ||
            0 != stat( path, &info ) || !S_ISREG( info.st_mode ) || info.st_size < 0 )
        {
            continue;
        }

        rfscout_media_entry_t entry;
        strlcpy( entry.name, item->d_name, sizeof( entry.name ) );
        entry.size_bytes = (uint64_t)info.st_size;
        entry.modified_time = (int64_t)info.st_mtime;
        if( !a_visit( &entry, a_context ) )
        {
            break;
        }
    }
    closedir( directory );

    storage_io_release();
    return ESP_OK;
}

size_t media_store_close_app( rfscout_app_id_t a_app )
{
    if( !storage_io_acquire( RFSCOUT_STORAGE_WAIT_FOREVER ) )
    {
        return 0U;
    }

    size_t closed = 0U;
    for( size_t index = 0U; index < MEDIA_SLOT_COUNT; ++index )
    {
        media_slot_t *slot = &slots[ index ];
        if( NULL == slot->file || slot->system || slot->app != a_app )
        {
            continue;
        }
        (void)close_slot( slot );
        ++closed;
    }

    storage_io_release();
    if( closed > 0U )
    {
        ESP_LOGE(
            TAG, "force-closed %u media handle(s) left open by app=%d", (unsigned)closed, a_app );
    }
    return closed;
}
