/**
    battery.c : RFScout battery telemetry implementation
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "battery.h"

#include "board.h"
#include "board_i2c.h"

#include "driver/i2c_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stddef.h>
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
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_3
#define BATTERY_ADC_DIVIDER_NUMERATOR 18U
#define BATTERY_ADC_DIVIDER_DENOMINATOR 10U

static const char *TAG = "battery";
static i2c_master_dev_handle_t gauge_device;
static i2c_master_dev_handle_t charger_device;
static battery_status_t cached_status;
static int64_t last_poll_us;
static bool initialized;
static adc_oneshot_unit_handle_t analog_unit;
static adc_cali_handle_t analog_calibration;

static esp_err_t
read_register_u8( i2c_master_dev_handle_t a_device, uint8_t a_register, uint8_t *a_value )
{
    if( ( NULL == a_device ) || ( NULL == a_value ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(
        a_device, &a_register, 1, a_value, 1, BATTERY_I2C_TIMEOUT_MS );
}

static esp_err_t
write_register_u8( i2c_master_dev_handle_t a_device, uint8_t a_register, uint8_t a_value )
{
    if( NULL == a_device )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t data[ 2 ] = { a_register, a_value };
    return i2c_master_transmit( a_device, data, sizeof( data ), BATTERY_I2C_TIMEOUT_MS );
}

static esp_err_t
read_register_u16( i2c_master_dev_handle_t a_device, uint8_t a_register, uint16_t *a_value )
{
    if( ( NULL == a_device ) || ( NULL == a_value ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t data[ 2 ];
    const esp_err_t result = i2c_master_transmit_receive(
        a_device, &a_register, 1, data, sizeof( data ), BATTERY_I2C_TIMEOUT_MS );
    if( ESP_OK == result )
    {
        *a_value = (uint16_t)data[ 0 ] | ( (uint16_t)data[ 1 ] << 8U );
    }
    return result;
}

static void detach_optional_device( i2c_master_dev_handle_t *a_device )
{
    if( ( NULL == a_device ) || ( NULL == *a_device ) )
    {
        return;
    }

    (void)i2c_master_bus_rm_device( *a_device );
    *a_device = NULL;
}

static esp_err_t
attach_optional_device( uint16_t a_address, const char *a_name, i2c_master_dev_handle_t *a_device )
{
    if( 0U == a_address )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const esp_err_t probe_result = board_i2c_probe( a_address );
    if( ESP_OK != probe_result )
    {
        ESP_LOGD( TAG, "%s not detected at 0x%02X", a_name, (unsigned)a_address );
        return probe_result;
    }
    const esp_err_t result = board_i2c_add_device( a_address, a_device );
    if( ESP_OK == result )
    {
        ESP_LOGI( TAG, "%s detected at 0x%02X", a_name, (unsigned)a_address );
    }
    else
    {
        ESP_LOGW( TAG, "unable to attach %s: %s", a_name, esp_err_to_name( result ) );
    }
    return result;
}

static battery_charge_state_t gauge_charge_state( uint16_t a_status, int16_t a_current_ma )
{
    if( 0U != ( a_status & BQ27220_STATUS_FULL ) )
    {
        return BATTERY_CHARGE_COMPLETE;
    }
    if( 0U != ( a_status & BQ27220_STATUS_DISCHARGING ) )
    {
        return BATTERY_CHARGE_DISCHARGING;
    }
    if( a_current_ma > BATTERY_CURRENT_ACTIVE_MA )
    {
        return BATTERY_CHARGE_CHARGING;
    }
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

static void refresh_i2c_battery( battery_status_t *a_status )
{
    if( NULL == gauge_device )
    {
        (void)attach_optional_device(
            RFSCOUT_BOARD_BQ27220_I2C_ADDRESS, "BQ27220 fuel gauge", &gauge_device );
    }
    if( NULL == charger_device )
    {
        (void)attach_optional_device(
            RFSCOUT_BOARD_BQ25896_I2C_ADDRESS, "BQ25896 charger", &charger_device );
    }

    bool gauge_sampled = false;
    uint16_t gauge_status = 0;
    if( NULL != gauge_device )
    {
        uint16_t percentage = 0;
        const esp_err_t gauge_result =
            read_register_u16( gauge_device, BQ27220_REG_STATE_OF_CHARGE, &percentage );
        if( ( ESP_OK == gauge_result ) && ( percentage <= 100U ) )
        {
            a_status->percentage = (uint8_t)percentage;
            a_status->available = true;
            a_status->percentage_fresh = true;
            a_status->gauge_available = true;
            gauge_sampled = true;

            uint16_t voltage_mv = 0;
            if( ESP_OK == read_register_u16( gauge_device, BQ27220_REG_VOLTAGE, &voltage_mv ) )
            {
                a_status->voltage_mv = voltage_mv;
            }
            uint16_t current_raw = 0;
            if( ESP_OK == read_register_u16( gauge_device, BQ27220_REG_CURRENT, &current_raw ) )
            {
                a_status->current_ma = (int16_t)current_raw;
            }
            (void)read_register_u16( gauge_device, BQ27220_REG_STATUS, &gauge_status );
        }
        else if( ESP_OK != gauge_result )
        {
            ESP_LOGW( TAG, "BQ27220 no longer responding: %s", esp_err_to_name( gauge_result ) );
            detach_optional_device( &gauge_device );
        }
    }

    bool charger_sampled = false;
    if( NULL != charger_device )
    {
        uint8_t charger_status = 0;
        const esp_err_t charger_result =
            read_register_u8( charger_device, BQ25896_REG_STATUS, &charger_status );
        if( ESP_OK == charger_result )
        {
            const uint8_t bus_status =
                ( charger_status >> BQ25896_BUS_STATUS_SHIFT ) & BQ25896_BUS_STATUS_MASK;
            const bool valid_input =
                ( BQ25896_BUS_NO_INPUT != bus_status ) && ( BQ25896_BUS_OTG != bus_status );
            a_status->external_power =
                valid_input || ( 0U != ( charger_status & BQ25896_POWER_GOOD_BIT ) );
            a_status->charge_state =
                charger_charge_state( charger_status, a_status->external_power );
            a_status->charger_available = true;
            a_status->charger_fresh = true;
            charger_sampled = true;
        }
        else
        {
            ESP_LOGW( TAG, "BQ25896 no longer responding: %s", esp_err_to_name( charger_result ) );
            detach_optional_device( &charger_device );
        }
    }

    if( !charger_sampled && gauge_sampled )
    {
        a_status->charge_state = gauge_charge_state( gauge_status, a_status->current_ma );
        a_status->external_power = ( BATTERY_CHARGE_CHARGING == a_status->charge_state ) ||
                                   ( BATTERY_CHARGE_COMPLETE == a_status->charge_state );
    }
}

/** @brief Configure only the documented SI4732 GPIO4 analog battery input. */
static esp_err_t init_analog_battery( void )
{
    if( RFSCOUT_BOARD_TEMBED_SI4732 != board_profile()->id )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const adc_oneshot_unit_init_cfg_t unit_config = { .unit_id = ADC_UNIT_1 };
    esp_err_t result = adc_oneshot_new_unit( &unit_config, &analog_unit );
    if( ESP_OK != result )
    {
        return result;
    }
    const adc_oneshot_chan_cfg_t channel_config = { .atten = ADC_ATTEN_DB_12,
                                                    .bitwidth = ADC_BITWIDTH_DEFAULT };
    result = adc_oneshot_config_channel( analog_unit, BATTERY_ADC_CHANNEL, &channel_config );
    if( ESP_OK != result )
    {
        (void)adc_oneshot_del_unit( analog_unit );
        analog_unit = NULL;
        return result;
    }
    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = ADC_UNIT_1,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    result = adc_cali_create_scheme_curve_fitting( &calibration_config, &analog_calibration );
    if( ESP_OK != result )
    {
        (void)adc_oneshot_del_unit( analog_unit );
        analog_unit = NULL;
    }
    return result;
}

