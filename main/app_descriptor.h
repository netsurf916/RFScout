/**
    app_descriptor.h : RFScout application descriptor contract
    Description: Defines compiled-in app metadata and platform capability dependencies.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_DESCRIPTOR_H_
#define _RFSCOUT_APP_DESCRIPTOR_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Full-screen applications in encoder navigation order. */
typedef enum
{
    RFSCOUT_APP_SYSTEM_EVENTS = 0,
    RFSCOUT_APP_DEVICE,
    RFSCOUT_APP_WIFI_BT,
    RFSCOUT_APP_CC1101,
    RFSCOUT_APP_NRF24,
    RFSCOUT_APP_AUDIO,
    RFSCOUT_APP_THERMAL,
    RFSCOUT_APP_COUNT
} rfscout_app_id_t;

/** @brief When an application's hardware availability should be probed. */
typedef enum
{
    RFSCOUT_APP_PROBE_NONE = 0,
    RFSCOUT_APP_PROBE_STARTUP,
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
    RFSCOUT_APP_SERVICE_RECORDER = 1U << 4
} rfscout_app_service_mask_t;

/**
 * @brief Self-contained metadata exported by each compiled-in application module.
 * @details Probe implementations may use platform/service APIs but must leave hardware inactive.
 *          Service lifetime is declared as capability bits and remains implemented by the platform.
 */
typedef struct
{
    rfscout_app_id_t id;
    const char *name;
    rfscout_app_probe_policy_t probe_policy;
    esp_err_t ( *probe )( void );
    bool requires_activation;
    uint32_t services;
} rfscout_app_descriptor_t;

#endif // _RFSCOUT_APP_DESCRIPTOR_H_
