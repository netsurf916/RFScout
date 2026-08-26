/**
    alert.h : RFScout audible alert policy interface
    Description: Defines user-selectable Off, Tone, and single-character Morse alert modes.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_ALERT_H_
#define _RFSCOUT_ALERT_H_

#include "esp_err.h"

#include <stdbool.h>

typedef enum
{
    RFSCOUT_ALERT_OFF = 0,
    RFSCOUT_ALERT_TONE,
    RFSCOUT_ALERT_MORSE,
    RFSCOUT_ALERT_MODE_COUNT
} rfscout_alert_mode_t;

/** @brief Initialize asynchronous audible-alert output with alerts disabled. */
esp_err_t alert_init( void );

/** @brief Return a stable user-facing name for an alert mode. */
const char *alert_mode_name( rfscout_alert_mode_t a_mode );

/** @brief Return the currently selected alert mode. */
rfscout_alert_mode_t alert_mode( void );

/** @brief Change the runtime alert mode. */
void alert_set_mode( rfscout_alert_mode_t a_mode );

/** @brief Return the configured single-character Morse alert. */
char alert_morse_character( void );

/** @brief Change the single-character Morse alert when the character is supported. */
bool alert_set_morse_character( char a_character );

/** @brief Return whether one character can be rendered as a Morse alert. */
bool alert_morse_character_supported( char a_character );

/**
 * @brief Request the configured audible alert without blocking the caller.
 * @return True for Off/no-op or when the requested sound was queued.
 */
bool alert_request( void );

#endif // _RFSCOUT_ALERT_H_
