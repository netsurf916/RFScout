/**
    app_manager.c : RFScout application runtime coordinator
    Description: Owns active-app selection, global navigation, activation timing, and lifecycle transitions.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_manager.h"

#include "app_framework.h"
#include "app_lifecycle.h"
#include "app_session.h"
#include "device_config.h"
#include "display.h"
#include "event.h"
#include "input.h"
#include "session_state.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stddef.h>

#define APP_FEATURE_PROBE_INTERVAL_US 10000000LL
#define APP_ACTIVATION_DELAY_US 500000LL

static const char *TAG = "app_manager";
static rfscout_app_id_t active_app;
static int64_t last_hotplug_probe_us;
static int64_t app_entered_us;
static bool app_activated;
static bool app_locked;

static bool app_available( rfscout_app_id_t a_app )
{
    if( RFSCOUT_APP_DEVICE == a_app )
    {
        return true;
    }
    if( !device_config_app_enabled( a_app ) )
    {
        return false;
    }
    return app_framework_check_available( a_app );
}

static void log_lifecycle( const char *a_phase,
                           rfscout_app_id_t a_from,
                           rfscout_app_id_t a_to,
                           int64_t a_elapsed_us )
{
    ESP_LOGI( TAG,
              "%s from=%d to=%d elapsed=%lldus heap=%u min=%u largest=%u",
              a_phase,
              (int)a_from,
              (int)a_to,
              (long long)a_elapsed_us,
              (unsigned)heap_caps_get_free_size( MALLOC_CAP_8BIT ),
              (unsigned)heap_caps_get_minimum_free_size( MALLOC_CAP_8BIT ),
              (unsigned)heap_caps_get_largest_free_block( MALLOC_CAP_8BIT ) );
}

static rfscout_app_runtime_result_t dispatch_runtime( rfscout_app_id_t a_app,
                                                      rfscout_app_runtime_event_type_t a_type,
                                                      const rfscout_input_event_t *a_input,
                                                      int64_t a_now_us,
                                                      bool a_activated )
{
    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_app );
    if( ( NULL == descriptor ) || ( NULL == descriptor->runtime ) )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    rfscout_app_runtime_event_t event = {
        .type = a_type,
        .now_us = a_now_us,
        .activated = a_activated,
        .view = ( RFSCOUT_APP_RUNTIME_ENTER == a_type ) ? app_session_view() : NULL,
        .input =
            {
                .type = RFSCOUT_INPUT_NONE,
                .value = 0,
            },
    };
    if( NULL != a_input )
    {
        event.input = *a_input;
    }
    return descriptor->runtime( &event );
}

static void enter_app( rfscout_app_id_t a_app )
{
    const rfscout_app_descriptor_t *descriptor = app_framework_descriptor( a_app );
    app_entered_us = esp_timer_get_time();
    app_activated = ( NULL == descriptor ) || !descriptor->requires_activation;
    (void)dispatch_runtime( a_app, RFSCOUT_APP_RUNTIME_ENTER, NULL, app_entered_us, app_activated );
    ESP_LOGI( TAG, "switched app=%d", (int)a_app );
}

static void activate_app_if_ready( int64_t a_now_us )
{
    if( app_activated || ( a_now_us - app_entered_us < APP_ACTIVATION_DELAY_US ) )
    {
        return;
    }

    const int64_t started = esp_timer_get_time();
    app_activated = true;
    (void)dispatch_runtime(
        active_app, RFSCOUT_APP_RUNTIME_ACTIVATE, NULL, a_now_us, app_activated );
    log_lifecycle( "activate", active_app, active_app, esp_timer_get_time() - started );
}

static rfscout_app_id_t next_available_app( int a_direction )
{
    int next = (int)active_app;
    for( int attempts = 0; attempts < RFSCOUT_APP_COUNT; ++attempts )
    {
        next += a_direction;
        if( next < 0 )
        {
            next = RFSCOUT_APP_COUNT - 1;
        }
        else if( next >= RFSCOUT_APP_COUNT )
        {
            next = 0;
        }
        const rfscout_app_id_t candidate = (rfscout_app_id_t)next;
        if( app_available( candidate ) )
        {
            return candidate;
        }
    }
    return active_app;
}

static esp_err_t prepare_app_exit( void *a_context )
{
    const rfscout_app_descriptor_t *descriptor = a_context;
    return ( NULL != descriptor ) && ( NULL != descriptor->prepare_leave )
               ? descriptor->prepare_leave()
               : ESP_OK;
}

static void commit_app_exit( void *a_context )
{
    const rfscout_app_descriptor_t *descriptor = a_context;
    if( NULL == descriptor )
    {
        return;
    }
    (void)dispatch_runtime(
        descriptor->id, RFSCOUT_APP_RUNTIME_LEAVE, NULL, esp_timer_get_time(), app_activated );
}

static bool transition_to_app( rfscout_app_id_t a_destination, bool a_report_abort )
{
    if( ( a_destination < 0 ) || ( a_destination >= RFSCOUT_APP_COUNT ) ||
        ( a_destination == active_app ) )
    {
        return false;
    }

    const rfscout_app_id_t previous = active_app;
    const rfscout_app_descriptor_t *source_descriptor = app_framework_descriptor( previous );
    const int64_t transition_started = esp_timer_get_time();
    const rfscout_app_lifecycle_hooks_t hooks = {
        .prepare = prepare_app_exit,
        .commit = commit_app_exit,
        .context = (void *)source_descriptor,
    };
    const rfscout_app_transition_diagnostics_t *diagnostics =
        app_lifecycle_transition( previous, a_destination, &hooks );
    if( !app_lifecycle_transition_committed( diagnostics ) )
    {
        log_lifecycle( "transition-abort",
                       previous,
                       a_destination,
                       esp_timer_get_time() - transition_started );
        if( a_report_abort )
        {
            rfscout_event_emit(
                RFSCOUT_EVENT_ERROR, (uint8_t)previous, (int32_t)diagnostics->error );
        }
        return false;
    }

    if( RFSCOUT_APP_TRANSITION_COMMITTED_DESTINATION_FAILED == diagnostics->outcome )
    {
        ESP_LOGE( TAG,
                  "committed transition from=%d could not establish destination=%d; using Home",
                  (int)previous,
                  (int)a_destination );
        const esp_err_t recovery = app_session_ensure( RFSCOUT_APP_DEVICE );
        if( ESP_OK != recovery )
        {
            ESP_LOGE( TAG, "Home session recovery failed: %s", esp_err_to_name( recovery ) );
            ESP_ERROR_CHECK( recovery );
        }
        active_app = RFSCOUT_APP_DEVICE;
        enter_app( active_app );
        log_lifecycle(
            "transition-recover", previous, active_app, esp_timer_get_time() - transition_started );
        if( a_report_abort )
        {
            rfscout_event_emit(
                RFSCOUT_EVENT_ERROR, (uint8_t)previous, (int32_t)diagnostics->error );
        }
        return false;
    }

    (void)app_manager_set_locked( false );
    active_app = a_destination;
    enter_app( active_app );
    log_lifecycle( "transition", previous, active_app, esp_timer_get_time() - transition_started );
    if( ESP_OK != diagnostics->error )
    {
        ESP_LOGE( TAG,
                  "committed transition from=%d to=%d reported %s",
                  (int)previous,
                  (int)active_app,
                  esp_err_to_name( diagnostics->error ) );
    }
    return true;
}

static bool switch_app_for_navigation( int32_t a_delta )
{
    if( 0 == a_delta )
    {
        return false;
    }
    const int direction = ( a_delta > 0 ) ? 1 : -1;
    const rfscout_app_id_t candidate = next_available_app( direction );
    if( !transition_to_app( candidate, true ) )
    {
        return false;
    }
    rfscout_event_emit( RFSCOUT_EVENT_NAVIGATE, (uint8_t)active_app, direction );
    return true;
}

static void handle_runtime_result( rfscout_app_runtime_result_t a_result )
{
    if( ( RFSCOUT_APP_RUNTIME_REQUEST_HOME == a_result ) && ( RFSCOUT_APP_DEVICE != active_app ) )
    {
        (void)transition_to_app( RFSCOUT_APP_DEVICE, false );
    }
}

esp_err_t app_manager_init( void )
{
    active_app = device_config_startup_app();
    rfscout_session_state_t restored_session;
    const bool have_restored_session = session_state_restore( &restored_session );
    last_hotplug_probe_us = esp_timer_get_time();
    app_entered_us = 0;
    app_activated = false;
    app_locked = false;

    esp_err_t result = display_init();
    if( ESP_OK != result )
    {
        return result;
    }
    const rfscout_display_info_t *display = display_info();
    if( NULL != display )
    {
        rfscout_event_emit( RFSCOUT_EVENT_DISPLAY_PROVIDER_SELECTED,
                            RFSCOUT_EVENT_SOURCE_SYSTEM,
                            (int32_t)display->provider );
    }

    result = app_lifecycle_init();
    if( ESP_OK != result )
    {
        ESP_LOGE(
            TAG, "application lifecycle initialization failed: %s", esp_err_to_name( result ) );
        return result;
    }

    result = app_framework_init();
    if( ESP_OK != result )
    {
        return result;
    }

    if( have_restored_session && app_available( restored_session.active_app ) )
    {
        active_app = restored_session.active_app;
    }
    if( !app_available( active_app ) )
    {
        ESP_LOGW( TAG, "configured or retained app unavailable; using Home" );
        active_app = RFSCOUT_APP_DEVICE;
    }
    if( app_session_app() != active_app )
    {
        result = app_session_ensure( active_app );
        if( ESP_OK != result )
        {
            return result;
        }
    }
    enter_app( active_app );
    return ESP_OK;
}

rfscout_app_id_t app_manager_current( void )
{
    return active_app;
}

bool app_manager_locked( void )
{
    return app_locked;
}

bool app_manager_set_locked( bool a_locked )
{
    if( a_locked && ( RFSCOUT_APP_DEVICE == active_app ) )
    {
        return false;
    }
    if( app_locked == a_locked )
    {
        return app_locked;
    }

    app_locked = a_locked;
    ESP_LOGI( TAG, "application %s app=%d", app_locked ? "locked" : "unlocked", (int)active_app );
    rfscout_event_emit( app_locked ? RFSCOUT_EVENT_APP_LOCKED : RFSCOUT_EVENT_APP_UNLOCKED,
                        RFSCOUT_EVENT_SOURCE_SYSTEM,
                        app_locked ? 1 : 0 );
    return app_locked;
}

bool app_manager_select( rfscout_app_id_t a_app )
{
    if( ( a_app < 0 ) || ( a_app >= RFSCOUT_APP_COUNT ) || ( a_app == active_app ) ||
        !app_available( a_app ) )
    {
        return false;
    }

    if( !transition_to_app( a_app, true ) )
    {
        return false;
    }
    rfscout_event_emit( RFSCOUT_EVENT_NAVIGATE, (uint8_t)active_app, 0 );
    return true;
}

void app_manager_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( now_us - last_hotplug_probe_us >= APP_FEATURE_PROBE_INTERVAL_US )
    {
        for( int app = 0; app < RFSCOUT_APP_COUNT; ++app )
        {
            const rfscout_app_id_t candidate = (rfscout_app_id_t)app;
            if( ( candidate != active_app ) && device_config_app_enabled( candidate ) )
            {
                app_framework_refresh_hotplug( candidate );
            }
        }
        last_hotplug_probe_us = now_us;
    }

    rfscout_input_event_t input;
    if( input_get( &input ) )
    {
        if( RFSCOUT_INPUT_GLOBAL_HOLD == input.type )
        {
            if( RFSCOUT_HOLD_SHORTCUT_APP_LOCK == device_config_hold_shortcut() )
            {
                (void)app_manager_set_locked( !app_locked );
            }
            else
            {
                input_set_locked( true );
            }
            return;
        }

        const rfscout_app_runtime_result_t result = dispatch_runtime(
            active_app, RFSCOUT_APP_RUNTIME_INPUT, &input, now_us, app_activated );
        if( RFSCOUT_APP_RUNTIME_REQUEST_HOME == result )
        {
            handle_runtime_result( result );
            return;
        }
        if( RFSCOUT_APP_RUNTIME_HANDLED == result )
        {
            if( RFSCOUT_INPUT_BACK != input.type )
            {
                activate_app_if_ready( now_us );
            }
            return;
        }

        if( RFSCOUT_INPUT_NAVIGATION == input.type )
        {
            if( !app_locked )
            {
                (void)switch_app_for_navigation( input.value );
            }
            return;
        }
        if( ( RFSCOUT_INPUT_BACK == input.type ) && ( RFSCOUT_APP_DEVICE != active_app ) )
        {
            if( transition_to_app( RFSCOUT_APP_DEVICE, true ) )
            {
                rfscout_event_emit( RFSCOUT_EVENT_BACK, (uint8_t)RFSCOUT_APP_DEVICE, 0 );
            }
            return;
        }
    }

    activate_app_if_ready( now_us );
    const rfscout_app_runtime_result_t result =
        dispatch_runtime( active_app, RFSCOUT_APP_RUNTIME_POLL, NULL, now_us, app_activated );
    handle_runtime_result( result );
}
