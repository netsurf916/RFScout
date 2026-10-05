/**
    media_store.h : RFScout removable media store
    Description: Owner/collection-scoped SD file access with arbitrated, generation-checked handles.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_MEDIA_STORE_H_
#define _RFSCOUT_MEDIA_STORE_H_

#include "app_descriptor.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Reserved owner directory for platform-owned media collections. */
#define RFSCOUT_MEDIA_SYSTEM_OWNER "system"
/** Maximum owner name length, excluding the terminator. */
#define RFSCOUT_MEDIA_OWNER_MAX 31U
/** Maximum collection name length, excluding the terminator. */
#define RFSCOUT_MEDIA_COLLECTION_MAX 31U
/** Maximum file name length, excluding the terminator. */
#define RFSCOUT_MEDIA_NAME_MAX 63U

/**
 * @brief One resolved owner-local media collection.
 * @details Obtain with media_system_collection() or media_app_collection(); do not populate the
 *          fields directly. The value is small and may be copied. An app collection captures the
 *          app-session generation, so it becomes stale (operations return ESP_ERR_INVALID_STATE)
 *          once that app session closes.
 */
typedef struct
{
    bool system;
    rfscout_app_id_t app;
    uint32_t generation;
    char collection[ RFSCOUT_MEDIA_COLLECTION_MAX + 1U ];
} rfscout_media_collection_t;

/**
 * @brief Opaque generation-checked media file handle.
 * @details A zero-initialized handle is invalid. Closing a handle, or the platform force-closing an
 *          exited app's handle, invalidates every copy of it; later operations return
 *          ESP_ERR_INVALID_STATE rather than aliasing a reused slot.
 */
typedef struct
{
    uint8_t slot;
    uint32_t serial;
} rfscout_media_file_t;

/** @brief How media_open() treats an existing or missing file. */
typedef enum
{
    /** Read-only; the file must exist. */
    RFSCOUT_MEDIA_OPEN_READ = 0,
    /** Read/write; the file must not already exist (ESP_ERR_NOT_ALLOWED when it does). */
    RFSCOUT_MEDIA_OPEN_CREATE_NEW,
    /** Read/append; created when missing. Writes always append at the current end. */
    RFSCOUT_MEDIA_OPEN_APPEND,
    /** Read/write; created when missing and truncated when present. */
    RFSCOUT_MEDIA_OPEN_REPLACE
} rfscout_media_open_mode_t;

/** @brief Reference point for media_seek(). */
typedef enum
{
    RFSCOUT_MEDIA_SEEK_START = 0,
    RFSCOUT_MEDIA_SEEK_END
} rfscout_media_seek_origin_t;

