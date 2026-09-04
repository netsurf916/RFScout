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

typedef esp_err_t ( *rfscout_app_service_fn )( void *a_context );

/**
 * @brief Ensure the platform has a resource session for the selected application.
 * @details The lifecycle coordinator owns this call. Applications do not open/close sessions.
 */
esp_err_t app_session_ensure( rfscout_app_id_t a_app );

/** @brief Allocate zero-initialized PSRAM owned by the current app session. */
void *app_session_calloc( size_t a_count, size_t a_size );

/** @brief Resize app-session memory while preserving the original allocation on failure. */
void *app_session_resize( void *a_pointer, size_t a_count, size_t a_size );

/** @brief Explicitly release one app-session allocation. Bulk release on close remains automatic. */
bool app_session_free( void *a_pointer );

/**
 * @brief Register a platform service used by the current app session.
 * @details Platform wrappers/registry code own service registration; application implementations
 *          should normally only use the service. Re-registering a stop callback/context pair is
 *          harmless.
 */
esp_err_t app_session_register_service( const char *a_name,
                                        rfscout_app_service_fn a_start,
                                        rfscout_app_service_fn a_stop,
                                        void *a_context );

/**
 * @brief Stop every registered platform service and prove the session is safe to reclaim.
 * @details A service stop succeeds only after callbacks/workers can no longer reach app memory.
 *          Failure preserves app memory and reactivates touched services best-effort.
 */
esp_err_t app_session_prepare_close( void );

/** @brief Return whether every registered platform service has safely quiesced. */
bool app_session_ready_to_reclaim( void );

/** @brief Commit a successful close; app_lifecycle subsequently bulk-reclaims app memory. */
void app_session_commit_close( void );

#endif // _RFSCOUT_APP_SESSION_H_
