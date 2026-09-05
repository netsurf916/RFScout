/**
    input.h : RFScout semantic input service
    Description: Normalizes board controls into focused application input while preserving wake activity.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_INPUT_H_
#define _RFSCOUT_INPUT_H_

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

typedef enum
{
    RFSCOUT_INPUT_PROVIDER_NONE = 0,
    RFSCOUT_INPUT_PROVIDER_ROTARY_BUTTONS
} rfscout_input_provider_t;

/** @brief Initialize the board-selected input provider(s). */
esp_err_t input_init( void );

/** @brief Poll low-rate gesture processing for the active input provider(s). */
void input_poll( void );

/** @brief Return whether any physical user activity is pending, independent of input lock. */
bool input_activity_pending( void );

/** @brief Discard the complete physical gesture currently being used to wake the display. */
void input_consume_wake( void );

/**
 * @brief Consume one board-neutral semantic input event.
 * @details Navigation values are signed and may represent multiple accumulated steps. Character
 *          events carry the input character/codepoint in value. Physical providers remain hidden
 *          from applications.
 */
bool input_get( rfscout_input_event_t *a_event );

/** @brief Return whether ordinary semantic user input is globally locked. */
bool input_locked( void );

/** @brief Return the currently selected primary provider for diagnostics. */
rfscout_input_provider_t input_provider( void );

#endif // _RFSCOUT_INPUT_H_
