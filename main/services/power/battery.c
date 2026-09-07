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
#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_ADC_FILTER_NUMERATOR 3U
#define BATTERY_ADC_FILTER_DENOMINATOR 4U

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

typedef struct
{
    uint16_t millivolts;
    uint8_t percent;
} battery_soc_point_t;

static const battery_soc_point_t LIPO_SOC_CURVE[] = {
    { 3300U, 0U },  { 3500U, 5U },  { 3600U, 12U }, { 3700U, 30U }, { 3800U, 50U },
    { 3900U, 65U }, { 4000U, 80U }, { 4100U, 90U }, { 4200U, 100U },
};

static const char *TAG = "battery";
static i2c_master_dev_handle_t gauge_device;
static i2c_master_dev_handle_t charger_device;
static adc_oneshot_unit_handle_t adc_unit;
static adc_cali_handle_t adc_calibration;
static adc_channel_t adc_channel;
static battery_status_t cached_status;
static int64_t last_poll_us;
static uint32_t filtered_voltage_mv;
static bool have_filtered_voltage;
static bool initialized;

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
        ESP_LOGW( TAG, "%s not detected at 0x%02X", a_name, (unsigned)a_address );
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

static uint8_t estimate_soc( uint16_t a_voltage_mv )
{
    if( a_voltage_mv <= LIPO_SOC_CURVE[ 0 ].millivolts )
    {
        return LIPO_SOC_CURVE[ 0 ].percent;
    }
    const size_t count = sizeof( LIPO_SOC_CURVE ) / sizeof( LIPO_SOC_CURVE[ 0 ] );
    if( a_voltage_mv >= LIPO_SOC_CURVE[ count - 1U ].millivolts )
    {
        return LIPO_SOC_CURVE[ count - 1U ].percent;
    }

    for( size_t index = 1U; index < count; ++index )
    {
        const battery_soc_point_t *low = &LIPO_SOC_CURVE[ index - 1U ];
        const battery_soc_point_t *high = &LIPO_SOC_CURVE[ index ];
        if( a_voltage_mv <= high->millivolts )
        {
            const uint32_t span_mv = (uint32_t)( high->millivolts - low->millivolts );
            const uint32_t offset_mv = (uint32_t)( a_voltage_mv - low->millivolts );
            const uint32_t span_percent = (uint32_t)( high->percent - low->percent );
            return (uint8_t)( low->percent + ( offset_mv * span_percent ) / span_mv );
        }
    }
    return 0U;
}

static esp_err_t init_adc_provider( void )
{
    adc_unit_t unit_id;
    esp_err_t result =
        adc_oneshot_io_to_channel( RFSCOUT_BOARD_BATTERY_ADC_PIN, &unit_id, &adc_channel );
    if( ESP_OK != result )
    {
        return result;
    }

    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = unit_id,
    };
    result = adc_oneshot_new_unit( &unit_config, &adc_unit );
    if( ESP_OK != result )
    {
        return result;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    result = adc_oneshot_config_channel( adc_unit, adc_channel, &channel_config );
    if( ESP_OK != result )
    {
        return result;
    }

    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = unit_id,
        .chan = adc_channel,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    result = adc_cali_create_scheme_curve_fitting( &calibration_config, &adc_calibration );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "battery ADC calibration unavailable: %s", esp_err_to_name( result ) );
        return result;
    }

    ESP_LOGI( TAG,
              "voltage-only battery provider: GPIO%d divider=%u/%u",
              RFSCOUT_BOARD_BATTERY_ADC_PIN,
              (unsigned)RFSCOUT_BOARD_BATTERY_DIVIDER_NUMERATOR,
              (unsigned)RFSCOUT_BOARD_BATTERY_DIVIDER_DENOMINATOR );
    return ESP_OK;
}

