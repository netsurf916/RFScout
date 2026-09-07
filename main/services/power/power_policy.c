/**
    power_policy.c : RFScout protective power-state policy
    Description: Monitors low battery and ESP32 internal temperature conditions with debounce and hysteresis.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "power_policy.h"

#include "battery.h"
#include "board.h"
#include "device_config.h"
#include "power.h"

#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdint.h>

#define POWER_POLICY_POLL_INTERVAL_US 1000000LL
#define POWER_POLICY_CONFIRM_SAMPLES 5U
#define POWER_POLICY_TRANSITION_DELAY_SECONDS 5U
#define POWER_POLICY_BATTERY_HYSTERESIS_PERCENT 3U
#define POWER_POLICY_TEMPERATURE_HYSTERESIS_C 5.0F

static const char *TAG = "power_policy";
static int64_t last_poll_us;
static uint8_t low_battery_samples;
static uint8_t high_temperature_samples;
static bool low_battery_latched;
static bool high_temperature_latched;
static temperature_sensor_handle_t temperature_sensor;
static bool temperature_available;

static bool transition_active( power_transition_reason_t a_reason )
{
    power_status_t status;
    power_get_status( &status );
    return ( status.reason == a_reason ) && ( POWER_HOLD_IDLE != status.state );
}

static void reset_low_battery( bool a_cancel_transition )
{
    low_battery_samples = 0U;
    low_battery_latched = false;
    if( a_cancel_transition )
    {
        power_cancel_transition( POWER_TRANSITION_LOW_BATTERY );
    }
}

static void reset_high_temperature( bool a_cancel_transition )
{
    high_temperature_samples = 0U;
    high_temperature_latched = false;
    if( a_cancel_transition )
    {
        power_cancel_transition( POWER_TRANSITION_HIGH_TEMPERATURE );
    }
}

static void poll_low_battery( void )
{
    const device_config_t *config = device_config_get();
    const uint8_t threshold = config->low_battery_threshold_percent;
    const power_action_t action = config->low_battery_action;
    if( ( 0U == threshold ) || ( POWER_ACTION_NONE == action ) )
    {
        reset_low_battery( true );
        return;
    }

    if( low_battery_latched )
    {
        battery_refresh();
    }

    battery_status_t battery;
    if( !battery_get_status( &battery ) )
    {
        if( !low_battery_latched )
        {
            low_battery_samples = 0U;
        }
        return;
    }

    /* Boards with authoritative charger telemetry retain the conservative T-Embed
       requirement: external power must be freshly known absent before a protective
       transition remains armed. Voltage-only boards such as T-Deck intentionally
       have no such signal, so they may act on fresh SOC without inventing one. */
    if( battery_external_power_telemetry_expected() )
    {
        if( !battery.charger_fresh )
        {
            reset_low_battery( true );
            return;
        }
        if( battery.external_power || ( BATTERY_CHARGE_CHARGING == battery.charge_state ) ||
            ( BATTERY_CHARGE_COMPLETE == battery.charge_state ) )
        {
            reset_low_battery( true );
            return;
        }
    }

    if( !battery.available || !battery.percentage_fresh )
    {
        if( !low_battery_latched )
        {
            low_battery_samples = 0U;
        }
        return;
    }

    const unsigned clear_threshold =
        (unsigned)threshold + (unsigned)POWER_POLICY_BATTERY_HYSTERESIS_PERCENT;
    if( (unsigned)battery.percentage >= clear_threshold )
    {
        reset_low_battery( true );
        return;
    }

    if( low_battery_latched )
    {
        if( transition_active( POWER_TRANSITION_LOW_BATTERY ) )
        {
            return;
        }
        low_battery_latched = false;
        low_battery_samples = 0U;
    }

    if( battery.percentage > threshold )
    {
        low_battery_samples = 0U;
        return;
    }

    if( low_battery_samples < POWER_POLICY_CONFIRM_SAMPLES )
    {
        ++low_battery_samples;
    }
    if( low_battery_samples < POWER_POLICY_CONFIRM_SAMPLES )
    {
        return;
    }

    if( power_request_transition( POWER_TRANSITION_LOW_BATTERY,
                                  action,
                                  "LOW BATTERY",
                                  POWER_POLICY_TRANSITION_DELAY_SECONDS ) )
    {
        low_battery_latched = true;
        ESP_LOGW( TAG,
                  "low battery protection requested: %u%% <= %u%% action=%s",
                  (unsigned)battery.percentage,
                  (unsigned)threshold,
                  power_action_name( action ) );
    }
}

