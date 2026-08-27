/**
    alert.h : RFScout audible alert policy interface
    Description: Defines device-wide Off, Tone, and per-detection Morse notification policy.
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

/** @brief Stable detection categories that can have independent Morse characters. */
typedef enum
{
    RFSCOUT_NOTIFICATION_FLOCK = 0,
    RFSCOUT_NOTIFICATION_BODY_CAMERA,
    RFSCOUT_NOTIFICATION_DRONE,
    RFSCOUT_NOTIFICATION_CAMERA,
    RFSCOUT_NOTIFICATION_SMART_GLASSES,
    RFSCOUT_NOTIFICATION_ACOUSTIC_SENSOR,
    RFSCOUT_NOTIFICATION_SURVEILLANCE_OTHER,
    RFSCOUT_NOTIFICATION_CC1101_NARROW_ISM,
    RFSCOUT_NOTIFICATION_CC1101_REMOTE_SENSOR,
    RFSCOUT_NOTIFICATION_CC1101_CARRIER,
    RFSCOUT_NOTIFICATION_CC1101_BURSTY,
    RFSCOUT_NOTIFICATION_CC1101_WIDEBAND,
    RFSCOUT_NOTIFICATION_NRF24_ESB,
    RFSCOUT_NOTIFICATION_NRF24_INPUT,
    RFSCOUT_NOTIFICATION_NRF24_SENSOR,
    RFSCOUT_NOTIFICATION_NRF24_WIFI_LIKE,
    RFSCOUT_NOTIFICATION_NRF24_BLE_LIKE,
    RFSCOUT_NOTIFICATION_NRF24_LOGITECH_LIKE,
    RFSCOUT_NOTIFICATION_TYPE_COUNT
} rfscout_notification_type_t;

/** @brief Initialize asynchronous audible-alert output with alerts disabled. */
esp_err_t alert_init( void );

/** @brief Return a stable user-facing name for an alert mode. */
const char *alert_mode_name( rfscout_alert_mode_t a_mode );

/** @brief Return a stable user-facing label for a notification category. */
const char *alert_notification_name( rfscout_notification_type_t a_type );

/** @brief Return the compiled default Morse character for a notification category. */
char alert_notification_default_character( rfscout_notification_type_t a_type );

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

/**
 * @brief Request an audible alert using the supplied Morse character when Morse mode is active.
 * @details Tone and Off modes retain their normal behavior; the character only selects Morse output.
 * @return True for Off/no-op or when the requested sound was queued.
 */
bool alert_request_character( char a_character );

#endif // _RFSCOUT_ALERT_H_
