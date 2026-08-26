/**
    app_manager.h : RFScout application view manager
    Description: Declares application navigation order and the primary application lifecycle interface.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_MANAGER_H_
#define _RFSCOUT_APP_MANAGER_H_

#include "esp_err.h"

/** @brief Full-screen applications in encoder navigation order. */
typedef enum
{
    RFSCOUT_APP_WIFI_BT = 0,
    RFSCOUT_APP_CC1101,
    RFSCOUT_APP_NRF24,
    RFSCOUT_APP_AUDIO,
    RFSCOUT_APP_THERMAL,
    RFSCOUT_APP_DEVICE,
    RFSCOUT_APP_COUNT
} rfscout_app_id_t;

/** @brief Initialize the primary RF view and application manager. */
esp_err_t app_manager_init( void );

/** @brief Process app navigation, lifecycle, button input, and active rendering. */
void app_manager_poll( void );

/** @brief Return the application currently visible on the LCD. */
rfscout_app_id_t app_manager_current( void );

#endif // _RFSCOUT_APP_MANAGER_H_
