/**
    board_i2c.c : Shared RFScout board I2C bus
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "board_i2c.h"

#include "board.h"

#include "esp_log.h"

#define BOARD_I2C_FREQUENCY_HZ 400000U
#define BOARD_I2C_PROBE_TIMEOUT_MS 50
#define BOARD_I2C_TRANSFER_TIMEOUT_MS 100

static const char *TAG = "board_i2c";
static i2c_master_bus_handle_t bus_handle;

esp_err_t board_i2c_init( void )
{
    if( NULL != bus_handle )
    {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = { .i2c_port = I2C_NUM_0,
                                           .sda_io_num = RFSCOUT_BOARD_I2C_SDA_PIN,
                                           .scl_io_num = RFSCOUT_BOARD_I2C_SCL_PIN,
                                           .clk_source = I2C_CLK_SRC_DEFAULT,
                                           .glitch_ignore_cnt = 7,
                                           .flags.enable_internal_pullup = true };

    const esp_err_t result = i2c_new_master_bus( &bus_config, &bus_handle );
    if( ESP_OK == result )
    {
        ESP_LOGI( TAG,
                  "shared board I2C initialized on SDA=%d SCL=%d",
                  RFSCOUT_BOARD_I2C_SDA_PIN,
                  RFSCOUT_BOARD_I2C_SCL_PIN );
    }
    return result;
}

i2c_master_bus_handle_t board_i2c_handle( void )
{
    return ESP_OK == board_i2c_init() ? bus_handle : NULL;
}

esp_err_t board_i2c_probe( uint16_t a_address )
{
    const esp_err_t result = board_i2c_init();
    if( ESP_OK != result )
    {
        return result;
    }

    return i2c_master_probe( bus_handle, a_address, BOARD_I2C_PROBE_TIMEOUT_MS );
}

esp_err_t board_i2c_add_device( uint16_t a_address, i2c_master_dev_handle_t *a_device )
{
    if( NULL == a_device )
    {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result = board_i2c_init();
    if( ESP_OK != result )
    {
        return result;
    }

    i2c_device_config_t device_config = { .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                          .device_address = a_address,
                                          .scl_speed_hz = BOARD_I2C_FREQUENCY_HZ };

    return i2c_master_bus_add_device( bus_handle, &device_config, a_device );
}

esp_err_t board_i2c_transmit_receive( i2c_master_dev_handle_t a_device,
                                      const uint8_t *a_write,
                                      size_t a_write_length,
                                      uint8_t *a_read,
                                      size_t a_read_length )
{
    if( ( NULL == a_device ) || ( NULL == a_write ) || ( 0U == a_write_length ) ||
        ( NULL == a_read ) || ( 0U == a_read_length ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(
        a_device, a_write, a_write_length, a_read, a_read_length, BOARD_I2C_TRANSFER_TIMEOUT_MS );
}

esp_err_t
board_i2c_transmit( i2c_master_dev_handle_t a_device, const uint8_t *a_data, size_t a_length )
{
    if( ( NULL == a_device ) || ( NULL == a_data ) || ( 0U == a_length ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit( a_device, a_data, a_length, BOARD_I2C_TRANSFER_TIMEOUT_MS );
}