/** @brief Metadata for one file in a media collection. */
typedef struct
{
    char name[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
    uint64_t size_bytes;
    int64_t modified_time;
} rfscout_media_entry_t;

/**
 * @brief Visit one collection entry during media_list().
 * @details Called with the storage arbitration held. The callback may call other media operations
 *          from the same task but should stay short because it delays other SD users.
 * @return true to continue listing, false to stop early.
 */
typedef bool ( *rfscout_media_list_fn )( const rfscout_media_entry_t *a_entry, void *a_context );

/** @brief Return whether mounted SD media is currently healthy and usable. */
bool media_store_available( void );

/**
 * @brief Retry-safe SD presence probe for hot-plug dependency checks.
 * @details Mounts the card when no card has ever mounted, then verifies the mounted card responds.
 *          It never remounts media that was latched lost.
 * @return ESP_OK when media is present and healthy, otherwise ESP_ERR_NOT_FOUND.
 */
esp_err_t media_store_probe( void );

/**
 * @brief Query SD filesystem capacity.
 * @param a_total_bytes Receives total bytes when non-NULL.
 * @param a_free_bytes Receives free bytes when non-NULL.
 */
esp_err_t media_store_space( uint64_t *a_total_bytes, uint64_t *a_free_bytes );

/**
 * @brief Resolve a privileged platform-owned collection under the reserved system owner.
 * @details Platform services only. Applications use media_app_collection().
 */
esp_err_t media_system_collection( const char *a_collection,
                                   rfscout_media_collection_t *a_resolved );

/**
 * @brief Resolve a collection owned by the current application session.
 * @details The owner directory is the session app's descriptor persistence key. Capture the result
 *          before handing it to asynchronous work owned by that session.
 */
esp_err_t media_app_collection( const char *a_collection, rfscout_media_collection_t *a_resolved );

/**
 * @brief Open one file in a collection.
 * @details Write modes create the owner and collection directories on demand. Reads never create
 *          directories. As with stdio streams, call media_seek() (or media_sync()) between a read
 *          and a following write on the same handle, and between a write and a following read.
 * @return ESP_OK, ESP_ERR_NOT_FOUND for a missing file in READ mode, ESP_ERR_NOT_ALLOWED when
 *         CREATE_NEW finds an existing file, ESP_ERR_NO_MEM when no handle is free,
 *         ESP_ERR_INVALID_STATE when media is unavailable or the collection is stale, or another
 *         ESP-IDF error.
 */
esp_err_t media_open( const rfscout_media_collection_t *a_collection,
                      const char *a_name,
                      rfscout_media_open_mode_t a_mode,
                      rfscout_media_file_t *a_file );

/**
 * @brief Read up to a_capacity bytes at the current position.
 * @param a_read Receives the byte count; zero with ESP_OK means end of file.
 */
esp_err_t
media_read( const rfscout_media_file_t *a_file, void *a_data, size_t a_capacity, size_t *a_read );

/** @brief Write exactly a_length bytes or return an error. */
esp_err_t media_write( const rfscout_media_file_t *a_file, const void *a_data, size_t a_length );

/** @brief Move the current position relative to the start or end of the file. */
esp_err_t media_seek( const rfscout_media_file_t *a_file,
                      int64_t a_offset,
                      rfscout_media_seek_origin_t a_origin );

/** @brief Return the current file size including buffered writes. */
esp_err_t media_size( const rfscout_media_file_t *a_file, uint64_t *a_size_bytes );

/** @brief Flush buffered writes and truncate the file to a_size_bytes. */
esp_err_t media_truncate( const rfscout_media_file_t *a_file, uint64_t a_size_bytes );

/** @brief Flush buffered writes and synchronize them to the card. */
esp_err_t media_sync( const rfscout_media_file_t *a_file );

/**
 * @brief Flush and close a handle.
 * @details The handle is invalidated even when the final flush/close reports an error.
 */
esp_err_t media_close( rfscout_media_file_t *a_file );

/** @brief Return metadata for one file; ESP_ERR_NOT_FOUND when it does not exist. */
esp_err_t media_stat( const rfscout_media_collection_t *a_collection,
                      const char *a_name,
                      rfscout_media_entry_t *a_entry );

/** @brief Delete one file; ESP_ERR_NOT_FOUND when it does not exist. */
esp_err_t media_remove( const rfscout_media_collection_t *a_collection, const char *a_name );

/**
 * @brief Visit regular files in a collection in directory order.
 * @details A collection that has never been written lists as empty. Hidden/dot names, directories,
 *          and names outside the media name rules are skipped.
 */
esp_err_t media_list( const rfscout_media_collection_t *a_collection,
                      rfscout_media_list_fn a_visit,
                      void *a_context );

/** @brief Return whether a name satisfies the media file-name rules. */
bool media_name_valid( const char *a_name );

/** @brief Return whether a descriptor persistence key can be used as a media owner directory. */
bool media_owner_name_valid( const char *a_owner );

/**
 * @brief Force-close every handle still owned by one application.
 * @details Lifecycle coordinator only; called after the app's services have quiesced.
 * @return Number of handles that had to be closed.
 */
size_t media_store_close_app( rfscout_app_id_t a_app );

#endif // _RFSCOUT_MEDIA_STORE_H_
