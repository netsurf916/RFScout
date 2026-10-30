/**
    apps/home/app.c : Home application descriptor and runtime
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "app_descriptor.h"

#include "device_view.h"
#include "event.h"
#include "network_session.h"

#include <stddef.h>

static bool menu_active;

static void emit_event( rfscout_event_type_t a_type, int32_t a_value )
{
    rfscout_event_emit( a_type, (uint8_t)RFSCOUT_APP_DEVICE, a_value );
}

static rfscout_app_runtime_result_t home_runtime( const rfscout_app_runtime_event_t *a_event )
{
    if( NULL == a_event )
    {
        return RFSCOUT_APP_RUNTIME_IGNORED;
    }

    if( RFSCOUT_APP_RUNTIME_ENTER == a_event->type )
    {
        menu_active = false;
        device_view_landing();
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_RENDER == a_event->type )
    {
        if( menu_active )
        {
            device_view_render();
        }
        else
        {
            device_view_landing();
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_LEAVE == a_event->type )
    {
        device_view_leave();
        menu_active = false;
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_POLL == a_event->type )
    {
        network_session_poll( a_event->now_us );
        if( menu_active )
        {
            device_view_poll( a_event->now_us );
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }
    if( RFSCOUT_APP_RUNTIME_INPUT != a_event->type )
    {
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( menu_active )
    {
        if( device_view_input( &a_event->input ) )
        {
            menu_active = false;
            device_view_landing();
        }
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    if( RFSCOUT_INPUT_SELECT == a_event->input.type )
    {
        menu_active = true;
        device_view_enter();
        emit_event( RFSCOUT_EVENT_ENTER, 0 );
        return RFSCOUT_APP_RUNTIME_HANDLED;
    }

    return RFSCOUT_APP_RUNTIME_IGNORED;
}

const rfscout_app_descriptor_t rfscout_home_app = {
    .id = RFSCOUT_APP_DEVICE,
    .name = "Home",
    .persistence_key = "device",
    .probe_policy = RFSCOUT_APP_PROBE_NONE,
    .probe = NULL,
    .requires_activation = false,
    .services = RFSCOUT_APP_SERVICE_NETWORK_SESSION | RFSCOUT_APP_SERVICE_OTA_INSTALLER |
                RFSCOUT_APP_SERVICE_OTA_ACQUISITION,
    .runtime = home_runtime,
};
