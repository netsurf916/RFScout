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

#define RFSCOUT_INPUT_LOCK_HOLD_US 2000000LL

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
    RFSCOUT_INPUT_PROVIDER_ROTARY_BUTTONS,
    RFSCOUT_INPUT_PROVIDER_TRACKBALL_BUTTONS
} rfscout_input_provider_t;

esp_err_t input_init( void );
void input_poll( void );
bool input_activity_pending( void );
void input_consume_wake( void );
bool input_get( rfscout_input_event_t *a_event );

/**
 * @brief Temporarily suspend the physical primary Select source while hardware reuses its GPIO.
 * @note Navigation inputs remain active. The matching resume call discards any button transition
 *       that occurred while the GPIO was unavailable.
 */
void input_primary_select_suspend( void );

/** @brief Restore the physical primary Select source after a temporary hardware conflict. */
void input_primary_select_resume( void );

/**
 * @brief Submit a semantic Select from another authoritative input provider.
 * @details This follows the same device-wide input-lock policy as the physical Select source.
 */
void input_inject_select( void );

/**
 * @brief Return whether device-wide user input is locked.
 * @details This is semantic policy, not provider state. All authoritative user-input paths must
 *          honor it, including rotary/trackball events, touch/pointer dispatch, and future
 *          keyboard character events. Physical activity may still be observed for display-wake
 *          and for the deliberate hold gesture used to unlock input.
 */
bool input_locked( void );
rfscout_input_provider_t input_provider( void );

#endif // _RFSCOUT_INPUT_H_
