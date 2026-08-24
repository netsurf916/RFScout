/**
    app_manager.h : RFScout application view manager
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#pragma once
#ifndef _RFSCOUT_APP_MANAGER_H_
#define _RFSCOUT_APP_MANAGER_H_

#include "esp_err.h"

/** @brief Initialize the primary RF view and application manager. */
esp_err_t app_manager_init( void );

/** @brief Process encoder view changes and update the active application. */
void app_manager_poll( void );

#endif // _RFSCOUT_APP_MANAGER_H_
