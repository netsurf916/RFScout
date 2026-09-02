/**
    system_events.h : RFScout system-event history and notification policy
    Description: Records noteworthy semantic system events and exposes transient notification state.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_SYSTEM_EVENTS_H_
#define _RFSCOUT_SYSTEM_EVENTS_H_

#include "event.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_SYSTEM_EVENT_HISTORY_CAPACITY 32U

typedef enum
{
    RFSCOUT_SYSTEM_EVENT_INFO = 0,
    RFSCOUT_SYSTEM_EVENT_WARNING,
    RFSCOUT_SYSTEM_EVENT_CRITICAL
} rfscout_system_event_severity_t;

typedef struct
{
    int64_t monotonic_us;
    rfscout_event_t event;
    rfscout_system_event_severity_t severity;
} rfscout_system_event_entry_t;

typedef struct
{
    bool active;
    rfscout_system_event_severity_t severity;
    char title[ 32 ];
    char detail[ 48 ];
} rfscout_system_event_notification_t;

/** @brief Subscribe the system-event history/presentation consumer to the semantic event bus. */
esp_err_t system_events_init( void );

/**
 * @brief Enable interactive event notifications after the shared display-idle service is ready.
 * @details Events observed before this point are still logged and retained in history but do not
 *          wake the panel, play runtime event feedback, or display an overlay.
 */
void system_events_enable_notifications( void );

/** @brief Return whether runtime event notifications are enabled. */
bool system_events_notifications_enabled( void );

/**
 * @brief Poll derived hardware state and expire transient event presentation.
 * @details Call after battery/application availability polling so state changes are observed once.
 */
void system_events_poll( void );

/** @brief Return the number of currently retained RAM-only history entries. */
size_t system_events_history_count( void );

/**
 * @brief Read one retained event by age, where index zero is the newest entry.
 * @return True when the requested entry exists.
 */
bool system_events_history_get( size_t a_index, rfscout_system_event_entry_t *a_entry );

/** @brief Return the currently active transient system-event notification, if any. */
bool system_events_notification_get( rfscout_system_event_notification_t *a_notification );

#endif // _RFSCOUT_SYSTEM_EVENTS_H_
