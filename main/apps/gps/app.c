/**
    apps/gps/app.c : GPS receiver diagnostics application
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "gps.h"
#include "gps_view.h"

#include "esp_log.h"

#include <stddef.h>

#define GPS_VIEW_REFRESH_INTERVAL_US 500000LL

static const char *TAG = "gps_app";
static int64_t last_refresh_us;
static bool reset_prompt;
static bool reset_yes;
static bool reset_failed;

static void render( void )
{
    gps_view_render( reset_prompt, reset_yes, reset_failed );
}

static rfscout_app_runtime_result_t gps_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        last_refresh_us = 0;
        reset_prompt = false;
        reset_yes = false;
        reset_failed = false;
        render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type )
    {
        if( 0 == last_refresh_us ||
            a_event->now_us - last_refresh_us >= GPS_VIEW_REFRESH_INTERVAL_US )
        {
            last_refresh_us = a_event->now_us;
            render();
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
        if( reset_prompt )
        {
            if( RFSCOUT_INPUT_NAVIGATION == a_event->input.type )
            {
                reset_yes = !reset_yes;
            }
            else if( RFSCOUT_INPUT_LONG_PRESS == a_event->input.type )
            {
                reset_prompt = false;
            }
            else if( RFSCOUT_INPUT_SELECT == a_event->input.type )
            {
                if( reset_yes )
                {
                    const esp_err_t result = gps_trip_reset();
                    reset_failed = ESP_OK != result;
                    if( reset_failed )
                    {
                        ESP_LOGW(
                            TAG, "trip reset could not be saved: %s", esp_err_to_name( result ) );
                    }
                }
                reset_prompt = false;
            }
            render();
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        if( RFSCOUT_INPUT_SELECT == a_event->input.type )
        {
            reset_prompt = true;
            reset_yes = false;
            reset_failed = false;
            render();
            return RFSCOUT_APP_RUNTIME_HANDLED;
        }
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    return RFSCOUT_APP_RUNTIME_HANDLED;
}

const rfscout_app_descriptor_t rfscout_gps_app = {
    .id = RFSCOUT_APP_GPS,
    .name = "GPS",
    .persistence_key = "gps",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = false,
    .services = RFSCOUT_APP_SERVICE_NONE,
    .runtime = gps_runtime,
};
