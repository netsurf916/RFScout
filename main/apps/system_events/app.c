/**
    apps/system_events/app.c : System Events application descriptor and runtime
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "event.h"
#include "system_events_view.h"

#include <stddef.h>

#define SYSTEM_EVENTS_REFRESH_INTERVAL_US 125000LL

static int64_t last_refresh_us;

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
        system_events_view_enter();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        system_events_view_leave();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type )
    {
        if( a_event->now_us - last_refresh_us >= SYSTEM_EVENTS_REFRESH_INTERVAL_US )
        {
            last_refresh_us = a_event->now_us;
            system_events_view_render();
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

const rfscout_app_descriptor_t rfscout_system_events_app = {
    .id = RFSCOUT_APP_SYSTEM_EVENTS,
    .name = "System Events",
    .persistence_key = "system_events",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = false,
    .services = RFSCOUT_APP_SERVICE_NONE,
    .runtime = system_events_runtime,
};
