/**
    device_config.h : RFScout persistent device configuration
    Description: Defines compiled defaults and firmware-managed system/application preferences.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DEVICE_CONFIG_H_
#define _RFSCOUT_DEVICE_CONFIG_H_

#include "alert.h"
#include "app_manager.h"
#include "audio_policy.h"
#include "esp_err.h"
#include "power.h"
#include "ui_theme.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_CONFIG_SCHEMA_VERSION 1U
#define RFSCOUT_TIMEZONE_MAX 8U
#define RFSCOUT_TIMEZONE_NAME_MAX 32U
#define RFSCOUT_TIMEZONE_LABEL_MAX 24U
#define RFSCOUT_TIMEZONE_POSIX_MAX 48U
#define RFSCOUT_OBSERVATION_TIMEOUT_MIN_SECONDS 10U
#define RFSCOUT_OBSERVATION_TIMEOUT_MAX_SECONDS 300U
#define RFSCOUT_OBSERVATION_TIMEOUT_STEP_SECONDS 10U
#define RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS 60U
#define RFSCOUT_LOW_BATTERY_THRESHOLD_MAX_PERCENT 20U
#define RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_MIN_C 70U
#define RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_MAX_C 100U
#define RFSCOUT_HIGH_TEMPERATURE_THRESHOLD_STEP_C 10U

typedef enum
{
    RFSCOUT_SURVEILLANCE_SENSITIVITY_HIGH = 0,
    RFSCOUT_SURVEILLANCE_SENSITIVITY_MEDIUM,
    RFSCOUT_SURVEILLANCE_SENSITIVITY_LOW,
    RFSCOUT_SURVEILLANCE_SENSITIVITY_COUNT
} rfscout_surveillance_sensitivity_t;

typedef struct
{
    char name[ RFSCOUT_TIMEZONE_NAME_MAX ];
    char label[ RFSCOUT_TIMEZONE_LABEL_MAX ];
    char posix[ RFSCOUT_TIMEZONE_POSIX_MAX ];
} device_timezone_t;

typedef struct
{
    uint8_t brightness_percent;
    uint16_t display_dim_timeout_seconds;
    uint16_t display_off_timeout_seconds;
    power_action_t power_button_short_action;
    power_action_t power_button_long_action;
    uint8_t low_battery_threshold_percent;
    power_action_t low_battery_action;
    uint8_t high_temperature_threshold_c;
    power_action_t high_temperature_action;
    bool apps_enabled[ RFSCOUT_APP_COUNT ];
    rfscout_app_id_t startup_app;
    bool logging_enabled;
    bool clock_24h;
    rfscout_theme_id_t theme;
    rfscout_audio_policy_id_t audio_policy;
    uint8_t audio_interaction_gain_percent;
    uint8_t audio_notification_gain_percent;
    rfscout_alert_mode_t alert_mode;
    uint8_t alert_volume_percent;
    uint8_t recorder_volume_percent;
    bool notification_app_enabled[ RFSCOUT_APP_COUNT ];
    bool wifi_device_feedback_enabled;
    bool ble_device_feedback_enabled;
    uint16_t wifi_timeout_seconds;
    uint16_t ble_timeout_seconds;
    rfscout_surveillance_sensitivity_t surveillance_sensitivity;
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
esp_err_t device_config_set_display_dim_timeout( uint16_t a_seconds );
esp_err_t device_config_set_display_off_timeout( uint16_t a_seconds );
esp_err_t device_config_set_power_button_short_action( power_action_t a_action );
esp_err_t device_config_set_power_button_long_action( power_action_t a_action );
esp_err_t device_config_set_low_battery_threshold( uint8_t a_percent );
esp_err_t device_config_set_low_battery_action( power_action_t a_action );
esp_err_t device_config_set_high_temperature_threshold( uint8_t a_temperature_c );
esp_err_t device_config_set_high_temperature_action( power_action_t a_action );
esp_err_t device_config_set_clock_24h( bool a_enabled );
rfscout_theme_id_t device_config_theme( void );
esp_err_t device_config_set_theme( rfscout_theme_id_t a_theme );
rfscout_audio_policy_id_t device_config_audio_policy( void );
esp_err_t device_config_set_audio_policy( rfscout_audio_policy_id_t a_policy );
esp_err_t device_config_set_audio_gains( uint8_t a_interaction_percent,
                                         uint8_t a_notification_percent );
esp_err_t device_config_set_alert_mode( rfscout_alert_mode_t a_mode );
esp_err_t device_config_set_alert_volume( uint8_t a_percent );
esp_err_t device_config_set_recorder_volume( uint8_t a_percent );
bool device_config_notification_app_enabled( rfscout_app_id_t a_app );
esp_err_t device_config_set_notification_app_enabled( rfscout_app_id_t a_app, bool a_enabled );
bool device_config_wifi_device_feedback_enabled( void );
esp_err_t device_config_set_wifi_device_feedback_enabled( bool a_enabled );
bool device_config_ble_device_feedback_enabled( void );
esp_err_t device_config_set_ble_device_feedback_enabled( bool a_enabled );
uint16_t device_config_wifi_timeout_seconds( void );
esp_err_t device_config_set_wifi_timeout_seconds( uint16_t a_seconds );
uint16_t device_config_ble_timeout_seconds( void );
esp_err_t device_config_set_ble_timeout_seconds( uint16_t a_seconds );
rfscout_surveillance_sensitivity_t device_config_surveillance_sensitivity( void );
esp_err_t
device_config_set_surveillance_sensitivity( rfscout_surveillance_sensitivity_t a_sensitivity );
char device_config_notification_character( rfscout_notification_type_t a_type );
esp_err_t device_config_set_notification_character( rfscout_notification_type_t a_type,
                                                    char a_character );
esp_err_t device_config_set_timezone( const char *a_name );
const device_timezone_t *device_config_timezone( void );
esp_err_t device_config_save( void );
esp_err_t device_config_save_if_dirty( void );

#endif // _RFSCOUT_DEVICE_CONFIG_H_
