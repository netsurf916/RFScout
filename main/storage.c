/**
    storage.c : RFScout SD-card storage implementation
    Description: Mounts the T-Embed CC1101 Plus SD card on the shared SPI bus.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "storage.h"

#include "board.h"

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define STORAGE_SPI_HOST SPI2_HOST
#define STORAGE_MAX_OPEN_FILES 5

static const char *TAG = "storage";
static sdmmc_card_t *card;
static bool mounted;

esp_err_t storage_init( void )
{
    if( mounted )
    {
        return ESP_OK;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = STORAGE_SPI_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = STORAGE_SPI_HOST;
    slot.gpio_cs = RFSCOUT_BOARD_SD_CS_PIN;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config =
    {
        .format_if_mount_failed = false,
        .max_files = STORAGE_MAX_OPEN_FILES,
        .allocation_unit_size = 16U * 1024U
    };

    const esp_err_t result = esp_vfs_fat_sdspi_mount(
        RFSCOUT_STORAGE_MOUNT_POINT,
        &host,
        &slot,
        &mount_config,
        &card );
    if( ESP_OK != result )
    {
        card = NULL;
        ESP_LOGW( TAG, "SD card unavailable: %s", esp_err_to_name( result ) );
        return result;
    }

    mounted = true;
    ESP_LOGI( TAG,
              "SD card mounted: %llu MiB",
              (unsigned long long)( ( (uint64_t)card->csd.capacity *
                                       card->csd.sector_size ) /
                                     ( 1024ULL * 1024ULL ) ) );
    return ESP_OK;
}

void storage_shutdown( void )
{
    if( !mounted )
    {
        return;
    }

    const esp_err_t result = esp_vfs_fat_sdcard_unmount(
        RFSCOUT_STORAGE_MOUNT_POINT,
        card );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "SD unmount failed: %s", esp_err_to_name( result ) );
    }

    card = NULL;
    mounted = false;
}

bool storage_is_available( void )
{
    return mounted;
}

esp_err_t storage_get_capacity( uint64_t *a_total_bytes,
                                uint64_t *a_free_bytes )
{
    if( !mounted )
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t total_bytes = 0U;
    uint64_t free_bytes = 0U;
    const esp_err_t result = esp_vfs_fat_info(
        RFSCOUT_STORAGE_MOUNT_POINT,
        &total_bytes,
        &free_bytes );
    if( ESP_OK != result )
    {
        return result;
    }

    if( NULL != a_total_bytes )
    {
        *a_total_bytes = total_bytes;
    }
    if( NULL != a_free_bytes )
    {
        *a_free_bytes = free_bytes;
    }

    return ESP_OK;
}
