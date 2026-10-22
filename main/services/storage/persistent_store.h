/**
    persistent_store.h : RFScout internal persistent record service
    Description: Provides atomic system-owned and descriptor-bound app records on internal flash.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_PERSISTENT_STORE_H_
#define _RFSCOUT_PERSISTENT_STORE_H_

#include "app_descriptor.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_INTERNAL_ROOT "/internal"
#define RFSCOUT_PERSISTENT_KEY_MAX_BYTES 256U
#define RFSCOUT_PERSISTENT_RECORD_MAX_BYTES 16384U

typedef struct
{
    rfscout_app_id_t app;
    uint32_t generation;
} rfscout_app_store_context_t;

/** @brief Return whether a fixed-size record read from the store is acceptable. */
typedef bool ( *persistent_record_valid_fn )( const void *a_record );

/** @brief Mount and initialize the system-owned internal persistence service. */
esp_err_t persistent_store_init( void );
/** @brief Return whether internal persistence is mounted and usable. */
bool persistent_store_is_available( void );

/**
 * @brief Atomically replace one privileged system record.
 * @details Logical keys are opaque strings. The service hashes them to fixed filesystem names and
 *          serializes filesystem operations internally, so callers do not need path-safe keys or
 *          storage locking.
 */
esp_err_t system_store_write( const char *a_key, const void *a_data, size_t a_length );
/** @brief Read one privileged system record; NULL/0 may be used to query required length. */
esp_err_t system_store_read( const char *a_key, void *a_data, size_t a_capacity, size_t *a_length );
esp_err_t system_store_remove( const char *a_key );
bool system_store_exists( const char *a_key );
/**
 * @brief Read one fixed-size system record and remove it when it is rejected.
 * @details Returns ESP_OK only when exactly @p a_length bytes were read and @p a_valid (optional)
 *          accepts them, and ESP_ERR_NOT_FOUND when no record exists. A record with the wrong
 *          size, an unreadable body or rejected contents is removed, because nothing short of
 *          erasing the device could otherwise clear it, and ESP_ERR_INVALID_RESPONSE is returned.
 *          @p a_record is unspecified on any failure.
 */
esp_err_t system_store_load( const char *a_key,
                             void *a_record,
                             size_t a_length,
                             persistent_record_valid_fn a_valid );

/**
 * @brief Privileged core access to one compiled application's records.
 * @details Used by system orchestration such as device_config when it must load/save application
 *          configuration records. Application modules should use app_store_*.
 */
esp_err_t persistent_store_app_write( rfscout_app_id_t a_app,
                                      const char *a_key,
                                      const void *a_data,
                                      size_t a_length );
esp_err_t persistent_store_app_read(
    rfscout_app_id_t a_app, const char *a_key, void *a_data, size_t a_capacity, size_t *a_length );
/** @brief system_store_load() semantics for one compiled application's record (privileged). */
esp_err_t persistent_store_app_load( rfscout_app_id_t a_app,
                                     const char *a_key,
                                     void *a_record,
                                     size_t a_length,
                                     persistent_record_valid_fn a_valid );
esp_err_t persistent_store_app_remove( rfscout_app_id_t a_app, const char *a_key );
bool persistent_store_app_exists( rfscout_app_id_t a_app, const char *a_key );

/**
 * @brief Capture the current app-session identity for asynchronous persistence work.
 * @details The context becomes invalid when the owning app session closes or is replaced.
 */
esp_err_t app_store_context_current( rfscout_app_store_context_t *a_context );

/**
 * @brief App persistence using an explicitly captured session identity.
 * @details These calls reject stale contexts. Asynchronous app work remains responsible for using
 *          the existing app lifecycle prepare hook to veto transition while its operation is active.
 */
esp_err_t app_store_write_context( const rfscout_app_store_context_t *a_context,
                                   const char *a_key,
                                   const void *a_data,
                                   size_t a_length );
esp_err_t app_store_read_context( const rfscout_app_store_context_t *a_context,
                                  const char *a_key,
                                  void *a_data,
                                  size_t a_capacity,
                                  size_t *a_length );
esp_err_t app_store_remove_context( const rfscout_app_store_context_t *a_context,
                                    const char *a_key );
bool app_store_exists_context( const rfscout_app_store_context_t *a_context, const char *a_key );

/**
 * @brief Convenience access to records owned by the current application session.
 * @details Synchronous app code may use these directly. Asynchronous work should capture a
 *          rfscout_app_store_context_t before dispatch and use the context variants above.
 */
esp_err_t app_store_write( const char *a_key, const void *a_data, size_t a_length );
esp_err_t app_store_read( const char *a_key, void *a_data, size_t a_capacity, size_t *a_length );
esp_err_t app_store_remove( const char *a_key );
bool app_store_exists( const char *a_key );
/** @brief system_store_load() semantics for a record owned by the current application session. */
esp_err_t app_store_load( const char *a_key,
                          void *a_record,
                          size_t a_length,
                          persistent_record_valid_fn a_valid );

#endif // _RFSCOUT_PERSISTENT_STORE_H_
