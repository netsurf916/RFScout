/**
    platform.h : RFScout board-level services
    Description: Exposes non-radio hardware services used by the application.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_PLATFORM_H_
#define _RFSCOUT_PLATFORM_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Initialize shared board GPIO and peripherals. */
esp_err_t platform_init( void );

/** @brief Poll low-rate board inputs and housekeeping. */
void platform_poll( void );

/**
 * @brief Consume accumulated encoder movement.
 * @return Negative for counter-clockwise, positive for clockwise, or zero. Always zero while the
 *         global input lock is active.
 */
int platform_encoder_delta( void );

/**
 * @brief Consume one debounced encoder-center-button press.
 * @return true once for each completed short press while input is unlocked.
 */
bool platform_encoder_button_pressed( void );

/**
 * @brief Consume one debounced encoder-center-button long press.
 * @return true once on release after the normal long-press interval, provided the hold did not
 *         reach the global input-lock interval.
 */
bool platform_encoder_button_long_pressed( void );

/**
 * @brief Return whether rotary and ordinary center-button input is globally locked.
 *
 * Holding the center button continuously for the lock interval toggles this state. The same gesture
 * remains available while locked so the user can always unlock the controls.
 */
bool platform_input_locked( void );

/** @brief Set the visual surveillance-alert state. */
void platform_set_alert_led( bool a_active );

/** @brief Enable or disable the requested audio tone. */
void platform_audio_tone( bool a_enabled, uint16_t a_frequency_hz, void *a_context );

#endif // _RFSCOUT_PLATFORM_H_