/** @brief Voltage-only fallback; this never claims percentage or charging state. */
static void refresh_analog_battery( battery_status_t *a_status )
{
    if( NULL == analog_unit || NULL == analog_calibration )
    {
        return;
    }
    int raw = 0;
    int millivolts = 0;
    if( ESP_OK != adc_oneshot_read( analog_unit, BATTERY_ADC_CHANNEL, &raw ) ||
        ESP_OK != adc_cali_raw_to_voltage( analog_calibration, raw, &millivolts ) ||
        millivolts <= 0 )
    {
        return;
    }
    a_status->voltage_mv = (uint16_t)( ( (uint32_t)millivolts *
                                        BATTERY_ADC_DIVIDER_NUMERATOR ) /
                                      BATTERY_ADC_DIVIDER_DENOMINATOR );
    a_status->available = true;
}

void battery_refresh( void )
{
    if( !initialized )
    {
        return;
    }

    last_poll_us = esp_timer_get_time();
    battery_status_t next = cached_status;
    next.available = false;
    next.percentage_fresh = false;
    next.gauge_available = false;
    next.charger_available = false;
    next.charger_fresh = false;
    next.charge_state = BATTERY_CHARGE_UNKNOWN;
    next.external_power = false;
    next.current_ma = 0;
    next.voltage_mv = 0;

    if( RFSCOUT_BATTERY_I2C_GAUGE_CHARGER == RFSCOUT_BOARD_BATTERY_TYPE )
    {
        refresh_i2c_battery( &next );
    }
    if( !next.gauge_available )
    {
        refresh_analog_battery( &next );
    }
    cached_status = next;
}

