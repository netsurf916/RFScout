/**
    app_lifecycle.h : RFScout transactional application lifecycle coordinator
    Description: Declares semantic prepare/reactivation hooks, reclamation, and transition diagnostics.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_LIFECYCLE_H_
#define _RFSCOUT_APP_LIFECYCLE_H_

#include "app_manager.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Outcome of the most recent application transition attempt. */
typedef enum
{
    RFSCOUT_APP_TRANSITION_NONE = 0,
    RFSCOUT_APP_TRANSITION_COMMITTED,
    RFSCOUT_APP_TRANSITION_ABORTED_PREPARE,
    RFSCOUT_APP_TRANSITION_ABORTED_REACTIVATION,
    RFSCOUT_APP_TRANSITION_COMMITTED_MEMORY_INVARIANT
} rfscout_app_transition_outcome_t;

/**
 * @brief Optional application-semantic hooks around the platform-owned resource session.
 * @details Applications do not implement service reachability. The session layer owns stopping and
 *          restoring platform services; these hooks exist only for semantic state the platform
 *          cannot infer, such as restoring the exact Recorder operation after an aborted close.
 */
typedef struct
{
    esp_err_t ( *prepare )( void *a_context );
    esp_err_t ( *reactivate )( void *a_context );
    void ( *commit )( void *a_context );
    void *context;
} rfscout_app_lifecycle_hooks_t;

/** @brief Diagnostics retained for the most recent transition attempt. */
typedef struct
{
    rfscout_app_id_t from;
    rfscout_app_id_t to;
    rfscout_app_transition_outcome_t outcome;
    esp_err_t error;
    size_t reclaimed_bytes;
    size_t remaining_allocations;
    int64_t elapsed_us;
    uint32_t sequence;
} rfscout_app_transition_diagnostics_t;

/** @brief Initialize lifecycle coordination and the managed PSRAM allocator. */
esp_err_t app_lifecycle_init( void );

/**
 * @brief Execute a transactional application exit through bulk reclamation.
 * @details Semantic Prepare runs first, then the platform resource session quiesces its services.
 *          Any failure before commit invokes session restoration and the optional semantic
 *          reactivation hook while leaving app-owned memory intact. After commit, all remaining
 *          managed allocations for the source app are reclaimed and verified.
 */
rfscout_app_transition_diagnostics_t app_lifecycle_transition(
    rfscout_app_id_t a_from, rfscout_app_id_t a_to, const rfscout_app_lifecycle_hooks_t *a_hooks );

/** @brief Return true when the transition crossed the irreversible commit boundary. */
bool app_lifecycle_transition_committed(
    const rfscout_app_transition_diagnostics_t *a_diagnostics );

/** @brief Return diagnostics for the most recent transition attempt. */
rfscout_app_transition_diagnostics_t app_lifecycle_last_transition( void );

#endif // _RFSCOUT_APP_LIFECYCLE_H_
