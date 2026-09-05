/**
    event.h : RFScout semantic event interface
    Description: Defines application-neutral events emitted after RFScout has accepted a state change.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_EVENT_H_
#define _RFSCOUT_EVENT_H_

#include "audio_policy.h"

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_EVENT_SOURCE_SYSTEM 0xffU

typedef enum
{
    RFSCOUT_EVENT_NAVIGATE = 0,
    RFSCOUT_EVENT_ACTIVATE,
    RFSCOUT_EVENT_ENTER,
    RFSCOUT_EVENT_BACK,
    RFSCOUT_EVENT_ENABLE,
    RFSCOUT_EVENT_DISABLE,
    RFSCOUT_EVENT_INPUT_LOCKED,
    RFSCOUT_EVENT_INPUT_UNLOCKED,
    RFSCOUT_EVENT_THEME_CHANGED,
    RFSCOUT_EVENT_AUDIO_POLICY_CHANGED,
    RFSCOUT_EVENT_DETECTED,
    RFSCOUT_EVENT_NOTIFICATION,
    RFSCOUT_EVENT_WARNING,
    RFSCOUT_EVENT_ERROR,
    RFSCOUT_EVENT_POWER_CONNECTED,
    RFSCOUT_EVENT_POWER_DISCONNECTED,
    RFSCOUT_EVENT_STORAGE_MOUNTED,
    RFSCOUT_EVENT_STORAGE_MOUNT_FAILED,
    RFSCOUT_EVENT_STORAGE_UNMOUNTED,
    RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNTED,
    RFSCOUT_EVENT_INTERNAL_STORAGE_FORMATTED,
    RFSCOUT_EVENT_INTERNAL_STORAGE_MOUNT_FAILED,
    RFSCOUT_EVENT_CONFIG_IMPORTED,
    RFSCOUT_EVENT_CONFIG_DEFAULTED,
    RFSCOUT_EVENT_APP_DISCOVERED,
    RFSCOUT_EVENT_APP_AVAILABLE,
    RFSCOUT_EVENT_APP_UNAVAILABLE,
    RFSCOUT_EVENT_APP_PROBE_ERROR,
    RFSCOUT_EVENT_DISPLAY_PROVIDER_SELECTED,
    RFSCOUT_EVENT_INPUT_PROVIDER_SELECTED,
    RFSCOUT_EVENT_TYPE_COUNT
} rfscout_event_type_t;

typedef struct
{
    rfscout_event_type_t type;
    uint8_t source;
    int32_t value;
} rfscout_event_t;

typedef void ( *rfscout_event_handler_t )( const rfscout_event_t *a_event, void *a_context );

/** @brief Return the semantic audio class for an event, or COUNT when it is silent by definition. */
rfscout_audio_category_t rfscout_event_audio_category( rfscout_event_type_t a_type );

/** @brief Register one synchronous semantic-event observer. */
bool rfscout_event_subscribe( rfscout_event_handler_t a_handler, void *a_context );

/** @brief Remove one previously registered observer. */
void rfscout_event_unsubscribe( rfscout_event_handler_t a_handler, void *a_context );

/**
 * @brief Publish an accepted semantic event to all observers.
 * @note Delivery is synchronous and handlers must remain short and nonblocking.
 */
void rfscout_event_emit( rfscout_event_type_t a_type, uint8_t a_source, int32_t a_value );

#endif // _RFSCOUT_EVENT_H_
