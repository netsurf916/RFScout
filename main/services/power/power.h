/**
    power.h : RFScout user-button power control
    Description: Handles configurable short/long user-button gestures and semantic power actions.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_POWER_H_
#define _RFSCOUT_POWER_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define POWER_STATUS_MESSAGE_MAX 32U
#define POWER_STATUS_DETAIL_MAX 32U

/** @brief Board-neutral state changes that may be requested by controls or policy. */
typedef enum
{
    POWER_ACTION_NONE = 0,
    POWER_ACTION_SCREEN_OFF,
    POWER_ACTION_SLEEP,
    POWER_ACTION_POWER_OFF,
    POWER_ACTION_COUNT
} power_action_t;

/** @brief Presentation state for a pending or executing power transition. */
typedef enum
{
    POWER_HOLD_IDLE = 0,
    POWER_HOLD_COUNTDOWN,
    POWER_HOLD_EXECUTING
} power_hold_state_t;

/** @brief Source of a non-interactive protective power transition. */
typedef enum
{
    POWER_TRANSITION_NONE = 0,
    POWER_TRANSITION_LOW_BATTERY,
    POWER_TRANSITION_HIGH_TEMPERATURE
} power_transition_reason_t;

/** @brief UI-visible description of the current power transition. */
typedef struct
{
    power_hold_state_t state;
    power_action_t action;
    power_transition_reason_t reason;
    uint8_t seconds_remaining;
    char message[ POWER_STATUS_MESSAGE_MAX ];
    char detail[ POWER_STATUS_DETAIL_MAX ];
} power_status_t;

/** @brief Initialize user-button power handling. */
esp_err_t power_init( void );

/** @brief Poll the user button and execute configured actions. */
void power_poll( void );

/** @brief Consume an entire currently held gesture because it was used only to wake the display. */
void power_consume_wake_input( void );

/** @brief Return whether the active board can perform the semantic action at all. */
bool power_action_available( power_action_t a_action );

/** @brief Return whether the active board can perform an action for the requested gesture. */
bool power_action_supported( power_action_t a_action, bool a_long_press );

/** @brief Return a concise user-facing action name. */
const char *power_action_name( power_action_t a_action );

/**
 * @brief Request a delayed protective power transition using the shared overlay.
 * @return True when the transition was accepted or the same reason is already pending.
 */
bool power_request_transition( power_transition_reason_t a_reason,
                               power_action_t a_action,
                               const char *a_message,
                               uint8_t a_delay_seconds );

/** @brief Cancel a pending protective transition when its condition clears. */
void power_cancel_transition( power_transition_reason_t a_reason );

/** @brief Return the current power-button transition state for UI rendering. */
void power_get_status( power_status_t *a_status );

#endif // _RFSCOUT_POWER_H_
