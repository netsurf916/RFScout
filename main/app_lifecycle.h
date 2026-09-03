/**
    app_lifecycle.h : RFScout transactional application lifecycle coordinator
    Description: Declares prepare, reachability, commit, reactivation, reclamation, and transition diagnostics.
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
    RFSCOUT_APP_TRANSITION_ABORTED_REACHABILITY,
    RFSCOUT_APP_TRANSITION_ABORTED_REACTIVATION,
    RFSCOUT_APP_TRANSITION_COMMITTED_MEMORY_INVARIANT
} rfscout_app_transition_outcome_t;

/** @brief Fallible/reversible and irreversible hooks for one source application. */
typedef struct
{
    esp_err_t ( *prepare )( void *a_context );
    bool ( *reachability_clear )( void *a_context );
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

/**
 * @brief Initialize lifecycle coordination and the managed PSRAM allocator.
 * @return ESP_OK when lifecycle prerequisites are available.
 */
esp_err_t app_lifecycle_init( void );

/**
 * @brief Execute a transactional application exit through the final reclamation invariant.
 * @details Prepare and reachability are fallible. Failure before commit invokes reactivation and
 *          leaves app-owned memory intact. Commit is the irreversible boundary; after it returns,
 *          all remaining managed allocations for the source app are reclaimed and verified.
 */
rfscout_app_transition_diagnostics_t app_lifecycle_transition(
    rfscout_app_id_t a_from, rfscout_app_id_t a_to, const rfscout_app_lifecycle_hooks_t *a_hooks );

/** @brief Return true when the transition crossed the irreversible commit boundary. */
bool app_lifecycle_transition_committed(
    const rfscout_app_transition_diagnostics_t *a_diagnostics );

/** @brief Return diagnostics for the most recent transition attempt. */
rfscout_app_transition_diagnostics_t app_lifecycle_last_transition( void );

#endif // _RFSCOUT_APP_LIFECYCLE_H_
