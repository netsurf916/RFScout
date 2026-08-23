/**
    battery.h : RFScout battery telemetry interface
    Description: Exposes cached fuel-gauge and charger status to the user interface.
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
    bool gauge_available;
    bool charger_available;
    bool external_power;
    uint8_t percentage;
    uint16_t voltage_mv;
    int16_t current_ma;
    battery_charge_state_t charge_state;
} battery_status_t;

/**
 * @brief Initialize the isolated battery telemetry feature.
 * @return ESP_OK when the shared I2C bus is available; an ESP-IDF error when
 *         the bus itself cannot be initialized.
 * @details Missing battery devices are nonfatal. In that case the cached status
 *          remains unavailable and the UI renders a gray placeholder.
 */
esp_err_t battery_init( void );

/**
 * @brief Refresh cached battery telemetry when its polling interval elapses.
 * @details The function is inexpensive between polls and is intended to be
 *          called from the main application loop.
 */
void battery_poll( void );

/**
 * @brief Copy the most recently cached battery status.
 * @param a_status Receives the cached battery telemetry.
 * @return True when either the fuel gauge or charger has produced valid data.
 */
bool battery_get_status( battery_status_t *a_status );

#endif // _RFSCOUT_BATTERY_H_
