/**
    storage.c : RFScout SD-card storage implementation
    Description: Mounts the T-Embed CC1101 Plus SD card on the shared SPI bus.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "storage.h"

#include "board.h"
#include "board_spi.h"

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

#define STORAGE_MAX_OPEN_FILES 5
#define STORAGE_PROBE_IO_TIMEOUT_MS 250U

static const char *TAG = "storage";
static sdmmc_card_t *card;
static bool mounted;
static bool media_lost;
static StaticSemaphore_t io_mutex_control;
static SemaphoreHandle_t io_mutex;

static void ensure_io_mutex( void )
{
    if( NULL == io_mutex )
    {
        io_mutex = xSemaphoreCreateMutexStatic( &io_mutex_control );
    }
}

bool storage_io_acquire( uint32_t a_timeout_ms )
{
    ensure_io_mutex();
    if( NULL == io_mutex )
    {
        return false;
    }

    TickType_t timeout = portMAX_DELAY;
    if( RFSCOUT_STORAGE_WAIT_FOREVER != a_timeout_ms )
    {
        timeout = pdMS_TO_TICKS( a_timeout_ms );
        if( ( a_timeout_ms > 0U ) && ( 0U == timeout ) )
        {
            timeout = 1U;
        }
    }
    return pdTRUE == xSemaphoreTake( io_mutex, timeout );
}

void storage_io_release( void )
{
    if( NULL != io_mutex )
    {
        (void)xSemaphoreGive( io_mutex );
    }
}

esp_err_t storage_init( void )
{
    ensure_io_mutex();
    if( NULL == io_mutex )
    {
        return ESP_ERR_NO_MEM;
    }

    if( mounted )
    {
        return media_lost ? ESP_ERR_INVALID_STATE : ESP_OK;
    }

    const esp_err_t bus_result = board_spi_init();
    if( ESP_OK != bus_result )
    {
        ESP_LOGW( TAG, "shared SPI bus unavailable: %s", esp_err_to_name( bus_result ) );
        return bus_result;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = RFSCOUT_BOARD_SPI_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = RFSCOUT_BOARD_SPI_HOST;
    slot.gpio_cs = RFSCOUT_BOARD_SD_CS_PIN;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = { .format_if_mount_failed = false,
                                                            .max_files = STORAGE_MAX_OPEN_FILES,
                                                            .allocation_unit_size = 16U * 1024U };

    const esp_err_t result =
        esp_vfs_fat_sdspi_mount( RFSCOUT_STORAGE_MOUNT_POINT, &host, &slot, &mount_config, &card );
    if( ESP_OK != result )
    {
        card = NULL;
        media_lost = false;
        ESP_LOGW( TAG, "SD card unavailable: %s", esp_err_to_name( result ) );
        return result;
    }

    mounted = true;
    media_lost = false;
    ESP_LOGI( TAG,
              "SD card mounted: %llu MiB",
              (unsigned long long)( ( (uint64_t)card->csd.capacity * card->csd.sector_size ) /
                                    ( 1024ULL * 1024ULL ) ) );
    return ESP_OK;
}

void storage_shutdown( void )
{
    if( !mounted )
    {
        return;
    }

    /* A detected physical removal may leave platform services holding VFS file objects. Do not
       tear down that mount from an application-level dependency failure; the next reset/deep-sleep
       will discard it, while ordinary orderly shutdown still unmounts healthy media. */
    if( media_lost )
    {
        ESP_LOGW( TAG, "SD media was removed; keeping stale VFS mount until platform reset" );
        return;
    }

    const esp_err_t result = esp_vfs_fat_sdcard_unmount( RFSCOUT_STORAGE_MOUNT_POINT, card );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "SD unmount failed: %s", esp_err_to_name( result ) );
    }

    card = NULL;
    mounted = false;
    media_lost = false;
}

bool storage_is_available( void )
{
    return mounted && !media_lost;
}

esp_err_t storage_probe_card( void )
{
    if( !mounted || NULL == card || media_lost )
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* sdmmc_get_status() bypasses the filesystem layer. Serialize it with asynchronous filesystem
       writers so its raw command cannot land in the middle of a multi-command FAT transaction. */
    if( !storage_io_acquire( STORAGE_PROBE_IO_TIMEOUT_MS ) )
    {
        return ESP_ERR_TIMEOUT;
    }

    if( !mounted || NULL == card || media_lost )
    {
        storage_io_release();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = sdmmc_get_status( card );
    if( ESP_OK != result )
    {
        media_lost = true;
        ESP_LOGW( TAG,
                  "SD card presence probe failed and media is now latched unavailable: %s",
                  esp_err_to_name( result ) );
    }
    storage_io_release();
    return result;
}

esp_err_t storage_get_capacity( uint64_t *a_total_bytes, uint64_t *a_free_bytes )
{
    if( !mounted || media_lost )
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t total_bytes = 0U;
    uint64_t free_bytes = 0U;
    const esp_err_t result =
        esp_vfs_fat_info( RFSCOUT_STORAGE_MOUNT_POINT, &total_bytes, &free_bytes );
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
