/**
    device_config.h : RFScout persistent device configuration
    Description: Defines compiled defaults, SD-backed JSON preferences, and application enablement.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_DEVICE_CONFIG_H_
#define _RFSCOUT_DEVICE_CONFIG_H_

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
    bool logging_enabled;
    bool clock_24h;
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

/** @brief Change whether an application is enabled; Device itself always remains enabled. */
esp_err_t device_config_set_app_enabled( rfscout_app_id_t a_app, bool a_enabled );

/** @brief Change logging preference. */
esp_err_t device_config_set_logging_enabled( bool a_enabled );

/** @brief Change display brightness percentage. */
esp_err_t device_config_set_brightness( uint8_t a_percent );

/** @brief Change 12/24-hour clock presentation. */
esp_err_t device_config_set_clock_24h( bool a_enabled );

/** @brief Select a timezone definition by configured name. */
esp_err_t device_config_set_timezone( const char *a_name );

/** @brief Return the selected timezone definition or NULL when unavailable. */
const device_timezone_t *device_config_timezone( void );

/** @brief Return saved cold-boot wall-clock state loaded from configuration. */
bool device_config_saved_time( int64_t *a_epoch, const char **a_source );

/** @brief Atomically persist current preferences and optional wall-clock bootstrap state to SD. */
esp_err_t device_config_save( bool a_include_time, int64_t a_epoch, const char *a_time_source );

#endif // _RFSCOUT_DEVICE_CONFIG_H_
