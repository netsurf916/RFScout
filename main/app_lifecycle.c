/**
    app_lifecycle.c : RFScout transactional application lifecycle coordinator
    Description: Coordinates app semantic preparation, app-session quiesce, managed-memory reclamation, and diagnostics.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_lifecycle.h"

#include "app_session.h"
#include "memory_manager.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "app_lifecycle";
static rfscout_app_transition_diagnostics_t last_transition;
static uint32_t transition_sequence;

static void log_transition( const rfscout_app_transition_diagnostics_t *a_diagnostics )
{
    ESP_LOGI( TAG,
              "transition seq=%u from=%d to=%d outcome=%d error=%s reclaimed=%u remaining=%u "
              "elapsed=%lldus psram_free=%u psram_largest=%u",
              (unsigned)a_diagnostics->sequence,
              (int)a_diagnostics->from,
              (int)a_diagnostics->to,
              (int)a_diagnostics->outcome,
              esp_err_to_name( a_diagnostics->error ),
              (unsigned)a_diagnostics->reclaimed_bytes,
              (unsigned)a_diagnostics->remaining_allocations,
              (long long)a_diagnostics->elapsed_us,
              (unsigned)heap_caps_get_free_size( MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT ),
              (unsigned)heap_caps_get_largest_free_block( MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT ) );
}

static rfscout_app_transition_diagnostics_t
finish_transition( rfscout_app_transition_diagnostics_t a_diagnostics, int64_t a_started_us )
{
    a_diagnostics.elapsed_us = esp_timer_get_time() - a_started_us;
    last_transition = a_diagnostics;
    log_transition( &last_transition );
    return last_transition;
}

static rfscout_app_transition_diagnostics_t
abort_transition( rfscout_app_transition_diagnostics_t a_diagnostics,
                  rfscout_app_transition_outcome_t a_outcome,
                  esp_err_t a_error,
                  const rfscout_app_lifecycle_hooks_t *a_hooks,
                  int64_t a_started_us )
{
    a_diagnostics.outcome = a_outcome;
    a_diagnostics.error = a_error;

    const esp_err_t session_result = app_session_abort_close();
    esp_err_t app_result = ESP_OK;
    if( NULL != a_hooks->reactivate )
    {
        app_result = a_hooks->reactivate( a_hooks->context );
    }

    if( ( ESP_OK != session_result ) || ( ESP_OK != app_result ) )
    {
        a_diagnostics.outcome = RFSCOUT_APP_TRANSITION_ABORTED_REACTIVATION;
        a_diagnostics.error = ( ESP_OK != session_result ) ? session_result : app_result;
        ESP_LOGE( TAG,
                  "source app reactivation failed after aborted transition from=%d to=%d",
                  (int)a_diagnostics.from,
                  (int)a_diagnostics.to );
    }

    return finish_transition( a_diagnostics, a_started_us );
}

esp_err_t app_lifecycle_init( void )
{
    last_transition = ( rfscout_app_transition_diagnostics_t ){
        .from = RFSCOUT_APP_DEVICE,
        .to = RFSCOUT_APP_DEVICE,
        .outcome = RFSCOUT_APP_TRANSITION_NONE,
        .error = ESP_OK,
    };
    transition_sequence = 0U;

    esp_err_t result = memory_manager_init();
    if( ESP_OK == result )
    {
        result = app_session_ensure( app_manager_current() );
    }
    return result;
}

rfscout_app_transition_diagnostics_t app_lifecycle_transition(
    rfscout_app_id_t a_from, rfscout_app_id_t a_to, const rfscout_app_lifecycle_hooks_t *a_hooks )
{
    const int64_t started_us = esp_timer_get_time();
    rfscout_app_transition_diagnostics_t diagnostics = {
        .from = a_from,
        .to = a_to,
        .outcome = RFSCOUT_APP_TRANSITION_NONE,
        .error = ESP_OK,
        .sequence = ++transition_sequence,
    };

    if( ( NULL == a_hooks ) || ( a_from < 0 ) || ( a_from >= RFSCOUT_APP_COUNT ) || ( a_to < 0 ) ||
        ( a_to >= RFSCOUT_APP_COUNT ) || ( a_from == a_to ) )
    {
        diagnostics.outcome = RFSCOUT_APP_TRANSITION_ABORTED_PREPARE;
        diagnostics.error = ESP_ERR_INVALID_ARG;
        return finish_transition( diagnostics, started_us );
    }

    esp_err_t result = app_session_ensure( a_from );
    if( ESP_OK != result )
    {
        return abort_transition(
            diagnostics, RFSCOUT_APP_TRANSITION_ABORTED_PREPARE, result, a_hooks, started_us );
    }

    if( NULL != a_hooks->prepare )
    {
        result = a_hooks->prepare( a_hooks->context );
        if( ESP_OK != result )
        {
            return abort_transition(
                diagnostics, RFSCOUT_APP_TRANSITION_ABORTED_PREPARE, result, a_hooks, started_us );
        }
    }

    result = app_session_prepare_close();
    if( ( ESP_OK != result ) || !app_session_ready_to_reclaim() )
    {
        if( ESP_OK == result )
        {
            result = ESP_ERR_INVALID_STATE;
        }
        return abort_transition(
            diagnostics, RFSCOUT_APP_TRANSITION_ABORTED_PREPARE, result, a_hooks, started_us );
    }

    if( NULL != a_hooks->commit )
    {
        a_hooks->commit( a_hooks->context );
    }
    app_session_commit_close();

    diagnostics.reclaimed_bytes = memory_manager_release_app( a_from );
    diagnostics.remaining_allocations = memory_manager_app_allocation_count( a_from );
    if( 0U != diagnostics.remaining_allocations )
    {
        diagnostics.outcome = RFSCOUT_APP_TRANSITION_COMMITTED_MEMORY_INVARIANT;
        diagnostics.error = ESP_ERR_INVALID_STATE;
        ESP_LOGE( TAG,
                  "committed app exit left %u managed allocations from=%d",
                  (unsigned)diagnostics.remaining_allocations,
                  (int)a_from );
    }
    else
    {
        diagnostics.outcome = RFSCOUT_APP_TRANSITION_COMMITTED;
        diagnostics.error = ESP_OK;
    }

    result = app_session_ensure( a_to );
    if( ESP_OK != result )
    {
        diagnostics.outcome = RFSCOUT_APP_TRANSITION_COMMITTED_MEMORY_INVARIANT;
        diagnostics.error = result;
        ESP_LOGE( TAG,
                  "destination app session creation failed from=%d to=%d: %s",
                  (int)a_from,
                  (int)a_to,
                  esp_err_to_name( result ) );
    }

    return finish_transition( diagnostics, started_us );
}

bool app_lifecycle_transition_committed( const rfscout_app_transition_diagnostics_t *a_diagnostics )
{
    if( NULL == a_diagnostics )
    {
        return false;
    }
    return ( RFSCOUT_APP_TRANSITION_COMMITTED == a_diagnostics->outcome ) ||
           ( RFSCOUT_APP_TRANSITION_COMMITTED_MEMORY_INVARIANT == a_diagnostics->outcome );
}

rfscout_app_transition_diagnostics_t app_lifecycle_last_transition( void )
{
    return last_transition;
}