static esp_err_t sample_adc_battery( battery_status_t *a_status )
{
    if( NULL == a_status || NULL == adc_unit || NULL == adc_calibration ||
        0U == RFSCOUT_BOARD_BATTERY_DIVIDER_DENOMINATOR )
    {
        return ESP_ERR_INVALID_STATE;
    }

    int raw = 0;
    esp_err_t result = adc_oneshot_read( adc_unit, adc_channel, &raw );
    if( ESP_OK != result )
    {
        return result;
    }
    int measured_mv = 0;
    result = adc_cali_raw_to_voltage( adc_calibration, raw, &measured_mv );
    if( ESP_OK != result || measured_mv < 0 )
    {
        return ESP_OK == result ? ESP_ERR_INVALID_RESPONSE : result;
    }

    const uint32_t battery_mv =
        ( (uint32_t)measured_mv * RFSCOUT_BOARD_BATTERY_DIVIDER_NUMERATOR ) /
        RFSCOUT_BOARD_BATTERY_DIVIDER_DENOMINATOR;
    if( !have_filtered_voltage )
    {
        filtered_voltage_mv = battery_mv;
        have_filtered_voltage = true;
    }
    else
    {
        filtered_voltage_mv =
            ( filtered_voltage_mv * BATTERY_ADC_FILTER_NUMERATOR + battery_mv ) /
            BATTERY_ADC_FILTER_DENOMINATOR;
    }

    a_status->voltage_mv = (uint16_t)( filtered_voltage_mv > UINT16_MAX ? UINT16_MAX
                                                                            : filtered_voltage_mv );
    a_status->percentage = estimate_soc( a_status->voltage_mv );
    a_status->available = true;
    a_status->percentage_fresh = true;
    a_status->gauge_available = false;
    a_status->charger_available = false;
    a_status->charger_fresh = false;
    a_status->external_power = false;
    a_status->current_ma = 0;
    a_status->charge_state = BATTERY_CHARGE_UNKNOWN;

    ESP_LOGD( TAG,
              "battery ADC raw=%d measured=%dmV battery=%umV filtered=%umV soc=%u%%",
              raw,
              measured_mv,
              (unsigned)battery_mv,
              (unsigned)a_status->voltage_mv,
              (unsigned)a_status->percentage );
    return ESP_OK;
}

static void refresh_i2c_battery( battery_status_t *a_status )
{
    bool gauge_sampled = false;
    uint16_t gauge_status = 0;
    if( NULL != gauge_device )
    {
        uint16_t percentage = 0;
        if( ( ESP_OK ==
              read_register_u16( gauge_device, BQ27220_REG_STATE_OF_CHARGE, &percentage ) ) &&
            ( percentage <= 100U ) )
        {
            a_status->percentage = (uint8_t)percentage;
            a_status->available = true;
            a_status->percentage_fresh = true;
            a_status->gauge_available = true;
            gauge_sampled = true;
        }
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
            a_status->external_power =
                valid_input || ( 0U != ( charger_status & BQ25896_POWER_GOOD_BIT ) );
            a_status->charge_state = charger_charge_state( charger_status, a_status->external_power );
            a_status->charger_available = true;
            a_status->charger_fresh = true;
            charger_sampled = true;
        }
    }

    if( !charger_sampled && gauge_sampled )
    {
        a_status->charge_state = gauge_charge_state( gauge_status, a_status->current_ma );
        a_status->external_power = ( BATTERY_CHARGE_CHARGING == a_status->charge_state ) ||
                                   ( BATTERY_CHARGE_COMPLETE == a_status->charge_state );
    }
}

void battery_refresh( void )
{
    if( !initialized )
    {
        return;
    }

    last_poll_us = esp_timer_get_time();
    battery_status_t next = cached_status;
    next.percentage_fresh = false;
    next.charger_fresh = false;

    if( RFSCOUT_BATTERY_ADC_VOLTAGE_ONLY == RFSCOUT_BOARD_BATTERY_TYPE )
    {
        if( ESP_OK != sample_adc_battery( &next ) )
        {
            next.percentage_fresh = false;
        }
    }
    else if( RFSCOUT_BATTERY_I2C_GAUGE_CHARGER == RFSCOUT_BOARD_BATTERY_TYPE )
    {
        refresh_i2c_battery( &next );
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
    adc_unit = NULL;
    adc_calibration = NULL;
    have_filtered_voltage = false;

    esp_err_t result = ESP_OK;
    if( RFSCOUT_BATTERY_ADC_VOLTAGE_ONLY == RFSCOUT_BOARD_BATTERY_TYPE )
    {
        result = init_adc_provider();
    }
    else if( RFSCOUT_BATTERY_I2C_GAUGE_CHARGER == RFSCOUT_BOARD_BATTERY_TYPE )
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
    else
    {
        return ESP_ERR_NOT_SUPPORTED;
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
