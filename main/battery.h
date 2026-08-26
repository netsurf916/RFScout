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
    bool available;                      /**< State-of-charge percentage is valid. */
    bool gauge_available;                /**< BQ27220 fuel-gauge data has been read. */
    bool charger_available;              /**< BQ25896 charger data has been read. */
    bool external_power;                 /**< A non-OTG external power input is present. */
    uint8_t percentage;                  /**< Remaining charge from 0 through 100 percent. */
    uint16_t voltage_mv;                 /**< Battery voltage in millivolts. */
    int16_t current_ma;                  /**< Signed battery current in milliamps. */
    battery_charge_state_t charge_state; /**< Current charging/discharging state. */
} battery_status_t;

/** @brief Initialize the isolated battery telemetry feature. */
esp_err_t battery_init( void );

/** @brief Refresh cached battery telemetry when its polling interval elapses. */
void battery_poll( void );

/** @brief Copy the most recently cached battery status. */
bool battery_get_status( battery_status_t *a_status );

/**
 * @brief Disable the BQ25896 battery power path for a true power-off.
 * @return ESP_OK when the BATFET disable bit is written; ESP_ERR_NOT_FOUND when
 *         the charger is unavailable; otherwise the I2C transaction error.
 * @note With external power connected the charger cannot fully remove system
 *       power, matching the BQ25896 hardware behavior.
 */
esp_err_t battery_shutdown( void );

#endif // _RFSCOUT_BATTERY_H_
