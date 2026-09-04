/**
    app_session.h : RFScout application resource session
    Description: Provides app-scoped memory and automatic platform-service teardown.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_SESSION_H_
#define _RFSCOUT_APP_SESSION_H_

#include "app_manager.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef esp_err_t ( *rfscout_app_service_fn )( void *a_context );

/** @brief Start a fresh resource session for the selected application. */
esp_err_t app_session_begin( rfscout_app_id_t a_app );

/** @brief Return a generation that changes whenever a fresh app session begins. */
uint32_t app_session_generation( void );

/** @brief Allocate zero-initialized PSRAM owned by the current app session. */
void *app_session_calloc( size_t a_count, size_t a_size );

/** @brief Resize app-session memory while preserving the original allocation on failure. */
void *app_session_resize( void *a_pointer, size_t a_count, size_t a_size );

/** @brief Explicitly release one app-session allocation. Bulk release on close remains automatic. */
bool app_session_free( void *a_pointer );

/**
 * @brief Register a platform service used by the current app session.
 * @details Platform wrappers call this themselves; application code should normally only open/use
 *          the service. Re-registering the same stop callback/context pair is harmless.
 */
esp_err_t app_session_register_service( const char *a_name,
                                        rfscout_app_service_fn a_start,
                                        rfscout_app_service_fn a_stop,
                                        void *a_context );

/**
 * @brief Stop every registered platform service and prove the session is safe to reclaim.
 * @details A service stop operation succeeds only after its callbacks/workers can no longer reach
 *          app memory. On failure, services already touched by the close attempt are reactivated
 *          best-effort and the session remains intact for a later retry.
 */
esp_err_t app_session_prepare_close( void );

/** @brief Return whether the current session has successfully quiesced all registered services. */
bool app_session_ready_to_reclaim( void );

/** @brief Commit a successful close; app_lifecycle performs the subsequent bulk memory release. */
void app_session_commit_close( void );

#endif // _RFSCOUT_APP_SESSION_H_
