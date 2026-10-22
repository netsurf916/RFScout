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
    /** Read-only charger registers, valid only when charger_details_fresh. */
    bool charger_details_fresh;
    uint8_t charger_faults;
    uint8_t charger_input_control;
    uint8_t charger_charge_control;
    uint8_t charger_current_control;
    uint8_t charger_precharge_control;
    uint8_t charger_voltage_control;
    uint8_t charger_timer_control;
} battery_status_t;

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
