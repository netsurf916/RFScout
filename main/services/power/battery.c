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

#include <stddef.h>
#include <string.h>

#define BATTERY_POLL_INTERVAL_US 1000000LL
#define BQ27220_REG_VOLTAGE 0x08U
#define BQ27220_REG_STATUS 0x0AU
#define BQ27220_REG_CURRENT 0x0CU
#define BQ27220_REG_STATE_OF_CHARGE 0x2CU
#define BQ27220_REG_TEMPERATURE 0x06U
#define BQ27220_REG_REMAINING_CAPACITY 0x10U
#define BQ27220_REG_FULL_CAPACITY 0x12U
#define BQ27220_REG_CYCLE_COUNT 0x2AU
#define BQ27220_REG_STATE_OF_HEALTH 0x2EU
#define BQ27220_REG_DESIGN_CAPACITY 0x3CU
#define BQ27220_STATUS_DISCHARGING 0x0001U
#define BQ27220_STATUS_FULL 0x0200U

#define BQ25896_REG_POWER_PATH 0x09U
#define BQ25896_REG_STATUS 0x0BU
#define BQ25896_REG_INPUT_CONTROL 0x00U
#define BQ25896_REG_CHARGE_CONTROL 0x03U
#define BQ25896_REG_CURRENT_CONTROL 0x04U
#define BQ25896_REG_PRECHARGE_CONTROL 0x05U
#define BQ25896_REG_VOLTAGE_CONTROL 0x06U
#define BQ25896_REG_TIMER_CONTROL 0x07U
#define BQ25896_REG_FAULT 0x0CU
#define BQ25896_REG_RESET_CONTROL 0x14U
#define BQ25896_REG_DPM_STATUS 0x13U
#define BQ25896_REG_ADC_VBUS 0x11U
#define BQ25896_REG_ADC_CHARGE_CURRENT 0x12U
#define BQ25896_REG_ADC_SYSTEM_VOLTAGE 0x0FU
#define BQ25896_WATCHDOG_RESET_BIT 0x40U
#define BQ25896_TERMINATION_CURRENT_MASK 0x0FU
#define BQ25896_WATCHDOG_MASK 0x30U
#define BQ25896_WATCHDOG_40S 0x10U
#define BQ25896_WATCHDOG_FAULT_BIT 0x80U
#define CHARGER_WATCHDOG_REFRESH_US 10000000LL
#define CHARGER_RETRY_US 5000000LL
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
typedef enum
{
    CHARGER_CONTROLLER_DISCOVER = 0,
    CHARGER_CONTROLLER_INSPECT,
    CHARGER_CONTROLLER_MANAGED,
    CHARGER_CONTROLLER_RECOVERY,
    CHARGER_CONTROLLER_SHUTDOWN
} charger_controller_state_t;
static charger_controller_state_t charger_controller;
static int64_t charger_last_watchdog_us;
static int64_t charger_retry_after_us;
static uint8_t charger_timer_policy;
static uint8_t charger_charge_policy;
static uint8_t charger_precharge_policy;
static uint8_t charger_input_policy;
static uint8_t charger_current_policy;
static uint8_t charger_voltage_policy;
static bool charger_policy_valid;

