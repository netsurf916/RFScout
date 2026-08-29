/**
    device_config.h : RFScout persistent device configuration
    Description: Defines compiled defaults, SD-backed JSON preferences, application enablement, and notifications.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DEVICE_CONFIG_H_
#define _RFSCOUT_DEVICE_CONFIG_H_

#include "alert.h"
#include "app_manager.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_CONFIG_PATH "/sdcard/rfscout.json"
#define RFSCOUT_CONFIG_SCHEMA_VERSION 1U
#define RFSCOUT_TIMEZONE_MAX 8U
#define RFSCOUT_TIMEZONE_NAME_MAX 32U
#define RFSCOUT_TIMEZONE_LABEL_MAX 24U
#define RFSCOUT_TIMEZONE_POSIX_MAX 48U

typedef struct
{
    char name[ RFSCOUT_TIMEZONE_NAME_MAX ];
    char label[ RFSCOUT_TIMEZONE_LABEL_MAX ];
    char posix[ RFSCOUT_TIMEZONE_POSIX_MAX ];
} device_timezone_t;

typedef struct
{
    uint8_t brightness_percent;
    bool apps_enabled[ RFSCOUT_APP_COUNT ];
    rfscout_app_id_t startup_app;
    bool logging_enabled;
    bool clock_24h;
    rfscout_alert_mode_t alert_mode;
    uint8_t alert_volume_percent;
    uint8_t recorder_volume_percent;
    uint8_t recorder_input_gain_db;
    char notification_characters[ RFSCOUT_NOTIFICATION_TYPE_COUNT ];
    char timezone_selected[ RFSCOUT_TIMEZONE_NAME_MAX ];
    device_timezone_t timezones[ RFSCOUT_TIMEZONE_MAX ];
    size_t timezone_count;
} device_config_t;

esp_err_t device_config_init( void );
const device_config_t *device_config_get( void );
bool device_config_app_enabled( rfscout_app_id_t a_app );
esp_err_t device_config_set_app_enabled( rfscout_app_id_t a_app, bool a_enabled );
esp_err_t device_config_set_startup_app( rfscout_app_id_t a_app );
rfscout_app_id_t device_config_startup_app( void );
esp_err_t device_config_set_logging_enabled( bool a_enabled );
esp_err_t device_config_set_brightness( uint8_t a_percent );
esp_err_t device_config_set_clock_24h( bool a_enabled );
esp_err_t device_config_set_alert_mode( rfscout_alert_mode_t a_mode );
esp_err_t device_config_set_alert_volume( uint8_t a_percent );
esp_err_t device_config_set_recorder_volume( uint8_t a_percent );
esp_err_t device_config_set_recorder_input_gain( uint8_t a_gain_db );
char device_config_notification_character( rfscout_notification_type_t a_type );
esp_err_t device_config_set_notification_character( rfscout_notification_type_t a_type,
                                                    char a_character );
esp_err_t device_config_set_timezone( const char *a_name );
const device_timezone_t *device_config_timezone( void );
bool device_config_saved_time( int64_t *a_epoch, const char **a_source );
esp_err_t device_config_save( bool a_update_time, int64_t a_epoch, const char *a_time_source );

#endif // _RFSCOUT_DEVICE_CONFIG_H_
