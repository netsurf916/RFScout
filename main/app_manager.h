/**
    app_manager.h : RFScout application view manager
    Description: Declares primary application navigation and invocation interfaces.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_APP_MANAGER_H_
#define _RFSCOUT_APP_MANAGER_H_

#include "app_descriptor.h"
#include "esp_err.h"

#include <stdbool.h>

/** @brief Initialize the primary application manager. */
esp_err_t app_manager_init( void );

/** @brief Process app navigation, semantic input, and active rendering. */
void app_manager_poll( void );

/** @brief Return the application currently visible on the LCD. */
rfscout_app_id_t app_manager_current( void );

/**
 * @brief Request a transition to one available application.
 *
 * System-owned navigation surfaces use this rather than synthesizing board-specific
 * navigation input. Normal lifecycle prepare/commit semantics still apply.
 */
bool app_manager_select( rfscout_app_id_t a_app );

#endif // _RFSCOUT_APP_MANAGER_H_
