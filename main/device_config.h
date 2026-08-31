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
#include "ui_theme.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RFSCOUT_CONFIG_PATH "/sdcard/rfscout.json"
#define RFSCOUT_CONFIG_SCHEMA_VERSION 2U
#define RFSCOUT_TIMEZONE_MAX 8U
#define RFSCOUT_TIMEZONE_NAME_MAX 32U
#define RFSCOUT_TIMEZONE_LABEL_MAX 24U
#define RFSCOUT_TIMEZONE_POSIX_MAX 48U
#define RFSCOUT_OBSERVATION_TIMEOUT_MIN_SECONDS 10U
#define RFSCOUT_OBSERVATION_TIMEOUT_MAX_SECONDS 300U
#define RFSCOUT_OBSERVATION_TIMEOUT_STEP_SECONDS 10U
#define RFSCOUT_OBSERVATION_TIMEOUT_DEFAULT_SECONDS 60U
#define RFSCOUT_DISPLAY_DIM_TIMEOUT_DEFAULT_SECONDS 120U
#define RFSCOUT_DISPLAY_OFF_TIMEOUT_DEFAULT_SECONDS 600U

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
    bool apps_enabled[ RFSCOUT_APP_COUNT ];
    rfscout_app_id_t startup_app;
    bool logging_enabled;
    bool clock_24h;
    rfscout_theme_id_t theme;
    rfscout_alert_mode_t alert_mode;
    uint8_t alert_volume_percent;
    uint8_t recorder_volume_percent;
    uint8_t recorder_input_gain_db;
    bool notification_app_enabled[ RFSCOUT_APP_COUNT ];
    bool wifi_device_feedback_enabled;
    bool ble_device_feedback_enabled;
    uint16_t wifi_timeout_seconds;
    uint16_t ble_timeout_seconds;
    uint16_t surveillance_timeout_seconds;
    char notification_characters[ RFSCOUT_NOTIFICATION_TYPE_COUNT ];
    char timezone_selected[ RFSCOUT_TIMEZONE_NAME_MAX ];
    device_timezone_t timezones[ RFSCOUT_TIMEZONE_MAX ];
    size_t timezone_count;
} device_config_t;

/** @brief Initialize compiled defaults and optionally load SD-backed configuration. */
esp_err_t device_config_init( void );

/** @brief Return the active device configuration. */
const device_config_t *device_config_get( void );

/** @brief Return whether one application is enabled by device configuration. */
bool device_config_app_enabled( rfscout_app_id_t a_app );

/** @brief Change whether one application is enabled; Device/Home itself always remains enabled. */
esp_err_t device_config_set_app_enabled( rfscout_app_id_t a_app, bool a_enabled );

/** @brief Change the preferred startup application. */
esp_err_t device_config_set_startup_app( rfscout_app_id_t a_app );

/** @brief Return the configured startup application. */
rfscout_app_id_t device_config_startup_app( void );

/** @brief Change logging preference. */
esp_err_t device_config_set_logging_enabled( bool a_enabled );

/** @brief Change display brightness percentage. */
esp_err_t device_config_set_brightness( uint8_t a_percent );

/** @brief Return the global inactivity interval before display dimming, or zero when disabled. */
uint16_t device_config_display_dim_timeout_seconds( void );
/** @brief Change the global display-dim timeout to one of the supported preset values. */
esp_err_t device_config_set_display_dim_timeout_seconds( uint16_t a_seconds );
/** @brief Return the global inactivity interval before display blanking, or zero when disabled. */
uint16_t device_config_display_off_timeout_seconds( void );
/** @brief Change the global display-off timeout to one of the supported preset values. */
esp_err_t device_config_set_display_off_timeout_seconds( uint16_t a_seconds );

/** @brief Change 12/24-hour clock presentation. */
esp_err_t device_config_set_clock_24h( bool a_enabled );
rfscout_theme_id_t device_config_theme( void );
esp_err_t device_config_set_theme( rfscout_theme_id_t a_theme );

/** @brief Change the global audible notification mode. */
esp_err_t device_config_set_alert_mode( rfscout_alert_mode_t a_mode );

/** @brief Change the global notification volume percentage from 0 through 100. */
esp_err_t device_config_set_alert_volume( uint8_t a_percent );

/** @brief Change Recorder playback volume percentage from 0 through 100. */
esp_err_t device_config_set_recorder_volume( uint8_t a_percent );

/** @brief Change Recorder fixed digital input gain (0..24 dB, 3 dB steps). */
esp_err_t device_config_set_recorder_input_gain( uint8_t a_gain_db );
bool device_config_notification_app_enabled( rfscout_app_id_t a_app );
esp_err_t device_config_set_notification_app_enabled( rfscout_app_id_t a_app, bool a_enabled );

/** @brief Return whether ordinary Wi-Fi discoveries may emit the general detection event. */
bool device_config_wifi_device_feedback_enabled( void );
/** @brief Change whether ordinary Wi-Fi discoveries may emit the general detection event. */
esp_err_t device_config_set_wifi_device_feedback_enabled( bool a_enabled );
/** @brief Return whether ordinary BLE discoveries may emit the general detection event. */
bool device_config_ble_device_feedback_enabled( void );
/** @brief Change whether ordinary BLE discoveries may emit the general detection event. */
esp_err_t device_config_set_ble_device_feedback_enabled( bool a_enabled );

/** @brief Return the Wi-Fi live-list timeout in seconds. */
uint16_t device_config_wifi_timeout_seconds( void );
/** @brief Change the Wi-Fi live-list timeout in 10-second steps from 10 through 300 seconds. */
esp_err_t device_config_set_wifi_timeout_seconds( uint16_t a_seconds );
/** @brief Return the Bluetooth live-list timeout in seconds. */
uint16_t device_config_ble_timeout_seconds( void );
/** @brief Change the Bluetooth live-list timeout in 10-second steps from 10 through 300 seconds. */
esp_err_t device_config_set_ble_timeout_seconds( uint16_t a_seconds );
/** @brief Return the retained surveillance-list timeout in seconds. */
uint16_t device_config_surveillance_timeout_seconds( void );
/** @brief Change the surveillance-list timeout in 10-second steps from 10 through 300 seconds. */
esp_err_t device_config_set_surveillance_timeout_seconds( uint16_t a_seconds );

/** @brief Return the configured Morse character for one notification category. */
char device_config_notification_character( rfscout_notification_type_t a_type );

/** @brief Change the Morse character for one notification category. */
esp_err_t device_config_set_notification_character( rfscout_notification_type_t a_type,
                                                    char a_character );

/** @brief Select a timezone definition by configured name. */
esp_err_t device_config_set_timezone( const char *a_name );

/** @brief Return the selected timezone definition or NULL when unavailable. */
const device_timezone_t *device_config_timezone( void );

/** @brief Return saved cold-boot wall-clock state loaded from configuration. */
bool device_config_saved_time( int64_t *a_epoch, const char **a_source );

/**
 * @brief Atomically persist current preferences and optional wall-clock bootstrap state to SD.
 * @param a_update_time True to replace/clear the saved shutdown epoch; false to preserve it.
 * @param a_epoch New saved epoch when @p a_update_time is true.
 * @param a_time_source Stable source token for the supplied epoch.
 */
esp_err_t device_config_save( bool a_update_time, int64_t a_epoch, const char *a_time_source );
/** @brief Persist runtime preferences only when a setter has changed canonical configuration. */
esp_err_t device_config_save_if_dirty( void );

#endif // _RFSCOUT_DEVICE_CONFIG_H_
