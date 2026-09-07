/**
    alert.h : RFScout audible notification presentation policy
    Description: Applies global mode/volume and environmental policy to semantic notification events.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_ALERT_H_
#define _RFSCOUT_ALERT_H_

#include "notification.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#define RFSCOUT_ALERT_VOLUME_DEFAULT_PERCENT 30U
#define RFSCOUT_ALERT_VOLUME_MAX_PERCENT 100U

typedef enum
{
    RFSCOUT_ALERT_OFF = 0,
    RFSCOUT_ALERT_TONE,
    RFSCOUT_ALERT_MORSE,
    RFSCOUT_ALERT_MODE_COUNT
} rfscout_alert_mode_t;

/** @brief Initialize notification presentation and subscribe it to semantic events. */
esp_err_t alert_init( void );

/** @brief Return a stable user-facing name for an alert presentation mode. */
const char *alert_mode_name( rfscout_alert_mode_t a_mode );

/* Compatibility aliases for existing configuration/UI code. Notification vocabulary now lives in
 * notification.[ch], not in the audio presentation layer. */
const rfscout_notification_descriptor_t *
alert_notification_descriptor( rfscout_notification_type_t a_type );
const char *alert_notification_name( rfscout_notification_type_t a_type );
const char *alert_notification_config_key( rfscout_notification_type_t a_type );
rfscout_app_id_t alert_notification_app( rfscout_notification_type_t a_type );
char alert_notification_default_character( rfscout_notification_type_t a_type );

rfscout_alert_mode_t alert_mode( void );
void alert_set_mode( rfscout_alert_mode_t a_mode );
uint8_t alert_volume_percent( void );
bool alert_set_volume_percent( uint8_t a_percent );

/** @brief Return/change the legacy generic Morse character used by alert_request(). */
char alert_morse_character( void );
bool alert_set_morse_character( char a_character );
bool alert_morse_character_supported( char a_character );

/** @brief Request a legacy generic alert through the current global presentation policy. */
bool alert_request( void );
bool alert_request_character( char a_character );

/**
 * @brief Compatibility wrapper that emits one semantic application notification.
 * @details The event subscriber performs app/theme/global policy resolution asynchronously from
 *          the producer's point of view. A true return value means the category was valid/emitted,
 *          not that speaker output was guaranteed.
 */
bool alert_request_notification( rfscout_notification_type_t a_type );

#endif // _RFSCOUT_ALERT_H_