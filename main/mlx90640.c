/**
    mlx90640.c : RFScout MLX90640 thermal-array implementation
    Description: Acquires raw 32x24 IR frames over the shared expansion I2C bus.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "mlx90640.h"

#include "board.h"
#include "board_i2c.h"

#include "esp_log.h"

#include <stddef.h>
#include <string.h>

#define MLX90640_STATUS_REGISTER 0x8000U
#define MLX90640_CONTROL_REGISTER 0x800DU
#define MLX90640_PIXEL_START_REGISTER 0x0400U
#define MLX90640_WORDS_PER_TRANSFER 32U
#define MLX90640_STATUS_NEW_DATA 0x0008U
#define MLX90640_STATUS_SUBPAGE 0x0001U
#define MLX90640_STATUS_CLEAR_VALUE 0x0030U
#define MLX90640_CONTROL_CHESS_MODE 0x1000U
#define MLX90640_SUBPAGE_MASK_0 0x01U
#define MLX90640_SUBPAGE_MASK_1 0x02U
#define MLX90640_SUBPAGE_MASK_BOTH 0x03U

static const char *TAG = "mlx90640";
static i2c_master_dev_handle_t device;
static bool detected;
static int16_t merged_pixels[ MLX90640_PIXEL_COUNT ];
static uint8_t merged_subpages;

/** @brief Read big-endian 16-bit MLX90640 registers in bounded I2C transfers. */
static esp_err_t read_words( uint16_t a_register,
                             uint16_t *a_words,
                             size_t a_count )
{
    uint8_t bytes[ MLX90640_WORDS_PER_TRANSFER * 2U ];

    while( a_count > 0U )
    {
        const size_t count = ( a_count > MLX90640_WORDS_PER_TRANSFER )
                                 ? MLX90640_WORDS_PER_TRANSFER
                                 : a_count;
        const uint8_t address[ 2 ] =
        {
            (uint8_t)( a_register >> 8 ),
            (uint8_t)a_register
        };

        const esp_err_t result =
            board_i2c_transmit_receive( device,
                                        address,
                                        sizeof( address ),
                                        bytes,
                                        count * 2U );
        if( ESP_OK != result )
        {
            return result;
        }

        for( size_t index = 0U; index < count; ++index )
        {
            a_words[ index ] =
                (uint16_t)( ( (uint16_t)bytes[ index * 2U ] << 8 ) |
                            bytes[ ( index * 2U ) + 1U ] );
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
    const uint8_t data[ 4 ] =
    {
        (uint8_t)( a_register >> 8 ),
        (uint8_t)a_register,
        (uint8_t)( a_value >> 8 ),
        (uint8_t)a_value
    };
    return board_i2c_transmit( device, data, sizeof( data ) );
}

/** @brief Return whether one pixel belongs to the sensor's current subpage. */
static bool pixel_in_subpage( size_t a_index, uint8_t a_subpage, bool a_chess_mode )
{
    const unsigned row = (unsigned)( a_index / MLX90640_WIDTH );
    const unsigned column = (unsigned)( a_index % MLX90640_WIDTH );
    unsigned pattern;

    if( a_chess_mode )
    {
        pattern = ( row + column ) & 1U;
    }
    else
    {
        pattern = row & 1U;
    }

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
    a_frame->center = a_frame->pixels[ ( MLX90640_HEIGHT / 2 ) * MLX90640_WIDTH +
                                        ( MLX90640_WIDTH / 2 ) ];
}

esp_err_t mlx90640_probe( void )
{
    const esp_err_t result = board_i2c_probe( RFSCOUT_BOARD_MLX90640_I2C_ADDRESS );
    if( ESP_OK != result )
    {
        detected = false;
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
        ESP_LOGI( TAG, "MLX90640 detected at I2C address 0x%02X",
                  RFSCOUT_BOARD_MLX90640_I2C_ADDRESS );
    }
    detected = true;
    return ESP_OK;
}

esp_err_t mlx90640_init( void )
{
    merged_subpages = 0U;
    memset( merged_pixels, 0, sizeof( merged_pixels ) );
    return mlx90640_probe();
}

bool mlx90640_is_detected( void )
{
    return detected;
}

esp_err_t mlx90640_read_frame( mlx90640_frame_t *a_frame )
{
    if( NULL == a_frame )
    {
        return ESP_ERR_INVALID_ARG;
    }
    a_frame->frame_valid = false;

    if( !detected || ( NULL == device ) )
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t status;
    esp_err_t result = read_words( MLX90640_STATUS_REGISTER, &status, 1U );
    if( ESP_OK != result )
    {
        detected = false;
        return result;
    }
    if( 0U == ( status & MLX90640_STATUS_NEW_DATA ) )
    {
        return ESP_ERR_NOT_FINISHED;
    }

    const uint8_t subpage = (uint8_t)( status & MLX90640_STATUS_SUBPAGE );

    /* Clear the new-data flag before reading RAM, matching the sensor's frame
       acquisition sequence so a newly completed subpage can be detected. */
    result = write_word( MLX90640_STATUS_REGISTER, MLX90640_STATUS_CLEAR_VALUE );
    if( ESP_OK != result )
    {
        detected = false;
        return result;
    }

    uint16_t control;
    result = read_words( MLX90640_CONTROL_REGISTER, &control, 1U );
    if( ESP_OK != result )
    {
        detected = false;
        return result;
    }

    uint16_t raw_pixels[ MLX90640_PIXEL_COUNT ];
    result = read_words( MLX90640_PIXEL_START_REGISTER,
                         raw_pixels,
                         MLX90640_PIXEL_COUNT );
    if( ESP_OK != result )
    {
        detected = false;
        return result;
    }

    const bool chess_mode = 0U != ( control & MLX90640_CONTROL_CHESS_MODE );
    for( size_t index = 0U; index < MLX90640_PIXEL_COUNT; ++index )
    {
        if( pixel_in_subpage( index, subpage, chess_mode ) )
        {
            merged_pixels[ index ] = (int16_t)raw_pixels[ index ];
        }
    }

    merged_subpages |= ( 0U == subpage )
                           ? MLX90640_SUBPAGE_MASK_0
                           : MLX90640_SUBPAGE_MASK_1;
    if( MLX90640_SUBPAGE_MASK_BOTH != merged_subpages )
    {
        return ESP_ERR_NOT_FINISHED;
    }

    finalize_frame( a_frame );
    return ESP_OK;
}
