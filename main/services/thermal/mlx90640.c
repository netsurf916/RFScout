/**
    mlx90640.c : RFScout MLX90640 thermal-array implementation
    Description: Acquires stable 32x24 IR frames over the shared expansion I2C bus.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "mlx90640.h"

#include "board.h"
#include "board_i2c.h"

#include "esp_log.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define MLX90640_STATUS_REGISTER 0x8000U
#define MLX90640_CONTROL_REGISTER 0x800DU
#define MLX90640_PIXEL_START_REGISTER 0x0400U
#define MLX90640_WORDS_PER_TRANSFER 32U
#define MLX90640_TRANSFERS_PER_POLL 4U
#define MLX90640_READ_FAILURE_RETRY_COUNT 3U
#define MLX90640_STATUS_NEW_DATA 0x0008U
#define MLX90640_STATUS_SUBPAGE 0x0001U
#define MLX90640_STATUS_CLEAR_VALUE 0x0030U
#define MLX90640_CONTROL_CHESS_MODE 0x1000U
#define MLX90640_CONTROL_REFRESH_MASK 0x0380U
#define MLX90640_CONTROL_REFRESH_8HZ 0x0200U
#define MLX90640_SUBPAGE_MASK_0 0x01U
#define MLX90640_SUBPAGE_MASK_1 0x02U
#define MLX90640_SUBPAGE_MASK_BOTH 0x03U

static const char *TAG = "mlx90640";
static i2c_master_dev_handle_t device;
static bool detected;
static int16_t merged_pixels[ MLX90640_PIXEL_COUNT ];
static uint8_t merged_subpages;
static unsigned consecutive_read_failures;
static size_t read_offset;
static uint8_t current_subpage;
static bool current_chess_mode;
static bool reading_pixels;
static bool refresh_configured;

/** @brief Read big-endian 16-bit MLX90640 registers in bounded I2C transfers. */
static esp_err_t read_words( uint16_t a_register, uint16_t *a_words, size_t a_count )
{
    uint8_t bytes[ MLX90640_WORDS_PER_TRANSFER * 2U ];

    while( a_count > 0U )
    {
        const size_t count =
            ( a_count > MLX90640_WORDS_PER_TRANSFER ) ? MLX90640_WORDS_PER_TRANSFER : a_count;
        const uint8_t address[ 2 ] = { (uint8_t)( a_register >> 8 ), (uint8_t)a_register };

        const esp_err_t result =
            board_i2c_transmit_receive( device, address, sizeof( address ), bytes, count * 2U );
        if( ESP_OK != result )
        {
            return result;
        }

        for( size_t index = 0U; index < count; ++index )
        {
            a_words[ index ] =
                (uint16_t)( ( (uint16_t)bytes[ index * 2U ] << 8 ) | bytes[ ( index * 2U ) + 1U ] );
        }

        a_register = (uint16_t)( a_register + count );
        a_words += count;
        a_count -= count;
    }

    return ESP_OK;
}

/** @brief Write one big-endian 16-bit MLX90640 register. */
static esp_err_t write_word( uint16_t a_register, uint16_t a_value )
{
    const uint8_t data[ 4 ] = { (uint8_t)( a_register >> 8 ),
                                (uint8_t)a_register,
                                (uint8_t)( a_value >> 8 ),
                                (uint8_t)a_value };
    return board_i2c_transmit( device, data, sizeof( data ) );
}

/** @brief Return whether one pixel belongs to the sensor's current subpage. */
static bool pixel_in_subpage( size_t a_index, uint8_t a_subpage, bool a_chess_mode )
{
    const unsigned row = (unsigned)( a_index / MLX90640_WIDTH );
    const unsigned column = (unsigned)( a_index % MLX90640_WIDTH );
    const unsigned pattern = a_chess_mode ? ( ( row & 1U ) ^ ( column & 1U ) ) : ( row & 1U );
    return pattern == (unsigned)a_subpage;
}

/** @brief Recompute display metadata after both alternating subpages are present. */
static void finalize_frame( mlx90640_frame_t *a_frame )
{
    int16_t minimum = INT16_MAX;
    int16_t maximum = INT16_MIN;

    memcpy( a_frame->pixels, merged_pixels, sizeof( merged_pixels ) );
    for( size_t index = 0U; index < MLX90640_PIXEL_COUNT; ++index )
    {
        const int16_t value = a_frame->pixels[ index ];
        if( value < minimum )
        {
            minimum = value;
        }
        if( value > maximum )
        {
            maximum = value;
        }
    }

    a_frame->detected = true;
    a_frame->frame_valid = true;
    a_frame->minimum = minimum;
    a_frame->maximum = maximum;
    a_frame->center =
        a_frame->pixels[ ( MLX90640_HEIGHT / 2 ) * MLX90640_WIDTH + ( MLX90640_WIDTH / 2 ) ];
}

esp_err_t mlx90640_probe( void )
{
    const esp_err_t result = board_i2c_probe( RFSCOUT_BOARD_MLX90640_I2C_ADDRESS );
    if( ESP_OK != result )
    {
        if( ESP_ERR_INVALID_STATE != result )
        {
            detected = false;
            refresh_configured = false;
        }
        return result;
    }

    if( NULL == device )
    {
        const esp_err_t attach =
            board_i2c_add_device( RFSCOUT_BOARD_MLX90640_I2C_ADDRESS, &device );
        if( ESP_OK != attach )
        {
            detected = false;
            return attach;
        }
    }

    if( !detected )
    {
        ESP_LOGI(
            TAG, "MLX90640 detected at I2C address 0x%02X", RFSCOUT_BOARD_MLX90640_I2C_ADDRESS );
    }
    if( !detected )
    {
        consecutive_read_failures = 0U;
        mlx90640_discard_partial_frame();
    }
    detected = true;
    return ESP_OK;
}

