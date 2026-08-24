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

#define MLX90640_PIXEL_START_REGISTER 0x0400U
#define MLX90640_WORDS_PER_TRANSFER 32U

static const char *TAG = "mlx90640";
static i2c_master_dev_handle_t device;
static bool detected;

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

esp_err_t mlx90640_init( void )
{
    detected = false;
    device = NULL;

    esp_err_t result = board_i2c_probe( RFSCOUT_BOARD_MLX90640_I2C_ADDRESS );
    if( ESP_OK != result )
    {
        ESP_LOGI( TAG, "MLX90640 not detected at I2C address 0x%02X",
                  RFSCOUT_BOARD_MLX90640_I2C_ADDRESS );
        return result;
    }

    result = board_i2c_add_device( RFSCOUT_BOARD_MLX90640_I2C_ADDRESS,
                                   &device );
    if( ESP_OK != result )
    {
        return result;
    }

    detected = true;
    ESP_LOGI( TAG, "MLX90640 detected at I2C address 0x%02X",
              RFSCOUT_BOARD_MLX90640_I2C_ADDRESS );
    return ESP_OK;
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
    if( !detected || ( NULL == device ) )
    {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = read_words( MLX90640_PIXEL_START_REGISTER,
                                         a_frame->pixels,
                                         MLX90640_PIXEL_COUNT );
    if( ESP_OK != result )
    {
        a_frame->frame_valid = false;
        return result;
    }

    uint16_t minimum = UINT16_MAX;
    uint16_t maximum = 0U;
    for( size_t index = 0U; index < MLX90640_PIXEL_COUNT; ++index )
    {
        const uint16_t value = a_frame->pixels[ index ];
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
    return ESP_OK;
}
