/**
    apps/gps/app.c : GPS receiver diagnostics application
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "gps.h"
#include "gps_view.h"

#include <stddef.h>

#define GPS_VIEW_REFRESH_INTERVAL_US 500000LL

static int64_t last_refresh_us;

static rfscout_app_runtime_result_t gps_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        last_refresh_us = 0;
        gps_view_render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type )
    {
        if( 0 == last_refresh_us ||
            a_event->now_us - last_refresh_us >= GPS_VIEW_REFRESH_INTERVAL_US )
        {
            last_refresh_us = a_event->now_us;
            gps_view_render();
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_APP_RUNTIME_INPUT == a_event->type )
    {
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
