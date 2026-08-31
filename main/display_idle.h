/**
    display_idle.h : RFScout global display-idle policy
    Description: Owns LCD dim/off/wake presentation state independently from application execution.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DISPLAY_IDLE_H_
#define _RFSCOUT_DISPLAY_IDLE_H_

#include "esp_err.h"

/** @brief Initialize the global display-idle state from current configuration. */
esp_err_t display_idle_init( void );

/** @brief Poll user activity and apply dim/off/wake transitions. */
void display_idle_poll( void );

/**
 * @brief Request immediate entry into the normal display-off state.
 *
 * This uses the same state transition as inactivity-driven screen-off so wake
 * detection, gesture consumption, framebuffer behavior, and LCD restoration
 * remain owned by the display-idle policy.
 *
 * @return ESP_OK when the display is already off or the transition succeeds.
 */
esp_err_t display_idle_request_off( void );

#endif // _RFSCOUT_DISPLAY_IDLE_H_
