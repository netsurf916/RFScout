/**
    app_descriptor.h : RFScout application descriptor contract
    Description: Defines compiled-in app metadata, runtime dispatch, and platform capability dependencies.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_DESCRIPTOR_H_
#define _RFSCOUT_APP_DESCRIPTOR_H_

#include "app_view.h"
#include "input.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Applications in global navigation order. */
typedef enum
{
    RFSCOUT_APP_SYSTEM_EVENTS = 0,
    RFSCOUT_APP_DEVICE,
    RFSCOUT_APP_WIFI_BT,
    RFSCOUT_APP_CC1101,
    RFSCOUT_APP_NRF24,
    RFSCOUT_APP_AUDIO,
    RFSCOUT_APP_THERMAL,
    RFSCOUT_APP_AUDIO_SPECTRUM,
    RFSCOUT_APP_COUNT
} rfscout_app_id_t;

/** @brief Whether an application has a retry-safe hot-plug hardware probe. */
typedef enum
{
    RFSCOUT_APP_PROBE_NONE = 0,
    RFSCOUT_APP_PROBE_HOTPLUG
} rfscout_app_probe_policy_t;

/** @brief Platform-managed asynchronous services consumed by an app session. */
typedef enum
{
    RFSCOUT_APP_SERVICE_NONE = 0U,
    RFSCOUT_APP_SERVICE_WIFI_MONITOR = 1U << 0,
    RFSCOUT_APP_SERVICE_BLE_MONITOR = 1U << 1,
    RFSCOUT_APP_SERVICE_CC1101 = 1U << 2,
    RFSCOUT_APP_SERVICE_NRF24 = 1U << 3,
    RFSCOUT_APP_SERVICE_AUDIO_CAPTURE = 1U << 4,
    RFSCOUT_APP_SERVICE_WIFI_STATION = 1U << 5,
    RFSCOUT_APP_SERVICE_OTA_INSTALLER = 1U << 6,
    RFSCOUT_APP_SERVICE_OTA_ACQUISITION = 1U << 7
} rfscout_app_service_mask_t;

/** @brief Runtime events delivered by the application manager to the active app module. */
typedef enum
{
    RFSCOUT_APP_RUNTIME_ENTER = 0,
    RFSCOUT_APP_RUNTIME_ACTIVATE,
    RFSCOUT_APP_RUNTIME_INPUT,
    RFSCOUT_APP_RUNTIME_POLL,
    RFSCOUT_APP_RUNTIME_LEAVE
} rfscout_app_runtime_event_type_t;

/** @brief One application-runtime dispatch. */
typedef struct
{
    rfscout_app_runtime_event_type_t type;
    int64_t now_us;
    bool activated;
    /** Managed presentation view assigned to this session; supplied on ENTER only. */
    const rfscout_view_t *view;
    rfscout_input_event_t input;
} rfscout_app_runtime_event_t;

/** @brief Result returned by an app runtime handler. */
typedef enum
{
    /** The event was not app-local; core may apply its global fallback. */
    RFSCOUT_APP_RUNTIME_IGNORED = 0,
    /** The app consumed the event or completed its periodic work. */
    RFSCOUT_APP_RUNTIME_HANDLED,
    /** The app lost a required runtime dependency and requests a transition to Home. */
    RFSCOUT_APP_RUNTIME_REQUEST_HOME
} rfscout_app_runtime_result_t;

typedef rfscout_app_runtime_result_t ( *rfscout_app_runtime_handler_t )(
    const rfscout_app_runtime_event_t *a_event );

/**
 * @brief Self-contained metadata and runtime contract exported by each compiled-in app module.
 * @details Probe implementations may use platform/service APIs but must leave hardware inactive.
 *          Service lifetime is declared as capability bits and remains implemented by the platform.
 *          Runtime handlers own app-local UI/state but not driver teardown or memory reachability.
 *          On ENTER, event.view identifies the managed content surface for the session; applications
 *          may retain that immutable pointer for the session lifetime and must not assume physical
 *          LCD dimensions. The optional leave-prepare hook is reserved for app-specific transition
 *          vetoes; ordinary apps should leave it NULL.
 */
typedef struct
{
    rfscout_app_id_t id;
    const char *name;
    const char *persistence_key;
    rfscout_app_probe_policy_t probe_policy;
    esp_err_t ( *probe )( void );
    bool requires_activation;
    uint32_t services;
    rfscout_app_runtime_handler_t runtime;
    esp_err_t ( *prepare_leave )( void );
} rfscout_app_descriptor_t;

#endif // _RFSCOUT_APP_DESCRIPTOR_H_
