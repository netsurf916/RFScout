/**
    apps/system_events/app.c : System Events application descriptor and runtime
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"
#include "app_manager.h"

#include "event.h"
#include "system_events.h"
#include "system_events_view.h"

#include <stddef.h>

#define SYSTEM_EVENTS_REFRESH_INTERVAL_US 200000LL

static int64_t last_refresh_us;
static int64_t latest_event_us;
static int64_t last_age_second;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_SYSTEM_EVENTS, a_value );
}

static rfscout_app_runtime_result_t
system_events_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        last_refresh_us = 0;
        latest_event_us = 0;
        last_age_second = -1;
        system_events_view_enter();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        system_events_view_leave();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_RENDER == a_event->type )
    {
        system_events_view_render();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type )
    {
        if( a_event->now_us - last_refresh_us >= SYSTEM_EVENTS_REFRESH_INTERVAL_US )
        {
            last_refresh_us = a_event->now_us;
            if( !system_events_view_browsing() )
            {
                rfscout_system_event_entry_t latest = { 0 };
                const bool have_event = system_events_history_get( 0U, &latest );
                const int64_t now_second = a_event->now_us / 1000000LL;
                if( ( have_event && latest.monotonic_us != latest_event_us ) ||
                    ( have_event && now_second != last_age_second ) ||
                    ( !have_event && latest_event_us != 0 ) )
                {
                    latest_event_us = have_event ? latest.monotonic_us : 0;
                    last_age_second = now_second;
                    app_manager_request_render( RFSCOUT_APP_SYSTEM_EVENTS, false );
                }
            }
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_INPUT != a_event->type )
    {
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_INPUT_SELECT == a_event->input.type )
    {
        system_events_view_toggle_browse();
        emit_event( RFSCOUT_EVENT_ACTIVATE, system_events_view_browsing() ? 1 : 0 );
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_INPUT_NAVIGATION == a_event->input.type && system_events_view_browsing() )
    {
        const int direction = ( a_event->input.value > 0 ) ? 1 : -1;
        system_events_view_scroll( direction );
        emit_event( RFSCOUT_EVENT_NAVIGATE, direction );
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    return RFSCOUT_APP_RUNTIME_IGNORED;
}

static bool app_telemetry( rfscout_log_record_t *a_record )
{
    bool success = log_uint( a_record, "retained", system_events_history_count() );
    return success;
}

const rfscout_app_descriptor_t rfscout_system_events_app = {
    .id = RFSCOUT_APP_SYSTEM_EVENTS,
    .name = "System Events",
    .persistence_key = "system_events",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = false,
    .services = RFSCOUT_APP_SERVICE_NONE,
    .runtime = system_events_runtime,
    .telemetry = app_telemetry,
};
