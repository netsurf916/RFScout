/**
    touch.c : RFScout GT911 touch provider
    Description: Probes, samples, and calibrates GT911 touch data for the selected runtime profile.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "touch.h"

#include "board.h"
#include "board_i2c.h"
#include "persistent_store.h"

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

#define TOUCH_CALIBRATION_KEY "touch.calibration"
#define TOUCH_CALIBRATION_MAGIC 0x5443414CU
#define TOUCH_CALIBRATION_VERSION 1U
#define TOUCH_CALIBRATION_POINTS 5U
#define TOUCH_TRANSFORM_SWAP_XY ( 1U << 0 )
#define TOUCH_TRANSFORM_MIRROR_X ( 1U << 1 )
#define TOUCH_TRANSFORM_MIRROR_Y ( 1U << 2 )

static const char *TAG = "touch";
static const char *CALIBRATION_PROMPTS[ TOUCH_CALIBRATION_POINTS ] = {
    "TOP LEFT", "TOP RIGHT", "BOTTOM LEFT", "BOTTOM RIGHT", "CENTER" };

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t board_id;
    uint16_t display_width;
    uint16_t display_height;
    uint16_t native_width;
    uint16_t native_height;
    uint8_t transform_flags;
    uint8_t reserved;
    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
} touch_calibration_record_t;

typedef struct
{
    uint16_t x;
    uint16_t y;
} calibration_point_t;

_Static_assert( sizeof( touch_calibration_record_t ) <= 64U,
                "touch calibration record must stay bounded" );

static i2c_master_dev_handle_t gt911;
static bool available;
static rfscout_touch_sample_t latest;
static bool have_sample;
static portMUX_TYPE sample_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t last_diagnostic_log_us;

static touch_calibration_record_t calibration;
static bool calibration_valid;
static bool calibration_active;
static bool calibration_press_latched;
static unsigned calibration_count;
static calibration_point_t calibration_points[ TOUCH_CALIBRATION_POINTS ];

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

static uint8_t profile_transform_flags( void )
{
    uint8_t flags = 0U;
    if( RFSCOUT_BOARD_DISPLAY_SWAP_XY )
    {
        flags |= TOUCH_TRANSFORM_SWAP_XY;
    }
    if( RFSCOUT_BOARD_DISPLAY_MIRROR_X )
    {
        flags |= TOUCH_TRANSFORM_MIRROR_X;
    }
    if( RFSCOUT_BOARD_DISPLAY_MIRROR_Y )
    {
        flags |= TOUCH_TRANSFORM_MIRROR_Y;
    }
    return flags;
}

static bool calibration_record_compatible( const touch_calibration_record_t *a_record )
{
    if( NULL == a_record || TOUCH_CALIBRATION_MAGIC != a_record->magic ||
        TOUCH_CALIBRATION_VERSION != a_record->version ||
        (uint16_t)board_profile()->id != a_record->board_id ||
        RFSCOUT_BOARD_DISPLAY_WIDTH != a_record->display_width ||
        RFSCOUT_BOARD_DISPLAY_HEIGHT != a_record->display_height ||
        RFSCOUT_BOARD_DISPLAY_NATIVE_WIDTH != a_record->native_width ||
        RFSCOUT_BOARD_DISPLAY_NATIVE_HEIGHT != a_record->native_height ||
        profile_transform_flags() != a_record->transform_flags )
    {
        return false;
    }

    return ( a_record->x_max > a_record->x_min ) && ( a_record->y_max > a_record->y_min ) &&
           ( (uint32_t)( a_record->x_max - a_record->x_min ) >=
             (uint32_t)RFSCOUT_BOARD_DISPLAY_WIDTH / 2U ) &&
           ( (uint32_t)( a_record->y_max - a_record->y_min ) >=
             (uint32_t)RFSCOUT_BOARD_DISPLAY_HEIGHT / 2U );
}

static void
apply_reference_transform( uint16_t a_raw_x, uint16_t a_raw_y, uint16_t *a_x, uint16_t *a_y )
{
    /* Hardware-validated on T-Deck Plus: GT911 reports a 240x320 coordinate space.
       RFScout maps it to the 320x240 landscape display by swapping X/Y and mirroring Y. */
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

static uint16_t
normalize_axis( uint16_t a_value, uint16_t a_min, uint16_t a_max, uint16_t a_extent )
{
    if( a_extent <= 1U || a_max <= a_min || a_value <= a_min )
    {
        return 0U;
    }
    if( a_value >= a_max )
    {
        return (uint16_t)( a_extent - 1U );
    }
    const uint32_t numerator = (uint32_t)( a_value - a_min ) * (uint32_t)( a_extent - 1U );
    return (uint16_t)( numerator / (uint32_t)( a_max - a_min ) );
}