void mlx90640_discard_partial_frame( void )
{
    reading_pixels = false;
    read_offset = 0U;
    merged_subpages = 0U;
}

esp_err_t mlx90640_init( void )
{
    merged_subpages = 0U;
    memset( merged_pixels, 0, sizeof( merged_pixels ) );
    consecutive_read_failures = 0U;
    refresh_configured = false;
    read_offset = 0U;
    reading_pixels = false;
    return mlx90640_probe();
}

bool mlx90640_is_detected( void )
{
    return detected;
}

static esp_err_t read_failed( esp_err_t a_result )
{
    merged_subpages = 0U;
    read_offset = 0U;
    reading_pixels = false;
    if( ++consecutive_read_failures >= MLX90640_READ_FAILURE_RETRY_COUNT )
    {
        detected = false;
        refresh_configured = false;
    }
    return a_result;
}

esp_err_t mlx90640_read_frame( mlx90640_frame_t *a_frame )
{
    if( NULL == a_frame )
    {
        return ESP_ERR_INVALID_ARG;
    }
    a_frame->frame_valid = false;
    if( !board_i2c_ready() )
    {
        mlx90640_discard_partial_frame();
        return ESP_ERR_NOT_FINISHED;
    }
    if( !detected || ( NULL == device ) )
    {
        return ESP_ERR_INVALID_STATE;
    }

    if( !refresh_configured )
    {
        uint16_t control;
        esp_err_t result = read_words( MLX90640_CONTROL_REGISTER, &control, 1U );
        if( ESP_OK != result )
        {
            return read_failed( result );
        }
        const uint16_t configured =
            (uint16_t)( ( control & (uint16_t)~MLX90640_CONTROL_REFRESH_MASK ) |
                        MLX90640_CONTROL_REFRESH_8HZ );
        if( configured != control )
        {
            result = write_word( MLX90640_CONTROL_REGISTER, configured );
            if( ESP_OK != result )
            {
                return read_failed( result );
            }
        }
        ESP_LOGI( TAG,
                  "MLX90640 refresh control: 0x%04X -> 0x%04X (8 Hz subpages)",
                  control,
                  configured );
        refresh_configured = true;
        return ESP_ERR_NOT_FINISHED;
    }

    if( !reading_pixels )
    {
        uint16_t status;
        esp_err_t result = read_words( MLX90640_STATUS_REGISTER, &status, 1U );
        if( ESP_OK != result )
        {
            return read_failed( result );
        }
        if( 0U == ( status & MLX90640_STATUS_NEW_DATA ) )
        {
            return ESP_ERR_NOT_FINISHED;
        }

        /* Latch the subpage before clearing ready, then consume pixel words
           over multiple runtime polls so other I2C clients get CPU time. */
        current_subpage = (uint8_t)( status & MLX90640_STATUS_SUBPAGE );
        result = write_word( MLX90640_STATUS_REGISTER, MLX90640_STATUS_CLEAR_VALUE );
        if( ESP_OK != result )
        {
            return read_failed( result );
        }
        uint16_t control;
        result = read_words( MLX90640_CONTROL_REGISTER, &control, 1U );
        if( ESP_OK != result )
        {
            return read_failed( result );
        }
        current_chess_mode = 0U != ( control & MLX90640_CONTROL_CHESS_MODE );
        read_offset = 0U;
        reading_pixels = true;
        return ESP_ERR_NOT_FINISHED;
    }

    /* Four short transactions per poll: no single long bus operation,
       but only six polling opportunities per 768-word subpage. */
    for( unsigned transfer = 0U;
         transfer < MLX90640_TRANSFERS_PER_POLL && read_offset < MLX90640_PIXEL_COUNT;
         ++transfer )
    {
        uint16_t words[ MLX90640_WORDS_PER_TRANSFER ];
        const size_t remaining = MLX90640_PIXEL_COUNT - read_offset;
        const size_t count =
            remaining < MLX90640_WORDS_PER_TRANSFER ? remaining : MLX90640_WORDS_PER_TRANSFER;
        const esp_err_t result =
            read_words( MLX90640_PIXEL_START_REGISTER + (uint16_t)read_offset, words, count );
        if( ESP_OK != result )
        {
            return read_failed( result );
        }

        for( size_t index = 0U; index < count; ++index )
        {
            const size_t pixel = read_offset + index;
            if( pixel_in_subpage( pixel, current_subpage, current_chess_mode ) )
            {
                merged_pixels[ pixel ] = (int16_t)words[ index ];
            }
        }
        read_offset += count;
    }
    if( read_offset < MLX90640_PIXEL_COUNT )
    {
        return ESP_ERR_NOT_FINISHED;
    }

    reading_pixels = false;
    read_offset = 0U;
    consecutive_read_failures = 0U;
    merged_subpages |=
        ( 0U == current_subpage ) ? MLX90640_SUBPAGE_MASK_0 : MLX90640_SUBPAGE_MASK_1;
    if( MLX90640_SUBPAGE_MASK_BOTH != merged_subpages )
    {
        return ESP_ERR_NOT_FINISHED;
    }

    finalize_frame( a_frame );
    merged_subpages = 0U;
    return ESP_OK;
}
