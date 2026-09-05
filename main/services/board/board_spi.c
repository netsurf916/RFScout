/**
    board_spi.c : RFScout shared board SPI bus implementation
    Description: Centralizes SPI2 initialization for the LCD, microSD, CC1101, and nRF24 devices.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "board_spi.h"

#include "board.h"

#include <stdbool.h>
#include <stdint.h>

#define BOARD_SPI_MAX_TRANSFER_SIZE ( RFSCOUT_BOARD_DISPLAY_WIDTH * 10U * sizeof( uint16_t ) )

static bool initialized;

esp_err_t board_spi_init( void )
{
    if( initialized )
    {
        return ESP_OK;
    }

    spi_bus_config_t bus = { .mosi_io_num = RFSCOUT_BOARD_SPI_MOSI_PIN,
                             .miso_io_num = RFSCOUT_BOARD_SPI_MISO_PIN,
                             .sclk_io_num = RFSCOUT_BOARD_SPI_SCLK_PIN,
                             .quadwp_io_num = -1,
                             .quadhd_io_num = -1,
                             .max_transfer_sz = BOARD_SPI_MAX_TRANSFER_SIZE };
    const esp_err_t result = spi_bus_initialize( RFSCOUT_BOARD_SPI_HOST, &bus, SPI_DMA_CH_AUTO );
    if( ESP_OK == result )
    {
        initialized = true;
    }
    return result;
}
