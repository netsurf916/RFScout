/**
    power.h : RFScout user-button power control
    Description: Handles sleep and full power-off gestures from the user button.
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
    POWER_HOLD_IDLE = 0,
    POWER_HOLD_COUNTDOWN,
    POWER_HOLD_SHUTDOWN
} power_hold_state_t;

typedef struct
{
    power_hold_state_t state;
    uint8_t seconds_remaining;
} power_status_t;

/** @brief Initialize user-button power handling. */
esp_err_t power_init( void );

/** @brief Poll the user button and execute sleep/shutdown actions. */
void power_poll( void );

/** @brief Consume a currently held user-button gesture because it was used only to wake the display. */
void power_consume_wake_input( void );

/** @brief Return the current power-button hold state for UI rendering. */
void power_get_status( power_status_t *a_status );

#endif // _RFSCOUT_POWER_H_