esp_err_t battery_init( void )
{
    if( initialized )
    {
        return ESP_OK;
    }
    memset( &cached_status, 0, sizeof( cached_status ) );
    cached_status.charge_state = BATTERY_CHARGE_UNKNOWN;
    gauge_device = NULL;
    charger_device = NULL;

    esp_err_t result = ESP_OK;
    if( RFSCOUT_BATTERY_I2C_GAUGE_CHARGER == RFSCOUT_BOARD_BATTERY_TYPE )
    {
        result = board_i2c_init();
        if( ESP_OK == result )
        {
            (void)attach_optional_device(
                RFSCOUT_BOARD_BQ27220_I2C_ADDRESS, "BQ27220 fuel gauge", &gauge_device );
            (void)attach_optional_device(
                RFSCOUT_BOARD_BQ25896_I2C_ADDRESS, "BQ25896 charger", &charger_device );
        }
    }
    if( RFSCOUT_BOARD_TEMBED_SI4732 == board_profile()->id )
    {
        const esp_err_t analog_result = init_analog_battery();
        if( ESP_OK != analog_result )
        {
            ESP_LOGW( TAG, "analog voltage unavailable: %s", esp_err_to_name( analog_result ) );
        }
        if( ESP_OK != result && ESP_OK == analog_result )
        {
            result = ESP_OK;
        }
    }
    if( ESP_OK != result )
    {
        return result;
    }

    initialized = true;
    last_poll_us = -BATTERY_POLL_INTERVAL_US;
    battery_refresh();
    return ESP_OK;
}

void battery_poll( void )
{
    if( !initialized )
    {
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if( ( now_us - last_poll_us ) < BATTERY_POLL_INTERVAL_US )
    {
        return;
    }
    battery_refresh();
}

bool battery_get_status( battery_status_t *a_status )
{
    if( NULL == a_status )
    {
        return false;
    }
    *a_status = cached_status;
    return cached_status.available || cached_status.charger_available;
}

bool battery_external_power_telemetry_expected( void )
{
    return RFSCOUT_BATTERY_I2C_GAUGE_CHARGER == RFSCOUT_BOARD_BATTERY_TYPE;
}

esp_err_t battery_shutdown( void )
{
    if( RFSCOUT_BATTERY_I2C_GAUGE_CHARGER != RFSCOUT_BOARD_BATTERY_TYPE )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if( NULL == charger_device )
    {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t power_path = 0;
    esp_err_t result = read_register_u8( charger_device, BQ25896_REG_POWER_PATH, &power_path );
    if( ESP_OK != result )
    {
        return result;
    }

    power_path |= BQ25896_BATFET_DISABLE_BIT;
    return write_register_u8( charger_device, BQ25896_REG_POWER_PATH, power_path );
}
