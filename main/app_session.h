/**
    app_session.h : RFScout application resource session
    Description: Provides app-scoped resources, managed presentation view ownership, and teardown.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_SESSION_H_
#define _RFSCOUT_APP_SESSION_H_

#include "app_descriptor.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Ensure the platform has a resource session for the selected application.
 * @details The lifecycle coordinator owns session creation. Every active app has a session, even
 *          when it currently owns no managed memory or asynchronous platform services. Session
 *          creation also acquires and initializes the canonical managed application view.
 */
esp_err_t app_session_ensure( rfscout_app_id_t a_app );

/** @brief Return the application that owns the current resource session. */
rfscout_app_id_t app_session_app( void );

/** @brief Return the immutable managed presentation view for the current session. */
const rfscout_view_t *app_session_view( void );

/** @brief Return a monotonically changing identifier for the current app-session lifetime. */
uint32_t app_session_generation( void );

/** @brief Allocate zero-initialized PSRAM owned by the current app session. */
void *app_session_calloc( size_t a_count, size_t a_size );

/** @brief Resize app-session memory while preserving the original allocation on failure. */
void *app_session_resize( void *a_pointer, size_t a_count, size_t a_size );

/** @brief Explicitly release one app-session allocation. Bulk release on close remains automatic. */
bool app_session_free( void *a_pointer );

/**
 * @brief Stop every platform service belonging to the session and prove it safe to reclaim.
 * @details Platform service ownership is registered internally by the platform. Applications do
 *          not implement callback counters or lifecycle service hooks. A service stop succeeds only
 *          after callbacks/workers can no longer reach app memory. Presentation cleanup occurs only
 *          after the app LEAVE callback, when the prepared close is committed.
 */
esp_err_t app_session_prepare_close( void );

/**
 * @brief Abort a prepared/partially prepared close and restore the source service set.
 * @return ESP_OK when required service restoration succeeded, otherwise the first restart error.
 */
esp_err_t app_session_abort_close( void );

/** @brief Return whether every platform service has safely quiesced. */
bool app_session_ready_to_reclaim( void );

/** @brief Commit a successful close, deinitialize its view, and release session ownership. */
void app_session_commit_close( void );

#endif // _RFSCOUT_APP_SESSION_H_
