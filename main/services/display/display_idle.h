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

#include <stdint.h>
#include <stdbool.h>

/** @brief Initialize the global display-idle state from current configuration. */
esp_err_t display_idle_init( void );

/** @brief Poll user activity and apply dim/off/wake transitions. */
void display_idle_poll( void );

/** @brief Consume residual wake gesture input before dispatch to applications. */
bool display_idle_input_should_wake( void );

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

/**
 * @brief Request that the display become visible without synthesizing user input.
 *
 * Device-level warnings use this path so a dimmed/off panel can present urgent
 * status while ordinary wake gestures remain fully consumed by display_idle.
 * The request also clears any queued explicit screen-off transition.
 */
esp_err_t display_idle_request_wake( void );

/**
 * @brief Temporarily keep a dim/off display awake for a device notification.
 *
 * Unlike ordinary user/device wake, this does not move the inactivity origin. When the attention
 * interval expires the original idle deadline immediately becomes authoritative again, allowing a
 * notification-woken panel to return to dim/off without pretending the user interacted.
 */
esp_err_t display_idle_request_attention( uint32_t a_duration_ms );

#endif // _RFSCOUT_DISPLAY_IDLE_H_