static esp_err_t
read_register_u8( i2c_master_dev_handle_t a_device, uint8_t a_register, uint8_t *a_value )
{
    if( ( NULL == a_device ) || ( NULL == a_value ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    return board_i2c_transmit_receive( a_device, &a_register, 1, a_value, 1 );
}

static esp_err_t
write_register_u8( i2c_master_dev_handle_t a_device, uint8_t a_register, uint8_t a_value )
{
    if( NULL == a_device )
    {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t data[ 2 ] = { a_register, a_value };
    return board_i2c_transmit( a_device, data, sizeof( data ) );
}

static esp_err_t
read_register_u16( i2c_master_dev_handle_t a_device, uint8_t a_register, uint16_t *a_value )
{
    if( ( NULL == a_device ) || ( NULL == a_value ) )
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t data[ 2 ];
    const esp_err_t result =
        board_i2c_transmit_receive( a_device, &a_register, 1, data, sizeof( data ) );
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

/** @brief Alternate which battery IC gets first access after shared-bus recovery. */
static bool charger_first;

static void sample_gauge( battery_status_t *a_status, bool *a_sampled, uint16_t *a_gauge_status )
{
    if( NULL == gauge_device || !board_i2c_ready() )
    {
        return;
    }
    uint16_t percentage = 0U;
    const esp_err_t result =
        read_register_u16( gauge_device, BQ27220_REG_STATE_OF_CHARGE, &percentage );
    if( ESP_OK == result && percentage <= 100U )
    {
        a_status->percentage = (uint8_t)percentage;
        a_status->available = true;
        a_status->percentage_fresh = true;
        a_status->gauge_available = true;
        *a_sampled = true;
        uint16_t voltage_mv = 0U;
        if( ESP_OK == read_register_u16( gauge_device, BQ27220_REG_VOLTAGE, &voltage_mv ) )
        {
            a_status->voltage_mv = voltage_mv;
        }
        if( !board_i2c_ready() )
        {
            return;
        }
        uint16_t current_raw = 0U;
        if( ESP_OK == read_register_u16( gauge_device, BQ27220_REG_CURRENT, &current_raw ) )
        {
            a_status->current_ma = (int16_t)current_raw;
        }
        if( board_i2c_ready() )
        {
            (void)read_register_u16( gauge_device, BQ27220_REG_STATUS, a_gauge_status );
        }
        /* The extended snapshot is valid only when every field was sampled. */
        if( board_i2c_ready() &&
            ESP_OK == read_register_u16( gauge_device,
                                         BQ27220_REG_REMAINING_CAPACITY,
                                         &a_status->remaining_capacity_mah ) &&
            ESP_OK == read_register_u16(
                          gauge_device, BQ27220_REG_FULL_CAPACITY, &a_status->full_capacity_mah ) &&
            ESP_OK == read_register_u16( gauge_device,
                                         BQ27220_REG_DESIGN_CAPACITY,
                                         &a_status->design_capacity_mah ) &&
            ESP_OK == read_register_u16(
                          gauge_device, BQ27220_REG_CYCLE_COUNT, &a_status->cycle_count ) &&
            ESP_OK == read_register_u16( gauge_device,
                                         BQ27220_REG_STATE_OF_HEALTH,
                                         &a_status->state_of_health_percent ) &&
            ESP_OK == read_register_u16( gauge_device,
                                         BQ27220_REG_TEMPERATURE,
                                         &a_status->temperature_decikelvin ) )
        {
            a_status->gauge_details_fresh = true;
        }
    }
    else if( ESP_OK != result && ESP_ERR_INVALID_STATE != result )
    {
        ESP_LOGW( TAG, "BQ27220 no longer responding: %s", esp_err_to_name( result ) );
        detach_optional_device( &gauge_device );
    }
}

/* The immutable board profile is the authority for charging limits. */
static esp_err_t charger_inspect_and_manage( void )
{
    const rfscout_charger_policy_t *policy = &board_profile()->charger_policy;
    if( 0U == policy->fast_charge_current_ma || policy->input_current_ma < 100U ||
        policy->input_current_ma > 3250U || policy->fast_charge_current_ma > 5056U ||
        policy->precharge_current_ma < 64U || policy->precharge_current_ma > 1024U ||
        policy->termination_current_ma < 64U || policy->termination_current_ma > 1024U ||
        policy->regulation_voltage_mv < 3840U || policy->regulation_voltage_mv > 4608U )
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t timer = 0U;
    uint8_t control = 0U;
    uint8_t precharge = 0U;
    uint8_t input = 0U;
    uint8_t current = 0U;
    uint8_t voltage = 0U;
    esp_err_t result = read_register_u8( charger_device, BQ25896_REG_TIMER_CONTROL, &timer );
    if( ESP_OK == result )
    {
        result = read_register_u8( charger_device, BQ25896_REG_CHARGE_CONTROL, &control );
    }
    if( ESP_OK == result )
    {
        result = read_register_u8( charger_device, BQ25896_REG_PRECHARGE_CONTROL, &precharge );
    }
    if( ESP_OK == result )
    {
        result = read_register_u8( charger_device, BQ25896_REG_INPUT_CONTROL, &input );
    }
    if( ESP_OK == result )
    {
        result = read_register_u8( charger_device, BQ25896_REG_CURRENT_CONTROL, &current );
    }
    if( ESP_OK == result )
    {
        result = read_register_u8( charger_device, BQ25896_REG_VOLTAGE_CONTROL, &voltage );
    }
    if( ESP_OK != result )
    {
        return result;
    }
    charger_precharge_policy = ( (uint8_t)( ( policy->precharge_current_ma / 64U - 1U ) << 4U ) ) |
                               (uint8_t)( policy->termination_current_ma / 64U - 1U );
    charger_input_policy =
        ( input & 0x80U ) | (uint8_t)( ( policy->input_current_ma - 100U ) / 50U );
    charger_current_policy =
        ( current & 0x80U ) | (uint8_t)( policy->fast_charge_current_ma / 64U );
    charger_voltage_policy = ( voltage & 0x03U ) |
                             (uint8_t)( ( ( policy->regulation_voltage_mv - 3840U ) / 16U ) << 2U );
    charger_timer_policy = ( timer & (uint8_t)~BQ25896_WATCHDOG_MASK ) | BQ25896_WATCHDOG_40S;
    charger_charge_policy = control & (uint8_t)~BQ25896_WATCHDOG_RESET_BIT;
    charger_policy_valid = true;
    result = write_register_u8( charger_device, BQ25896_REG_INPUT_CONTROL, charger_input_policy );
    if( ESP_OK == result )
    {
        result = write_register_u8(
            charger_device, BQ25896_REG_CURRENT_CONTROL, charger_current_policy );
    }
    if( ESP_OK == result )
    {
        result = write_register_u8(
            charger_device, BQ25896_REG_VOLTAGE_CONTROL, charger_voltage_policy );
    }
    if( ESP_OK == result )
    {
        result = write_register_u8(
            charger_device, BQ25896_REG_PRECHARGE_CONTROL, charger_precharge_policy );
    }
    if( ESP_OK == result )
    {
        result =
            write_register_u8( charger_device, BQ25896_REG_TIMER_CONTROL, charger_timer_policy );
    }
    if( ESP_OK == result )
    {
        result = write_register_u8( charger_device,
                                    BQ25896_REG_CHARGE_CONTROL,
                                    charger_charge_policy | BQ25896_WATCHDOG_RESET_BIT );
    }
    if( ESP_OK == result )
    {
        charger_last_watchdog_us = esp_timer_get_time();
        charger_controller = CHARGER_CONTROLLER_MANAGED;
        ESP_LOGI( TAG, "BQ25896 board charging policy active, 40s watchdog" );
    }
    return result;
}

static void charger_controller_poll( void )
{
    if( NULL == charger_device || !board_i2c_ready() ||
        CHARGER_CONTROLLER_SHUTDOWN == charger_controller )
    {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if( CHARGER_CONTROLLER_MANAGED == charger_controller )
    {
        if( now - charger_last_watchdog_us < CHARGER_WATCHDOG_REFRESH_US )
        {
            return;
        }
        uint8_t timer = 0U;
        esp_err_t result = read_register_u8( charger_device, BQ25896_REG_TIMER_CONTROL, &timer );
        uint8_t precharge = 0U;
        if( ESP_OK == result )
        {
            result = read_register_u8( charger_device, BQ25896_REG_PRECHARGE_CONTROL, &precharge );
        }
        if( ESP_OK == result && charger_policy_valid && timer == charger_timer_policy &&
            precharge == charger_precharge_policy )
        {
            result = write_register_u8( charger_device,
                                        BQ25896_REG_CHARGE_CONTROL,
                                        charger_charge_policy | BQ25896_WATCHDOG_RESET_BIT );
            if( ESP_OK == result )
            {
                charger_last_watchdog_us = now;
                return;
            }
        }
        ESP_LOGW( TAG, "BQ25896 configuration/watchdog lost: %s", esp_err_to_name( result ) );
        charger_controller = CHARGER_CONTROLLER_RECOVERY;
        charger_retry_after_us = now + CHARGER_RETRY_US;
        return;
    }
    if( CHARGER_CONTROLLER_RECOVERY == charger_controller && now < charger_retry_after_us )
    {
        return;
    }
    charger_controller = CHARGER_CONTROLLER_INSPECT;
    const esp_err_t result = charger_inspect_and_manage();
    if( ESP_OK != result )
    {
        charger_controller = CHARGER_CONTROLLER_RECOVERY;
        charger_retry_after_us = now + CHARGER_RETRY_US;
        ESP_LOGW( TAG, "BQ25896 host setup deferred: %s", esp_err_to_name( result ) );
    }
}

static void sample_charger( battery_status_t *a_status, bool *a_sampled )
{
    if( NULL == charger_device || !board_i2c_ready() )
    {
        return;
    }
    uint8_t status = 0U;
    const esp_err_t result = read_register_u8( charger_device, BQ25896_REG_STATUS, &status );
    if( ESP_OK == result )
    {
        charger_controller_poll();
        a_status->charger_status_register = status;
        a_status->charger_controller_state = (uint8_t)charger_controller;
        const uint8_t bus_status = ( status >> BQ25896_BUS_STATUS_SHIFT ) & BQ25896_BUS_STATUS_MASK;
        const bool valid_input =
            BQ25896_BUS_NO_INPUT != bus_status && BQ25896_BUS_OTG != bus_status;
        a_status->external_power = valid_input || ( 0U != ( status & BQ25896_POWER_GOOD_BIT ) );
        a_status->charge_state = charger_charge_state( status, a_status->external_power );
        a_status->charger_available = true;
        a_status->charger_fresh = true;
        *a_sampled = true;
        if( board_i2c_ready() &&
            ESP_OK == read_register_u8( charger_device,
                                        BQ25896_REG_INPUT_CONTROL,
                                        &a_status->charger_input_control ) &&
            ESP_OK == read_register_u8( charger_device,
                                        BQ25896_REG_CHARGE_CONTROL,
                                        &a_status->charger_charge_control ) &&
            ESP_OK == read_register_u8( charger_device,
                                        BQ25896_REG_CURRENT_CONTROL,
                                        &a_status->charger_current_control ) &&
            ESP_OK == read_register_u8( charger_device,
                                        BQ25896_REG_PRECHARGE_CONTROL,
                                        &a_status->charger_precharge_control ) &&
            ESP_OK == read_register_u8( charger_device,
                                        BQ25896_REG_VOLTAGE_CONTROL,
                                        &a_status->charger_voltage_control ) &&
            ESP_OK == read_register_u8( charger_device,
                                        BQ25896_REG_TIMER_CONTROL,
                                        &a_status->charger_timer_control ) &&
            ESP_OK ==
                read_register_u8( charger_device, BQ25896_REG_FAULT, &a_status->charger_faults ) )
        {
            a_status->charger_details_fresh = true;
            if( ESP_OK != read_register_u8( charger_device,
                                            BQ25896_REG_DPM_STATUS,
                                            &a_status->charger_dpm_status ) ||
                ESP_OK != read_register_u8(
                              charger_device, BQ25896_REG_ADC_VBUS, &a_status->charger_adc_vbus ) ||
                ESP_OK != read_register_u8( charger_device,
                                            BQ25896_REG_ADC_CHARGE_CURRENT,
                                            &a_status->charger_adc_charge_current ) ||
                ESP_OK != read_register_u8( charger_device,
                                            BQ25896_REG_ADC_SYSTEM_VOLTAGE,
                                            &a_status->charger_adc_system_voltage ) )
            {
                a_status->charger_details_fresh = false;
            }
            /* REG0C first exposes latched history; the second read reports
               present faults. Do not mistake old watchdog events for current ones. */
            uint8_t current_faults = 0U;
            if( ESP_OK == read_register_u8( charger_device, BQ25896_REG_FAULT, &current_faults ) )
            {
                if( 0U != ( a_status->charger_faults & BQ25896_WATCHDOG_FAULT_BIT ) )
                {
                    ESP_LOGD( TAG, "BQ25896 historical watchdog fault" );
                }
                a_status->charger_faults = current_faults;
            }
            else
            {
                a_status->charger_details_fresh = false;
            }
        }
    }
    else if( ESP_ERR_INVALID_STATE != result )
    {
        ESP_LOGW( TAG, "BQ25896 no longer responding: %s", esp_err_to_name( result ) );
        charger_controller = CHARGER_CONTROLLER_RECOVERY;
        charger_retry_after_us = esp_timer_get_time() + CHARGER_RETRY_US;
        detach_optional_device( &charger_device );
    }
}

static void refresh_i2c_battery( battery_status_t *a_status )
{
    /* A failing peer must not always preempt sampling the healthy device. */
    const bool charger_priority = charger_first;
    charger_first = !charger_first;

    bool gauge_sampled = false;
    bool charger_sampled = false;
    uint16_t gauge_status = 0U;
    if( charger_priority )
    {
        if( NULL == charger_device && board_i2c_ready() )
        {
            (void)attach_optional_device(
                RFSCOUT_BOARD_BQ25896_I2C_ADDRESS, "BQ25896 charger", &charger_device );
        }
        sample_charger( a_status, &charger_sampled );
        if( NULL == gauge_device && board_i2c_ready() )
        {
            (void)attach_optional_device(
                RFSCOUT_BOARD_BQ27220_I2C_ADDRESS, "BQ27220 fuel gauge", &gauge_device );
        }
        sample_gauge( a_status, &gauge_sampled, &gauge_status );
    }
    else
    {
        if( NULL == gauge_device && board_i2c_ready() )
        {
            (void)attach_optional_device(
                RFSCOUT_BOARD_BQ27220_I2C_ADDRESS, "BQ27220 fuel gauge", &gauge_device );
        }
        sample_gauge( a_status, &gauge_sampled, &gauge_status );
        if( NULL == charger_device && board_i2c_ready() )
        {
            (void)attach_optional_device(
                RFSCOUT_BOARD_BQ25896_I2C_ADDRESS, "BQ25896 charger", &charger_device );
        }
        sample_charger( a_status, &charger_sampled );
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

    /* Preserve cached battery readings while another client recovers the bus. */
    if( !board_i2c_ready() )
    {
        cached_status.percentage_fresh = false;
        cached_status.charger_fresh = false;
        cached_status.gauge_details_fresh = false;
        cached_status.charger_details_fresh = false;
        return;
    }
    last_poll_us = esp_timer_get_time();
    battery_status_t next = cached_status;
    next.available = false;
    next.percentage_fresh = false;
    next.gauge_available = false;
    next.charger_available = false;
    next.charger_fresh = false;
    next.gauge_details_fresh = false;
    next.charger_details_fresh = false;

    if( RFSCOUT_BATTERY_I2C_GAUGE_CHARGER == RFSCOUT_BOARD_BATTERY_TYPE )
    {
        refresh_i2c_battery( &next );
    }
    /* A timeout invalidates unobserved values, not a healthy peer's sample
       already completed during this refresh. */
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
    charger_controller = CHARGER_CONTROLLER_DISCOVER;
    charger_policy_valid = false;
    charger_last_watchdog_us = 0;
    charger_retry_after_us = 0;

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

    charger_controller = CHARGER_CONTROLLER_SHUTDOWN;
    power_path |= BQ25896_BATFET_DISABLE_BIT;
    result = write_register_u8( charger_device, BQ25896_REG_POWER_PATH, power_path );
    if( ESP_OK != result )
    {
        charger_controller = CHARGER_CONTROLLER_RECOVERY;
        charger_retry_after_us = esp_timer_get_time() + CHARGER_RETRY_US;
    }
    return result;
}
