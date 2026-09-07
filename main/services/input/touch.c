/**
    touch.c : RFScout GT911 touch provider
    Description: Safely probes and samples GT911 touch data for T-Deck bring-up diagnostics.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "touch.h"

#include "board.h"
#include "board_i2c.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include <stddef.h>
#include <string.h>

#define GT911_ADDRESS_PRIMARY 0x5DU
#define GT911_ADDRESS_SECONDARY 0x14U
#define GT911_REG_PRODUCT_ID 0x8140U
#define GT911_REG_STATUS 0x814EU
#define GT911_REG_POINT1 0x814FU
#define GT911_STATUS_READY 0x80U
#define GT911_STATUS_COUNT_MASK 0x0FU
#define GT911_MAX_POINTS 5U
#define TOUCH_DIAGNOSTIC_LOG_US 250000LL

static const char *TAG = "touch";
static i2c_master_dev_handle_t gt911;
static bool available;
static rfscout_touch_sample_t latest;
static bool have_sample;
static portMUX_TYPE sample_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t last_diagnostic_log_us;

static esp_err_t read_register( uint16_t a_register, uint8_t *a_data, size_t a_length )
{
    const uint8_t address[ 2 ] = { (uint8_t)( a_register >> 8U ), (uint8_t)a_register };
    return board_i2c_transmit_receive( gt911, address, sizeof( address ), a_data, a_length );
}

static esp_err_t write_u8( uint16_t a_register, uint8_t a_value )
{
    const uint8_t data[ 3 ] = {
        (uint8_t)( a_register >> 8U ),
        (uint8_t)a_register,
        a_value,
    };
    return board_i2c_transmit( gt911, data, sizeof( data ) );
}

static void
apply_reference_transform( uint16_t a_raw_x, uint16_t a_raw_y, uint16_t *a_x, uint16_t *a_y )
{
    /* LILYGO's T-Deck reference uses a 240x320 GT911 coordinate space, then
       swaps X/Y and mirrors Y to match the 320x240 landscape display. This is
       intentionally diagnostic-only until RFScout has a validated calibration record. */
    uint16_t x = a_raw_y;
    uint16_t y = ( a_raw_x < 240U ) ? (uint16_t)( 239U - a_raw_x ) : 0U;
    if( x >= RFSCOUT_BOARD_DISPLAY_WIDTH )
    {
        x = (uint16_t)( RFSCOUT_BOARD_DISPLAY_WIDTH - 1U );
    }
    if( y >= RFSCOUT_BOARD_DISPLAY_HEIGHT )
    {
        y = (uint16_t)( RFSCOUT_BOARD_DISPLAY_HEIGHT - 1U );
    }
    *a_x = x;
    *a_y = y;
}

static void publish_sample( const rfscout_touch_sample_t *a_sample )
{
    portENTER_CRITICAL( &sample_lock );
    latest = *a_sample;
    have_sample = true;
    portEXIT_CRITICAL( &sample_lock );
}

esp_err_t touch_init( void )
{
    if( available )
    {
        return ESP_OK;
    }
    if( !RFSCOUT_BOARD_HAS_TOUCH )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    gt911 = NULL;
    portENTER_CRITICAL( &sample_lock );
    have_sample = false;
    memset( &latest, 0, sizeof( latest ) );
    portEXIT_CRITICAL( &sample_lock );

    uint16_t address = GT911_ADDRESS_PRIMARY;
    esp_err_t result = board_i2c_probe( address );
    if( ESP_OK != result )
    {
        address = GT911_ADDRESS_SECONDARY;
        result = board_i2c_probe( address );
    }
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "GT911 not found at 0x5D or 0x14" );
        return ESP_ERR_NOT_FOUND;
    }

    result = board_i2c_add_device( address, &gt911 );
    if( ESP_OK != result )
    {
        return result;
    }

    uint8_t product_id[ 4 ] = { 0 };
    result = read_register( GT911_REG_PRODUCT_ID, product_id, sizeof( product_id ) );
    if( ESP_OK != result )
    {
        gt911 = NULL;
        return result;
    }

    available = true;
    last_diagnostic_log_us = 0;
    ESP_LOGI( TAG,
              "GT911 ready at 0x%02X product=%.4s irq=GPIO%d; raw touch is diagnostic only until "
              "calibrated",
              (unsigned)address,
              (const char *)product_id,
              RFSCOUT_BOARD_TOUCH_IRQ_PIN );
    return ESP_OK;
}

void touch_poll( void )
{
    if( !available || ( NULL == gt911 ) )
    {
        return;
    }

    uint8_t status = 0U;
    if( ESP_OK != read_register( GT911_REG_STATUS, &status, sizeof( status ) ) ||
        0U == ( status & GT911_STATUS_READY ) )
    {
        return;
    }

    rfscout_touch_sample_t sample;
    portENTER_CRITICAL( &sample_lock );
    sample = latest;
    portEXIT_CRITICAL( &sample_lock );

    const uint8_t count = status & GT911_STATUS_COUNT_MASK;
    sample.point_count = count <= GT911_MAX_POINTS ? count : GT911_MAX_POINTS;
    sample.pressed = sample.point_count > 0U;
    ++sample.sequence;

    if( sample.pressed )
    {
        uint8_t point[ 8 ] = { 0 };
        if( ESP_OK == read_register( GT911_REG_POINT1, point, sizeof( point ) ) )
        {
            sample.raw_x = (uint16_t)point[ 1 ] | ( (uint16_t)point[ 2 ] << 8U );
            sample.raw_y = (uint16_t)point[ 3 ] | ( (uint16_t)point[ 4 ] << 8U );
            apply_reference_transform(
                sample.raw_x, sample.raw_y, &sample.reference_x, &sample.reference_y );
            publish_sample( &sample );

            const int64_t now_us = esp_timer_get_time();
            if( now_us - last_diagnostic_log_us >= TOUCH_DIAGNOSTIC_LOG_US )
            {
                last_diagnostic_log_us = now_us;
                ESP_LOGI( TAG,
                          "diagnostic touch raw=(%u,%u) reference=(%u,%u) points=%u",
                          (unsigned)sample.raw_x,
                          (unsigned)sample.raw_y,
                          (unsigned)sample.reference_x,
                          (unsigned)sample.reference_y,
                          (unsigned)sample.point_count );
            }
        }
    }
    else
    {
        publish_sample( &sample );
    }

    (void)write_u8( GT911_REG_STATUS, 0U );
}

bool touch_available( void )
{
    return available;
}

bool touch_sample_get( rfscout_touch_sample_t *a_sample )
{
    if( NULL == a_sample )
    {
        return false;
    }

    portENTER_CRITICAL( &sample_lock );
    const bool result = have_sample;
    if( result )
    {
        *a_sample = latest;
    }
    portEXIT_CRITICAL( &sample_lock );
    return result;
}
