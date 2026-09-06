/**
    apps/thermal/app.c : Thermal application descriptor and runtime
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "app_framework.h"
#include "mlx90640.h"
#include "thermal_view.h"

#include <stddef.h>

#define THERMAL_REFRESH_INTERVAL_US 125000LL

static bool service_initialized;
static int64_t last_refresh_us;

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
        thermal_view_enter();
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
    const esp_err_t result = thermal_view_render();
    if( ( ESP_OK != result ) && ( ESP_ERR_NOT_FINISHED != result ) && !mlx90640_is_detected() )
    {
        app_framework_report_availability( RFSCOUT_APP_THERMAL,
                                           RFSCOUT_APP_AVAILABILITY_UNAVAILABLE );
        return RFSCOUT_APP_RUNTIME_REQUEST_HOME;
    }
    return RFSCOUT_APP_RUNTIME_HANDLED;
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
};
