/**
    alert.h : RFScout audible notification policy interface
    Description: Defines global notification presentation while allowing per-app notification gates.
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

typedef struct
{
    rfscout_notification_type_t type;
    rfscout_app_id_t app;
    const char *config_key;
    const char *label;
    char default_character;
} rfscout_notification_descriptor_t;

esp_err_t alert_init( void );
const char *alert_mode_name( rfscout_alert_mode_t a_mode );
const rfscout_notification_descriptor_t *alert_notification_descriptor( rfscout_notification_type_t a_type );
const char *alert_notification_name( rfscout_notification_type_t a_type );
const char *alert_notification_config_key( rfscout_notification_type_t a_type );
rfscout_app_id_t alert_notification_app( rfscout_notification_type_t a_type );
char alert_notification_default_character( rfscout_notification_type_t a_type );
rfscout_alert_mode_t alert_mode( void );
void alert_set_mode( rfscout_alert_mode_t a_mode );
uint8_t alert_volume_percent( void );
bool alert_set_volume_percent( uint8_t a_percent );
char alert_morse_character( void );
bool alert_set_morse_character( char a_character );
bool alert_morse_character_supported( char a_character );
bool alert_request( void );
bool alert_request_character( char a_character );

/**
 * @brief Request one application-owned notification using global presentation policy.
 * @details Applies the owning app's notification gate and active theme before touching audio.
 */
bool alert_request_notification( rfscout_notification_type_t a_type );

#endif // _RFSCOUT_ALERT_H_
