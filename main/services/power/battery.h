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
