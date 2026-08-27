/**
    alert.h : RFScout audible alert policy interface
    Description: Defines device-wide Off, Tone, per-detection Morse, and notification volume policy.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_ALERT_H_
#define _RFSCOUT_ALERT_H_

#include "app_manager.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_ALERT_VOLUME_DEFAULT_PERCENT 50U
#define RFSCOUT_ALERT_VOLUME_MAX_PERCENT 100U

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

/**
 * @brief Canonical metadata for one configurable alert item.
 *
 * The notification enum and the descriptor table in alert.c intentionally have a 1:1
 * correspondence. When adding or removing a detection category, update both together. The
 * Device UI and SD configuration iterate these descriptors automatically. Detection producers
 * must also map their classifier output to the new enum value: WiFi/BT surveillance mappings live
 * in rfscout.c, while CC1101 and nRF24 mappings live in app_manager.c.
 */
typedef struct
{
    rfscout_notification_type_t type;
    rfscout_app_id_t app;
    const char *config_key;
    const char *label;
    char default_character;
} rfscout_notification_descriptor_t;

/** @brief Initialize asynchronous audible-alert output with alerts disabled. */
esp_err_t alert_init( void );

/** @brief Return a stable user-facing name for an alert mode. */
const char *alert_mode_name( rfscout_alert_mode_t a_mode );

/** @brief Return the canonical descriptor for one notification category. */
const rfscout_notification_descriptor_t *
alert_notification_descriptor( rfscout_notification_type_t a_type );

/** @brief Return a stable user-facing label for a notification category. */
const char *alert_notification_name( rfscout_notification_type_t a_type );

/** @brief Return the stable SD configuration key for a notification category. */
const char *alert_notification_config_key( rfscout_notification_type_t a_type );

/** @brief Return the application that owns one notification category. */
rfscout_app_id_t alert_notification_app( rfscout_notification_type_t a_type );

/** @brief Return the compiled default Morse character for a notification category. */
char alert_notification_default_character( rfscout_notification_type_t a_type );

/** @brief Return the currently selected alert mode. */
rfscout_alert_mode_t alert_mode( void );

/** @brief Change the runtime alert mode. */
void alert_set_mode( rfscout_alert_mode_t a_mode );

/** @brief Return the current global notification volume percentage. */
uint8_t alert_volume_percent( void );

/** @brief Change the global notification volume percentage. */
bool alert_set_volume_percent( uint8_t a_percent );

/** @brief Return the legacy generic single-character Morse alert. */
char alert_morse_character( void );

/** @brief Change the legacy generic Morse alert when the character is supported. */
bool alert_set_morse_character( char a_character );

/** @brief Return whether one character can be rendered as a Morse alert. */
bool alert_morse_character_supported( char a_character );

/**
 * @brief Request the generic audible alert without blocking the caller.
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
