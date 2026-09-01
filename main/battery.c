/**
    battery.c : RFScout battery telemetry implementation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "battery.h"

#include "board.h"
#include "board_i2c.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

#define BATTERY_POLL_INTERVAL_US 1000000LL
#define BATTERY_I2C_TIMEOUT_MS 50

#define BQ27220_REG_VOLTAGE 0x08U
#define BQ27220_REG_STATUS 0x0AU
#define BQ27220_REG_CURRENT 0x0CU
#define BQ27220_REG_STATE_OF_CHARGE 0x2CU
#define BQ27220_STATUS_DISCHARGING 0x0001U
#define BQ27220_STATUS_FULL 0x0200U

#define BQ25896_REG_POWER_PATH 0x09U
#define BQ25896_REG_STATUS 0x0BU
#define BQ25896_BATFET_DISABLE_BIT 0x20U
#define BQ25896_BUS_STATUS_SHIFT 5U
#define BQ25896_BUS_STATUS_MASK 0x07U
#define BQ25896_CHARGE_STATUS_SHIFT 3U
#define BQ25896_CHARGE_STATUS_MASK 0x03U
#define BQ25896_POWER_GOOD_BIT 0x04U
#define BQ25896_BUS_NO_INPUT 0U
#define BQ25896_BUS_OTG 7U
#define BQ25896_CHARGE_NONE 0U
#define BQ25896_CHARGE_PRECHARGE 1U
#define BQ25896_CHARGE_FAST 2U
#define BQ25896_CHARGE_DONE 3U
#define BATTERY_CURRENT_ACTIVE_MA 30

static const char *TAG = "battery";
static i2c_master_dev_handle_t gauge_device;
static i2c_master_dev_handle_t charger_device;
static battery_status_t cached_status;
static int64_t last_poll_us;
static bool initialized;

static esp_err_t
read_register_u8( i2c_master_dev_handle_t a_device, uint8_t a_register, uint8_t *a_value )
{
    if( ( NULL == a_device ) || ( NULL == a_value ) )
        return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(
        a_device, &a_register, 1, a_value, 1, BATTERY_I2C_TIMEOUT_MS );
}

static esp_err_t
write_register_u8( i2c_master_dev_handle_t a_device, uint8_t a_register, uint8_t a_value )
{
    if( NULL == a_device )
        return ESP_ERR_INVALID_ARG;
    const uint8_t data[ 2 ] = { a_register, a_value };
    return i2c_master_transmit( a_device, data, sizeof( data ), BATTERY_I2C_TIMEOUT_MS );
}

static esp_err_t
read_register_u16( i2c_master_dev_handle_t a_device, uint8_t a_register, uint16_t *a_value )
{
    if( ( NULL == a_device ) || ( NULL == a_value ) )
        return ESP_ERR_INVALID_ARG;
    uint8_t data[ 2 ];
    const esp_err_t result = i2c_master_transmit_receive(
        a_device, &a_register, 1, data, sizeof( data ), BATTERY_I2C_TIMEOUT_MS );
    if( ESP_OK == result )
    {
        *a_value = (uint16_t)data[ 0 ] | ( (uint16_t)data[ 1 ] << 8U );
    }
    return result;
}

static esp_err_t
attach_optional_device( uint16_t a_address, const char *a_name, i2c_master_dev_handle_t *a_device )
{
    const esp_err_t probe_result = board_i2c_probe( a_address );
    if( ESP_OK != probe_result )
    {
        ESP_LOGW( TAG, "%s not detected at 0x%02X", a_name, (unsigned)a_address );
        return probe_result;
    }
    const esp_err_t result = board_i2c_add_device( a_address, a_device );
    if( ESP_OK == result )
        ESP_LOGI( TAG, "%s detected at 0x%02X", a_name, (unsigned)a_address );
    else
        ESP_LOGW( TAG, "unable to attach %s: %s", a_name, esp_err_to_name( result ) );
    return result;
}

static battery_charge_state_t gauge_charge_state( uint16_t a_status, int16_t a_current_ma )
{
    if( 0U != ( a_status & BQ27220_STATUS_FULL ) )
        return BATTERY_CHARGE_COMPLETE;
    if( 0U != ( a_status & BQ27220_STATUS_DISCHARGING ) )
        return BATTERY_CHARGE_DISCHARGING;
    if( a_current_ma > BATTERY_CURRENT_ACTIVE_MA )
        return BATTERY_CHARGE_CHARGING;
    return BATTERY_CHARGE_UNKNOWN;
}

static battery_charge_state_t charger_charge_state( uint8_t a_status, bool a_external_power )
{
    const uint8_t charge_status =
        ( a_status >> BQ25896_CHARGE_STATUS_SHIFT ) & BQ25896_CHARGE_STATUS_MASK;
    switch( charge_status )
    {
        case BQ25896_CHARGE_PRECHARGE:
        case BQ25896_CHARGE_FAST:
            return BATTERY_CHARGE_CHARGING;
        case BQ25896_CHARGE_DONE:
            return BATTERY_CHARGE_COMPLETE;
        case BQ25896_CHARGE_NONE:
        default:
            return a_external_power ? BATTERY_CHARGE_UNKNOWN : BATTERY_CHARGE_DISCHARGING;
    }
}

void battery_refresh( void )
{
    if( !initialized )
        return;

    last_poll_us = esp_timer_get_time();
    battery_status_t next = cached_status;
    bool gauge_sampled = false;
    uint16_t gauge_status = 0;
    if( NULL != gauge_device )
    {
        uint16_t percentage = 0;
        if( ( ESP_OK ==
              read_register_u16( gauge_device, BQ27220_REG_STATE_OF_CHARGE, &percentage ) ) &&
            ( percentage <= 100U ) )
        {
            next.percentage = (uint8_t)percentage;
            next.available = true;
            next.gauge_available = true;
            gauge_sampled = true;
        }
        uint16_t voltage_mv = 0;
        if( ESP_OK == read_register_u16( gauge_device, BQ27220_REG_VOLTAGE, &voltage_mv ) )
            next.voltage_mv = voltage_mv;
        uint16_t current_raw = 0;
        if( ESP_OK == read_register_u16( gauge_device, BQ27220_REG_CURRENT, &current_raw ) )
            next.current_ma = (int16_t)current_raw;
        (void)read_register_u16( gauge_device, BQ27220_REG_STATUS, &gauge_status );
    }

    bool charger_sampled = false;
    if( NULL != charger_device )
    {
        uint8_t charger_status = 0;
        if( ESP_OK == read_register_u8( charger_device, BQ25896_REG_STATUS, &charger_status ) )
        {
            const uint8_t bus_status =
                ( charger_status >> BQ25896_BUS_STATUS_SHIFT ) & BQ25896_BUS_STATUS_MASK;
            const bool valid_input =
                ( BQ25896_BUS_NO_INPUT != bus_status ) && ( BQ25896_BUS_OTG != bus_status );
            next.external_power =
                valid_input || ( 0U != ( charger_status & BQ25896_POWER_GOOD_BIT ) );
            next.charge_state = charger_charge_state( charger_status, next.external_power );
            next.charger_available = true;
            charger_sampled = true;
        }
    }

    if( !charger_sampled && gauge_sampled )
    {
        next.charge_state = gauge_charge_state( gauge_status, next.current_ma );
        next.external_power = ( BATTERY_CHARGE_CHARGING == next.charge_state ) ||
                              ( BATTERY_CHARGE_COMPLETE == next.charge_state );
    }
    cached_status = next;
}

esp_err_t battery_init( void )
{
    if( initialized )
        return ESP_OK;
    memset( &cached_status, 0, sizeof( cached_status ) );
    cached_status.charge_state = BATTERY_CHARGE_UNKNOWN;
    const esp_err_t result = board_i2c_init();
    if( ESP_OK != result )
        return result;
    (void)attach_optional_device(
        RFSCOUT_BOARD_BQ27220_I2C_ADDRESS, "BQ27220 fuel gauge", &gauge_device );
    (void)attach_optional_device(
        RFSCOUT_BOARD_BQ25896_I2C_ADDRESS, "BQ25896 charger", &charger_device );
    initialized = true;
    last_poll_us = -BATTERY_POLL_INTERVAL_US;
    battery_refresh();
    return ESP_OK;
}

void battery_poll( void )
{
    if( !initialized )
        return;
    const int64_t now_us = esp_timer_get_time();
    if( ( now_us - last_poll_us ) < BATTERY_POLL_INTERVAL_US )
        return;
    battery_refresh();
}

bool battery_get_status( battery_status_t *a_status )
{
    if( NULL == a_status )
        return false;
    *a_status = cached_status;
    return cached_status.available || cached_status.charger_available;
}

esp_err_t battery_shutdown( void )
{
    if( NULL == charger_device )
        return ESP_ERR_NOT_FOUND;

    uint8_t power_path = 0;
    esp_err_t result = read_register_u8( charger_device, BQ25896_REG_POWER_PATH, &power_path );
    if( ESP_OK != result )
        return result;

    power_path |= BQ25896_BATFET_DISABLE_BIT;
    return write_register_u8( charger_device, BQ25896_REG_POWER_PATH, power_path );
}
