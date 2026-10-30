/**
    apps/thermal/app.c : Thermal application descriptor and runtime
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "app_framework.h"
#include "app_manager.h"
#include "display_idle.h"
#include "mlx90640.h"
#include "thermal_view.h"

#include <stddef.h>

#define THERMAL_REFRESH_INTERVAL_US 1000LL
#define THERMAL_ERROR_BACKOFF_US 250000LL

static bool service_initialized;
static int64_t last_refresh_us;
static bool menu_active;
static bool keep_screen_awake = true;
static unsigned menu_item;

static esp_err_t probe_thermal( void )
{
    if( !service_initialized )
    {
        service_initialized = true;
        return mlx90640_init();
    }
    return mlx90640_probe();
}

static rfscout_app_runtime_result_t thermal_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        last_refresh_us = 0;
        menu_active = false;
        mlx90640_discard_partial_frame();
        menu_item = 0U;
        display_idle_set_keep_awake( keep_screen_awake );
        thermal_view_reset();
        app_manager_request_render( RFSCOUT_APP_THERMAL, true );
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        mlx90640_discard_partial_frame();
        display_idle_set_keep_awake( false );
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
        if( RFSCOUT_INPUT_SELECT == a_event->input.type )
        {
            if( !menu_active )
            {
                menu_active = true;
                mlx90640_discard_partial_frame();
                menu_item = 0U;
            }
            else if( 0U == menu_item )
            {
                menu_active = false;
            }
            else if( 1U == menu_item )
            {
                (void)app_manager_set_locked( !app_manager_locked() );
            }
            else
            {
                keep_screen_awake = !keep_screen_awake;
                display_idle_set_keep_awake( keep_screen_awake );
            }
            if( menu_active )
            {
                app_manager_request_render( RFSCOUT_APP_THERMAL, true );
            }
            else
            {
                app_manager_request_render( RFSCOUT_APP_THERMAL, true );
            }
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        if( menu_active && RFSCOUT_INPUT_NAVIGATION == a_event->input.type )
        {
            menu_item = ( menu_item + ( a_event->input.value > 0 ? 1U : 2U ) ) % 3U;
            app_manager_request_render( RFSCOUT_APP_THERMAL, true );
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( RFSCOUT_APP_RUNTIME_RENDER == a_event->type )
    {
        if( menu_active )
        {
            thermal_view_menu( menu_item, app_manager_locked(), keep_screen_awake );
        }
        else
        {
            (void)thermal_view_render();
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL != a_event->type )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }
    if( !a_event->activated )
    {
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( !mlx90640_is_detected() )
    {
        app_framework_report_availability( RFSCOUT_APP_THERMAL,
                                           RFSCOUT_APP_AVAILABILITY_UNAVAILABLE );
        return RFSCOUT_APP_RUNTIME_REQUEST_HOME;
    }
    if( a_event->now_us - last_refresh_us < THERMAL_REFRESH_INTERVAL_US )
    {
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    last_refresh_us = a_event->now_us;
    if( menu_active )
    {
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    const esp_err_t result = thermal_view_sample();
    if( ESP_ERR_NOT_FINISHED != result )
    {
        app_manager_request_render( RFSCOUT_APP_THERMAL, false );
    }
    if( ( ESP_OK != result ) && ( ESP_ERR_NOT_FINISHED != result ) && mlx90640_is_detected() )
    {
        /* Avoid hammering an unhealthy shared I2C bus between other clients. */
        last_refresh_us = a_event->now_us + THERMAL_ERROR_BACKOFF_US;
    }
    if( ( ESP_OK != result ) && ( ESP_ERR_NOT_FINISHED != result ) && !mlx90640_is_detected() )
    {
        app_framework_report_availability( RFSCOUT_APP_THERMAL,
                                           RFSCOUT_APP_AVAILABILITY_UNAVAILABLE );
        return RFSCOUT_APP_RUNTIME_REQUEST_HOME;
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
}

static bool app_telemetry( rfscout_log_record_t *a_record )
{
    bool success = log_bool( a_record, "detected", mlx90640_is_detected() );
    return success;
}

const rfscout_app_descriptor_t rfscout_thermal_app = {
    .id = RFSCOUT_APP_THERMAL,
    .name = "Thermal",
    .persistence_key = "thermal",
    .probe_policy = RFSCOUT_APP_PROBE_HOTPLUG,
    .probe = probe_thermal,
    .requires_activation = true,
    .services = RFSCOUT_APP_SERVICE_NONE,
    .runtime = thermal_runtime,
    .telemetry = app_telemetry,
};