static void apply_calibration( rfscout_touch_sample_t *a_sample )
{
    a_sample->calibrated = calibration_valid && !calibration_active;
    if( !a_sample->calibrated )
    {
        a_sample->x = a_sample->reference_x;
        a_sample->y = a_sample->reference_y;
        return;
    }

    a_sample->x = normalize_axis(
        a_sample->reference_x, calibration.x_min, calibration.x_max, RFSCOUT_BOARD_DISPLAY_WIDTH );
    a_sample->y = normalize_axis(
        a_sample->reference_y, calibration.y_min, calibration.y_max, RFSCOUT_BOARD_DISPLAY_HEIGHT );
}

static void publish_sample( const rfscout_touch_sample_t *a_sample )
{
    portENTER_CRITICAL( &sample_lock );
    latest = *a_sample;
    have_sample = true;
    portEXIT_CRITICAL( &sample_lock );
}

static bool calibration_points_valid( touch_calibration_record_t *a_record )
{
    const calibration_point_t *top_left = &calibration_points[ 0 ];
    const calibration_point_t *top_right = &calibration_points[ 1 ];
    const calibration_point_t *bottom_left = &calibration_points[ 2 ];
    const calibration_point_t *bottom_right = &calibration_points[ 3 ];
    const calibration_point_t *center = &calibration_points[ 4 ];

    const uint16_t x_min = (uint16_t)( ( (uint32_t)top_left->x + bottom_left->x ) / 2U );
    const uint16_t x_max = (uint16_t)( ( (uint32_t)top_right->x + bottom_right->x ) / 2U );
    const uint16_t y_min = (uint16_t)( ( (uint32_t)top_left->y + top_right->y ) / 2U );
    const uint16_t y_max = (uint16_t)( ( (uint32_t)bottom_left->y + bottom_right->y ) / 2U );

    if( x_max <= x_min || y_max <= y_min ||
        (uint32_t)( x_max - x_min ) < (uint32_t)RFSCOUT_BOARD_DISPLAY_WIDTH / 2U ||
        (uint32_t)( y_max - y_min ) < (uint32_t)RFSCOUT_BOARD_DISPLAY_HEIGHT / 2U )
    {
        return false;
    }

    const uint16_t center_x = (uint16_t)( ( (uint32_t)x_min + x_max ) / 2U );
    const uint16_t center_y = (uint16_t)( ( (uint32_t)y_min + y_max ) / 2U );
    const uint16_t tolerance_x = (uint16_t)( ( x_max - x_min ) / 4U );
    const uint16_t tolerance_y = (uint16_t)( ( y_max - y_min ) / 4U );
    const uint16_t center_dx = center->x > center_x ? (uint16_t)( center->x - center_x )
                                                    : (uint16_t)( center_x - center->x );
    const uint16_t center_dy = center->y > center_y ? (uint16_t)( center->y - center_y )
                                                    : (uint16_t)( center_y - center->y );
    if( center_dx > tolerance_x || center_dy > tolerance_y )
    {
        return false;
    }

    memset( a_record, 0, sizeof( *a_record ) );
    a_record->magic = TOUCH_CALIBRATION_MAGIC;
    a_record->version = TOUCH_CALIBRATION_VERSION;
    a_record->board_id = (uint16_t)board_profile()->id;
    a_record->display_width = RFSCOUT_BOARD_DISPLAY_WIDTH;
    a_record->display_height = RFSCOUT_BOARD_DISPLAY_HEIGHT;
    a_record->native_width = RFSCOUT_BOARD_DISPLAY_NATIVE_WIDTH;
    a_record->native_height = RFSCOUT_BOARD_DISPLAY_NATIVE_HEIGHT;
    a_record->transform_flags = profile_transform_flags();
    a_record->x_min = x_min;
    a_record->x_max = x_max;
    a_record->y_min = y_min;
    a_record->y_max = y_max;
    return true;
}

