/**
    battery.h : RFScout battery telemetry interface
    Description: Exposes cached battery telemetry through the active board profile's provider.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#pragma once

#ifndef _RFSCOUT_BATTERY_H_
#define _RFSCOUT_BATTERY_H_

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum
{
    BATTERY_CHARGE_UNKNOWN = 0,
    BATTERY_CHARGE_DISCHARGING,
    BATTERY_CHARGE_CHARGING,
    BATTERY_CHARGE_COMPLETE
} battery_charge_state_t;

typedef struct
{
    bool available;
    bool percentage_fresh;
    bool electrical_fresh;
    bool gauge_available;
    bool charger_available;
    bool charger_fresh;
    bool external_power;
    uint8_t percentage;
    uint16_t voltage_mv;
    int16_t current_ma;
    battery_charge_state_t charge_state;
    /** Extended read-only gauge telemetry, valid only when gauge_details_fresh. */
    bool gauge_details_fresh;
    uint16_t remaining_capacity_mah;
    uint16_t full_capacity_mah;
    uint16_t design_capacity_mah;
    uint16_t cycle_count;
    uint16_t state_of_health_percent;
    uint16_t temperature_decikelvin;
    /** Decoded charger telemetry; valid only when charger_details_fresh. */
    bool charger_details_fresh;
    uint8_t charger_faults;
    uint8_t charger_phase;
    uint8_t charger_input_source;
    bool charger_power_good;
    uint16_t charger_input_limit_ma;
    uint16_t charger_fast_charge_limit_ma;
    uint16_t charger_regulation_mv;
    uint16_t charger_termination_ma;
    /** ADC values are meaningful only when charger_adc_fresh is true. */
    bool charger_adc_fresh;
    uint16_t charger_vbus_mv;
    uint16_t charger_adc_current_ma;
    uint16_t charger_system_mv;
    uint8_t charger_dpm_status;
    uint8_t charger_controller_state;
    bool charger_charge_enabled;
    bool charger_watchdog_enabled;
    bool charger_soc_disagreement;
} battery_status_t;

/** Default periodic battery telemetry interval. */
#define BATTERY_TELEMETRY_INTERVAL_US 5000000LL

/** Number of historical telemetry slots (about 5m50s at the default interval). */
#define BATTERY_HISTORY_CAPACITY 70U

/** Battery current is signed: positive means charging, negative means discharge. */
typedef struct
{
    int64_t timestamp_us;
    int16_t current_ma;
    uint16_t voltage_mv;
} battery_history_sample_t;

/**
 * @brief Copy up to a_capacity most recent valid electrical samples, oldest first.
 * @param a_samples Caller-owned storage, or NULL when querying available count.
 * @param a_capacity Number of elements in a_samples.
 * @return Number of samples copied, or available count when a_samples is NULL.
 */
size_t battery_history_read( battery_history_sample_t *a_samples, size_t a_capacity );

/** Read-only result of the one-shot BQ27220 startup profile inspection. */
typedef enum
{
    BATTERY_GAUGE_CONFIG_PENDING = 0,
    BATTERY_GAUGE_CONFIGURING,
    BATTERY_GAUGE_CONFIG_VERIFIED,
    BATTERY_GAUGE_CONFIG_FAILED
} battery_gauge_config_result_t;

typedef struct
{
    battery_gauge_config_result_t result;
    /** Last provisioning error, ESP_OK unless result is FAILED. */
    esp_err_t error;
    /** Stable description of current or failed startup step. */
    const char *stage;
    /** True when profile values have been read successfully during this startup. */
    bool profile_read;
    uint16_t profile_full_capacity_mah;
    uint16_t profile_design_capacity_mah;
} battery_gauge_configuration_t;

/** @brief Copy the one-shot gauge configuration status into caller-owned storage. */
bool battery_get_gauge_configuration( battery_gauge_configuration_t *a_status );

esp_err_t battery_init( void );
void battery_poll( void );
void battery_refresh( void );
bool battery_get_status( battery_status_t *a_status );

/** @brief Return whether this board design provides authoritative external-power telemetry. */
bool battery_external_power_telemetry_expected( void );

/**
 * @brief Request hardware battery-path shutdown when supported by the active provider.
 * @return ESP_ERR_NOT_SUPPORTED for voltage-only providers without a controllable battery path.
 */
esp_err_t battery_shutdown( void );

#endif // _RFSCOUT_BATTERY_H_
