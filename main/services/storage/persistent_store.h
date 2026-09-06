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

#define RFSCOUT_INTERNAL_ROOT "/internal"
#define RFSCOUT_PERSISTENT_KEY_MAX 48U

/** @brief Mount and initialize the system-owned internal persistence service. */
esp_err_t persistent_store_init( void );
/** @brief Return whether internal persistence is mounted and usable. */
bool persistent_store_is_available( void );

/** @brief Atomically replace one privileged system record. */
esp_err_t system_store_write( const char *a_key, const void *a_data, size_t a_length );
/** @brief Read one privileged system record; NULL/0 may be used to query required length. */
esp_err_t system_store_read( const char *a_key, void *a_data, size_t a_capacity, size_t *a_length );
esp_err_t system_store_remove( const char *a_key );
bool system_store_exists( const char *a_key );

/**
 * @brief Privileged core access to one compiled application's records.
 * @details Used by system orchestration such as device_config when it must load/save all
 *          application configuration records. Application modules should use app_store_*.
 */
esp_err_t persistent_store_app_write( rfscout_app_id_t a_app,
                                      const char *a_key,
                                      const void *a_data,
                                      size_t a_length );
esp_err_t persistent_store_app_read(
    rfscout_app_id_t a_app, const char *a_key, void *a_data, size_t a_capacity, size_t *a_length );
esp_err_t persistent_store_app_remove( rfscout_app_id_t a_app, const char *a_key );
bool persistent_store_app_exists( rfscout_app_id_t a_app, const char *a_key );

/**
 * @brief Atomically replace a record owned by the current application session.
 * @details The caller cannot name another application's namespace; ownership is derived
 *          from app_session_app() and the descriptor persistence_key.
 */
esp_err_t app_store_write( const char *a_key, const void *a_data, size_t a_length );
esp_err_t app_store_read( const char *a_key, void *a_data, size_t a_capacity, size_t *a_length );
esp_err_t app_store_remove( const char *a_key );
bool app_store_exists( const char *a_key );

#endif // _RFSCOUT_PERSISTENT_STORE_H_
