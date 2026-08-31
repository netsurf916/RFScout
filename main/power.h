/**
    power.h : RFScout user-button power control
    Description: Handles configurable short/long user-button gestures and power actions.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_POWER_H_
#define _RFSCOUT_POWER_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    POWER_ACTION_NONE = 0,
    POWER_ACTION_SCREEN_OFF,
    POWER_ACTION_SLEEP,
    POWER_ACTION_POWER_OFF,
    POWER_ACTION_COUNT
} power_action_t;

typedef enum
{
    POWER_HOLD_IDLE = 0,
    POWER_HOLD_COUNTDOWN,
    POWER_HOLD_EXECUTING
} power_hold_state_t;

typedef struct
{
    power_hold_state_t state;
    power_action_t action;
    uint8_t seconds_remaining;
} power_status_t;

/** @brief Initialize user-button power handling. */
esp_err_t power_init( void );

/** @brief Poll the user button and execute configured actions. */
void power_poll( void );

/** @brief Consume an entire currently held gesture because it was used only to wake the display. */
void power_consume_wake_input( void );

/** @brief Return whether the active board can perform an action for the requested gesture. */
bool power_action_supported( power_action_t a_action, bool a_long_press );

/** @brief Return a concise user-facing action name. */
const char *power_action_name( power_action_t a_action );

/** @brief Return the current power-button hold state for UI rendering. */
void power_get_status( power_status_t *a_status );

#endif // _RFSCOUT_POWER_H_
