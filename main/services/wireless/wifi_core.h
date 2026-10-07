/**
    wifi_core.h : shared ESP-IDF Wi-Fi substrate
    Description: Idempotent inactive initialization shared by monitor and station services.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_WIFI_CORE_H_
#define _RFSCOUT_WIFI_CORE_H_

#include "esp_err.h"

/**
 * @brief Initialize the shared ESP-NETIF/event-loop/Wi-Fi driver substrate.
 * @details Idempotent and leaves Wi-Fi inactive in WIFI_MODE_NULL.
 */
esp_err_t wifi_core_init( void );

#endif // _RFSCOUT_WIFI_CORE_H_
