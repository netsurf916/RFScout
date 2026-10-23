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

#define RFSCOUT_GPS_TIMEOUT_MIN_SECONDS 2U
#define RFSCOUT_GPS_TIMEOUT_MAX_SECONDS 30U
#define RFSCOUT_GPS_TIMEOUT_DEFAULT_SECONDS 5U
#define RFSCOUT_GPS_MOVEMENT_DEFAULT_M 10U
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
    RFSCOUT_HOLD_SHORTCUT_INPUT_LOCK = 0,
    RFSCOUT_HOLD_SHORTCUT_APP_LOCK,
    RFSCOUT_HOLD_SHORTCUT_COUNT
} rfscout_hold_shortcut_t;

typedef enum
{
    RFSCOUT_EVIDENCE_STRENGTH_EVERYTHING = 0,
    RFSCOUT_EVIDENCE_STRENGTH_POSSIBLE,
    RFSCOUT_EVIDENCE_STRENGTH_PROBABLE,
    RFSCOUT_EVIDENCE_STRENGTH_DISTINCTIVE,
    RFSCOUT_EVIDENCE_STRENGTH_COUNT
} rfscout_evidence_strength_t;

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
    rfscout_hold_shortcut_t hold_shortcut;
    bool apps_enabled[ RFSCOUT_APP_COUNT ];
    rfscout_app_id_t startup_app;
    bool logging_enabled;
    bool clock_24h;
    uint8_t gps_timeout_seconds;
    /** Receiver movement required between samples of every GPS-located SD log. */
    uint8_t gps_movement_m;
    bool gps_timepulse_enabled;
    bool gps_led_enabled;
    uint8_t gps_led_brightness;
    uint32_t gps_led_rgb;
    bool gps_keep_awake;
    rfscout_theme_id_t theme;
    rfscout_style_id_t style;
    rfscout_colors_id_t colors;
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
    rfscout_evidence_strength_t evidence_threshold;
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
rfscout_hold_shortcut_t device_config_hold_shortcut( void );
const char *device_config_hold_shortcut_name( rfscout_hold_shortcut_t a_shortcut );
esp_err_t device_config_set_hold_shortcut( rfscout_hold_shortcut_t a_shortcut );
esp_err_t device_config_set_gps_timeout_seconds( uint8_t a_seconds );
/** @brief Set the movement trigger; only the distances offered by the step helper are valid. */
esp_err_t device_config_set_gps_movement_m( uint8_t a_meters );
/** @brief Next supported movement-trigger distance in a_direction, wrapping. */
uint8_t device_config_gps_movement_step( uint8_t a_meters, int a_direction );
esp_err_t device_config_set_gps_timepulse( bool a_enabled );
esp_err_t device_config_set_gps_led( bool a_enabled, uint8_t a_brightness, uint32_t a_rgb );
esp_err_t device_config_set_gps_keep_awake( bool a_enabled );
esp_err_t device_config_set_clock_24h( bool a_enabled );
rfscout_theme_id_t device_config_theme( void );
esp_err_t device_config_set_theme( rfscout_theme_id_t a_theme );
rfscout_style_id_t device_config_style( void );
rfscout_colors_id_t device_config_colors( void );
esp_err_t device_config_set_style( rfscout_style_id_t a_style );
esp_err_t device_config_set_colors( rfscout_colors_id_t a_colors );
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
rfscout_evidence_strength_t device_config_evidence_threshold( void );
esp_err_t device_config_set_evidence_threshold( rfscout_evidence_strength_t a_threshold );
char device_config_notification_character( rfscout_notification_type_t a_type );
esp_err_t device_config_set_notification_character( rfscout_notification_type_t a_type,
                                                    char a_character );
esp_err_t device_config_set_timezone( const char *a_name );
const device_timezone_t *device_config_timezone( void );
esp_err_t device_config_save( void );
esp_err_t device_config_save_if_dirty( void );

#endif // _RFSCOUT_DEVICE_CONFIG_H_
