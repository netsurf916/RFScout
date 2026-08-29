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

typedef enum
{
    RFSCOUT_INPUT_NONE = 0,
    RFSCOUT_INPUT_NAVIGATION,
    RFSCOUT_INPUT_SELECT,
    RFSCOUT_INPUT_LONG_SELECT,
    RFSCOUT_INPUT_BACK,
    RFSCOUT_INPUT_CHARACTER
} rfscout_input_type_t;

typedef struct
{
    rfscout_input_type_t type;
    int32_t value;
} rfscout_input_event_t;

/** @brief Initialize shared board GPIO and peripherals. */
esp_err_t platform_init( void );

/** @brief Poll low-rate board inputs and housekeeping. */
void platform_poll( void );

/**
 * @brief Consume one board-neutral user-input event.
 *
 * Navigation values are signed and may represent multiple accumulated steps.
 * Character events carry the input character/codepoint in value. Boards may
 * expose richer physical controls while keeping application navigation free of
 * encoder, keyboard, touch, or trackball implementation details.
 *
 * @param[out] a_event Receives the next available event.
 * @return true when an event was returned, false when no input is pending.
 */
bool platform_input_get( rfscout_input_event_t *a_event );

/**
 * @brief Consume accumulated encoder movement.
 * @return Negative for counter-clockwise, positive for clockwise, or zero. Always zero while the
 *         global input lock is active.
 * @deprecated Application code should migrate to platform_input_get().
 */
int platform_encoder_delta( void );

/**
 * @brief Consume one debounced encoder-center-button press.
 * @return true once for each completed short press while input is unlocked.
 * @deprecated Application code should migrate to platform_input_get().
 */
bool platform_encoder_button_pressed( void );

/**
 * @brief Consume one debounced encoder-center-button long press.
 * @return true once on release after the normal long-press interval, provided the hold did not
 *         reach the global input-lock interval.
 * @deprecated Application code should migrate to platform_input_get().
 */
bool platform_encoder_button_long_pressed( void );

/**
 * @brief Return whether ordinary user input is globally locked.
 *
 * The current T-Embed implementation toggles this state by holding the center
 * button continuously for the lock interval. Future boards may provide an
 * equivalent board-appropriate gesture.
 */
bool platform_input_locked( void );

/** @brief Set the visual surveillance-alert state. */
void platform_set_alert_led( bool a_active );

/** @brief Enable or disable the requested audio tone. */
void platform_audio_tone( bool a_enabled, uint16_t a_frequency_hz, void *a_context );

#endif // _RFSCOUT_PLATFORM_H_
