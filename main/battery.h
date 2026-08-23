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

/** @brief High-level charging state reported by the battery feature. */
typedef enum
{
    BATTERY_CHARGE_UNKNOWN = 0,
    BATTERY_CHARGE_DISCHARGING,
    BATTERY_CHARGE_CHARGING,
    BATTERY_CHARGE_COMPLETE
} battery_charge_state_t;

/** @brief Cached battery and external-power telemetry consumed by the UI. */
typedef struct
{
    bool available;                   /**< State-of-charge percentage is valid. */
    bool gauge_available;             /**< BQ27220 fuel-gauge data has been read. */
    bool charger_available;           /**< BQ25896 charger data has been read. */
    bool external_power;              /**< A non-OTG external power input is present. */
    uint8_t percentage;               /**< Remaining charge from 0 through 100 percent. */
    uint16_t voltage_mv;              /**< Battery voltage in millivolts. */
    int16_t current_ma;               /**< Signed battery current in milliamps. */
    battery_charge_state_t charge_state; /**< Current charging/discharging state. */
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
