/**
    board_spi.c : RFScout shared board SPI bus implementation
    Description: Centralizes SPI2 initialization for the LCD, microSD, CC1101, and nRF24 devices.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "board_spi.h"

#include "board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdbool.h>
#include <stdint.h>

#define BOARD_SPI_MAX_TRANSFER_SIZE ( RFSCOUT_BOARD_DISPLAY_WIDTH * 10U * sizeof( uint16_t ) )

static bool initialized;
static spi_device_handle_t manual_guard_device;
static StaticSemaphore_t manual_mutex_control;
static SemaphoreHandle_t manual_mutex;

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


static void ensure_manual_mutex( void )
{
    if( NULL == manual_mutex )
    {
        manual_mutex = xSemaphoreCreateMutexStatic( &manual_mutex_control );
    }
}

static esp_err_t ensure_manual_guard_device( void )
{
    if( NULL != manual_guard_device )
    {
        return ESP_OK;
    }

    spi_device_interface_config_t guard_config = { .clock_speed_hz = 1000000,
                                                   .mode = 0,
                                                   .spics_io_num = -1,
                                                   .queue_size = 1 };
    return spi_bus_add_device( RFSCOUT_BOARD_SPI_HOST, &guard_config, &manual_guard_device );
}

esp_err_t board_spi_manual_acquire( void )
{
    esp_err_t result = board_spi_init();
    if( ESP_OK != result )
    {
        return result;
    }

    ensure_manual_mutex();
    if( NULL == manual_mutex )
    {
        return ESP_ERR_NO_MEM;
    }

    if( pdTRUE != xSemaphoreTake( manual_mutex, portMAX_DELAY ) )
    {
        return ESP_ERR_TIMEOUT;
    }

    result = ensure_manual_guard_device();
    if( ESP_OK == result )
    {
        result = spi_device_acquire_bus( manual_guard_device, portMAX_DELAY );
    }

    if( ESP_OK != result )
    {
        (void)xSemaphoreGive( manual_mutex );
    }
    return result;
}

void board_spi_manual_release( void )
{
    if( NULL == manual_mutex )
    {
        return;
    }

    if( NULL != manual_guard_device )
    {
        spi_device_release_bus( manual_guard_device );
    }
    (void)xSemaphoreGive( manual_mutex );
}
