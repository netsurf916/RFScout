/**
    notification.h : RFScout semantic notification catalog
    Description: Defines application-owned notification categories independently from audio presentation policy.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_NOTIFICATION_H_
#define _RFSCOUT_NOTIFICATION_H_

#include "app_manager.h"

#include <stdbool.h>

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

const rfscout_notification_descriptor_t *
notification_descriptor( rfscout_notification_type_t a_type );
const char *notification_name( rfscout_notification_type_t a_type );
const char *notification_config_key( rfscout_notification_type_t a_type );
rfscout_app_id_t notification_app( rfscout_notification_type_t a_type );
char notification_default_character( rfscout_notification_type_t a_type );

/**
 * @brief Emit one semantic notification event.
 * @return true when the notification category is valid and was emitted.
 */
bool notification_emit( rfscout_notification_type_t a_type );

#endif // _RFSCOUT_NOTIFICATION_H_