static void calibration_capture( const rfscout_touch_sample_t *a_sample )
{
    if( !calibration_active || calibration_press_latched || !a_sample->pressed ||
        calibration_count >= TOUCH_CALIBRATION_POINTS )
    {
        return;
    }

    calibration_press_latched = true;
    calibration_points[ calibration_count ].x = a_sample->reference_x;
    calibration_points[ calibration_count ].y = a_sample->reference_y;
    ++calibration_count;
    ESP_LOGI( TAG,
              "calibration point %u/%u reference=(%u,%u)",
              calibration_count,
              TOUCH_CALIBRATION_POINTS,
              (unsigned)a_sample->reference_x,
              (unsigned)a_sample->reference_y );

    if( calibration_count < TOUCH_CALIBRATION_POINTS )
    {
        return;
    }

    touch_calibration_record_t candidate;
    if( !calibration_points_valid( &candidate ) )
    {
        ESP_LOGW( TAG, "touch calibration geometry rejected; restarting capture" );
        calibration_count = 0U;
        return;
    }

    const esp_err_t save_result =
        system_store_write( TOUCH_CALIBRATION_KEY, &candidate, sizeof( candidate ) );
    calibration = candidate;
    calibration_valid = true;
    calibration_active = false;
    if( ESP_OK == save_result )
    {
        ESP_LOGI( TAG,
                  "touch calibration saved x=%u..%u y=%u..%u",
                  (unsigned)candidate.x_min,
                  (unsigned)candidate.x_max,
                  (unsigned)candidate.y_min,
                  (unsigned)candidate.y_max );
    }
    else
    {
        ESP_LOGW( TAG,
                  "touch calibration active for this boot but could not be persisted: %s",
                  esp_err_to_name( save_result ) );
    }
}

static void load_calibration( void )
{
    calibration_valid = false;
    memset( &calibration, 0, sizeof( calibration ) );
    if( !persistent_store_is_available() )
    {
        return;
    }

    touch_calibration_record_t record;
    size_t length = 0U;
    const esp_err_t result =
        system_store_read( TOUCH_CALIBRATION_KEY, &record, sizeof( record ), &length );
    if( ESP_OK == result && sizeof( record ) == length && calibration_record_compatible( &record ) )
    {
        calibration = record;
        calibration_valid = true;
        ESP_LOGI( TAG,
                  "touch calibration loaded x=%u..%u y=%u..%u",
                  (unsigned)record.x_min,
                  (unsigned)record.x_max,
                  (unsigned)record.y_min,
                  (unsigned)record.y_max );
    }
    else if( ESP_OK == result )
    {
        ESP_LOGW( TAG, "stored touch calibration is incompatible; recalibration required" );
    }
}

esp_err_t touch_calibration_begin( void )
{
    if( !available )
    {
        return ESP_ERR_INVALID_STATE;
    }
    calibration_valid = false;
    calibration_active = true;
    calibration_press_latched = false;
    calibration_count = 0U;
    memset( calibration_points, 0, sizeof( calibration_points ) );
    ESP_LOGI( TAG, "touch calibration started" );
    return ESP_OK;
}

bool touch_calibration_valid( void )
{
    return calibration_valid && !calibration_active;
}

bool touch_calibration_active( void )
{
    return calibration_active;
}

const char *touch_calibration_prompt( void )
{
    return calibration_active && calibration_count < TOUCH_CALIBRATION_POINTS
               ? CALIBRATION_PROMPTS[ calibration_count ]
               : NULL;
}

void touch_calibration_progress( unsigned *a_step, unsigned *a_total )
{
    if( NULL != a_step )
    {
        *a_step = calibration_active ? calibration_count + 1U : 0U;
    }
    if( NULL != a_total )
    {
        *a_total = TOUCH_CALIBRATION_POINTS;
    }
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
    load_calibration();
    if( !calibration_valid )
    {
        (void)touch_calibration_begin();
    }

    ESP_LOGI( TAG,
              "GT911 ready at 0x%02X product=%.4s irq=GPIO%d calibration=%s",
              (unsigned)address,
              (const char *)product_id,
              RFSCOUT_BOARD_TOUCH_IRQ_PIN,
              touch_calibration_valid() ? "valid" : "required" );
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
            apply_calibration( &sample );
            calibration_capture( &sample );
            publish_sample( &sample );

            const int64_t now_us = esp_timer_get_time();
            if( now_us - last_diagnostic_log_us >= TOUCH_DIAGNOSTIC_LOG_US )
            {
                last_diagnostic_log_us = now_us;
                ESP_LOGI( TAG,
                          "touch raw=(%u,%u) reference=(%u,%u) calibrated=(%u,%u) authoritative=%u",
                          (unsigned)sample.raw_x,
                          (unsigned)sample.raw_y,
                          (unsigned)sample.reference_x,
                          (unsigned)sample.reference_y,
                          (unsigned)sample.x,
                          (unsigned)sample.y,
                          sample.calibrated ? 1U : 0U );
            }
        }
    }
    else
    {
        calibration_press_latched = false;
        apply_calibration( &sample );
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