static power_action_t thermal_effective_action( power_action_t a_configured_action )
{
    if( POWER_ACTION_POWER_OFF != a_configured_action ||
        !power_action_available( POWER_ACTION_SLEEP ) )
    {
        return a_configured_action;
    }

    battery_refresh();
    battery_status_t battery;
    if( !battery_get_status( &battery ) || !battery.charger_fresh || battery.external_power )
    {
        ESP_LOGW( TAG, "thermal power-off cannot confirm removable power; using sleep fallback" );
        return POWER_ACTION_SLEEP;
    }

    return a_configured_action;
}

static void poll_high_temperature( void )
{
    const device_config_t *config = device_config_get();
    const uint8_t threshold = config->high_temperature_threshold_c;
    const power_action_t action = config->high_temperature_action;
    if( ( 0U == threshold ) || ( POWER_ACTION_NONE == action ) || !temperature_available )
    {
        reset_high_temperature( true );
        return;
    }

    float temperature_c = 0.0F;
    const esp_err_t result = temperature_sensor_get_celsius( temperature_sensor, &temperature_c );
    if( ESP_OK != result )
    {
        if( !high_temperature_latched )
        {
            high_temperature_samples = 0U;
        }
        ESP_LOGW( TAG, "internal temperature read failed: %s", esp_err_to_name( result ) );
        return;
    }

    if( temperature_c <= (float)threshold - POWER_POLICY_TEMPERATURE_HYSTERESIS_C )
    {
        reset_high_temperature( true );
        return;
    }

    if( high_temperature_latched )
    {
        if( transition_active( POWER_TRANSITION_HIGH_TEMPERATURE ) )
        {
            return;
        }
        high_temperature_latched = false;
        high_temperature_samples = 0U;
    }

    if( temperature_c < (float)threshold )
    {
        high_temperature_samples = 0U;
        return;
    }

    if( high_temperature_samples < POWER_POLICY_CONFIRM_SAMPLES )
    {
        ++high_temperature_samples;
    }
    if( high_temperature_samples < POWER_POLICY_CONFIRM_SAMPLES )
    {
        return;
    }

    reset_low_battery( true );
    const power_action_t effective_action = thermal_effective_action( action );
    if( power_request_transition( POWER_TRANSITION_HIGH_TEMPERATURE,
                                  effective_action,
                                  "HIGH TEMPERATURE",
                                  POWER_POLICY_TRANSITION_DELAY_SECONDS ) )
    {
        high_temperature_latched = true;
        ESP_LOGW( TAG,
                  "high temperature protection requested: %.1f C >= %u C action=%s%s",
                  (double)temperature_c,
                  (unsigned)threshold,
                  power_action_name( effective_action ),
                  ( effective_action != action ) ? " (fallback)" : "" );
    }
}

esp_err_t power_policy_init( void )
{
    last_poll_us = -POWER_POLICY_POLL_INTERVAL_US;
    low_battery_samples = 0U;
    high_temperature_samples = 0U;
    low_battery_latched = false;
    high_temperature_latched = false;
    temperature_sensor = NULL;
    temperature_available = false;

#if RFSCOUT_BOARD_HAS_INTERNAL_TEMPERATURE
    const temperature_sensor_config_t sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT( 20, 100 );
    esp_err_t result = temperature_sensor_install( &sensor_config, &temperature_sensor );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "internal temperature sensor unavailable: %s", esp_err_to_name( result ) );
        return ESP_OK;
    }
    result = temperature_sensor_enable( temperature_sensor );
    if( ESP_OK != result )
    {
        ESP_LOGW(
            TAG, "could not enable internal temperature sensor: %s", esp_err_to_name( result ) );
        (void)temperature_sensor_uninstall( temperature_sensor );
        temperature_sensor = NULL;
        return ESP_OK;
    }
    temperature_available = true;
#endif

    return ESP_OK;
}

void power_policy_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( ( now_us - last_poll_us ) < POWER_POLICY_POLL_INTERVAL_US )
    {
        return;
    }
    last_poll_us = now_us;

    poll_high_temperature();
    poll_low_battery();
}
