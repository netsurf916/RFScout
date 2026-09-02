/**
    event.c : RFScout semantic event dispatcher
    Description: Provides a small fixed observer set without a task, heap allocation, or event queue.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include <stddef.h>

#define RFSCOUT_EVENT_OBSERVER_MAX 8U

typedef struct
{
    rfscout_event_handler_t handler;
    void *context;
} event_observer_t;

static event_observer_t observers[ RFSCOUT_EVENT_OBSERVER_MAX ];
static portMUX_TYPE event_lock = portMUX_INITIALIZER_UNLOCKED;

rfscout_audio_category_t rfscout_event_audio_category( rfscout_event_type_t a_type )
{
    switch( a_type )
    {
        case RFSCOUT_EVENT_NAVIGATE:
        case RFSCOUT_EVENT_ACTIVATE:
        case RFSCOUT_EVENT_ENTER:
        case RFSCOUT_EVENT_BACK:
        case RFSCOUT_EVENT_ENABLE:
        case RFSCOUT_EVENT_DISABLE:
        case RFSCOUT_EVENT_INPUT_LOCKED:
        case RFSCOUT_EVENT_INPUT_UNLOCKED:
            return RFSCOUT_AUDIO_CATEGORY_UI;
        case RFSCOUT_EVENT_DETECTED:
        case RFSCOUT_EVENT_POWER_CONNECTED:
        case RFSCOUT_EVENT_POWER_DISCONNECTED:
        case RFSCOUT_EVENT_STORAGE_MOUNTED:
        case RFSCOUT_EVENT_STORAGE_UNMOUNTED:
        case RFSCOUT_EVENT_APP_AVAILABLE:
        case RFSCOUT_EVENT_APP_UNAVAILABLE:
            return RFSCOUT_AUDIO_CATEGORY_EVENT;
        case RFSCOUT_EVENT_NOTIFICATION:
        case RFSCOUT_EVENT_WARNING:
        case RFSCOUT_EVENT_ERROR:
        case RFSCOUT_EVENT_STORAGE_MOUNT_FAILED:
            return RFSCOUT_AUDIO_CATEGORY_ALERT;
        case RFSCOUT_EVENT_THEME_CHANGED:
        case RFSCOUT_EVENT_AUDIO_POLICY_CHANGED:
        case RFSCOUT_EVENT_TYPE_COUNT:
        default:
            return RFSCOUT_AUDIO_CATEGORY_COUNT;
    }
}

bool rfscout_event_subscribe( rfscout_event_handler_t a_handler, void *a_context )
{
    if( NULL == a_handler )
    {
        return false;
    }

    bool added = false;
    portENTER_CRITICAL( &event_lock );
    for( size_t index = 0U; index < RFSCOUT_EVENT_OBSERVER_MAX; ++index )
    {
        if( observers[ index ].handler == a_handler && observers[ index ].context == a_context )
        {
            added = true;
            break;
        }
        if( NULL == observers[ index ].handler )
        {
            observers[ index ].handler = a_handler;
            observers[ index ].context = a_context;
            added = true;
            break;
        }
    }
    portEXIT_CRITICAL( &event_lock );
    return added;
}

void rfscout_event_unsubscribe( rfscout_event_handler_t a_handler, void *a_context )
{
    portENTER_CRITICAL( &event_lock );
    for( size_t index = 0U; index < RFSCOUT_EVENT_OBSERVER_MAX; ++index )
    {
        if( observers[ index ].handler == a_handler && observers[ index ].context == a_context )
        {
            observers[ index ].handler = NULL;
            observers[ index ].context = NULL;
            break;
        }
    }
    portEXIT_CRITICAL( &event_lock );
}

void rfscout_event_emit( rfscout_event_type_t a_type, uint8_t a_source, int32_t a_value )
{
    if( a_type >= RFSCOUT_EVENT_TYPE_COUNT )
    {
        return;
    }

    event_observer_t snapshot[ RFSCOUT_EVENT_OBSERVER_MAX ];
    portENTER_CRITICAL( &event_lock );
    for( size_t index = 0U; index < RFSCOUT_EVENT_OBSERVER_MAX; ++index )
    {
        snapshot[ index ] = observers[ index ];
    }
    portEXIT_CRITICAL( &event_lock );

    const rfscout_event_t event = { .type = a_type, .source = a_source, .value = a_value };
    for( size_t index = 0U; index < RFSCOUT_EVENT_OBSERVER_MAX; ++index )
    {
        if( NULL != snapshot[ index ].handler )
        {
            snapshot[ index ].handler( &event, snapshot[ index ].context );
        }
    }
}
