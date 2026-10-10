/**
    storage.c : RFScout SD-card storage implementation
    Description: Mounts and owns the runtime board SD card on the shared SPI bus.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "storage.h"
#include "storage_internal.h"

#include "board.h"
#include "board_spi.h"
#include "event.h"

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

#define STORAGE_PROBE_IO_TIMEOUT_MS 250U
#define STORAGE_FAILURE_COOLDOWN_MS 3000U

#define STORAGE_EVENT_MOUNTED ( 1U << 0 )
#define STORAGE_EVENT_MOUNT_FAILED ( 1U << 1 )
#define STORAGE_EVENT_LOST ( 1U << 2 )
#define STORAGE_EVENT_RECOVERED ( 1U << 3 )

static const char *TAG = "storage";
static sdmmc_card_t *card;
static bool mounted;
static bool media_lost;
static StaticSemaphore_t io_mutex_control;
static SemaphoreHandle_t io_mutex;
/* Access to this 64-bit timestamp must be synchronized on 32-bit ESP targets.
   An expired deadline may remain cached: monotonic time cannot reactivate it. */
static portMUX_TYPE retry_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t retry_after_us;
/* Latch transitions only; no SD I/O, task, timer, or background recovery. */
static uint32_t pending_events;
static bool mount_attempt_recorded;

static void storage_record_mount_result( esp_err_t a_result )
{
    if( ESP_OK == a_result )
    {
        (void)__atomic_fetch_or( &pending_events,
                                 mount_attempt_recorded ? STORAGE_EVENT_RECOVERED
                                                        : STORAGE_EVENT_MOUNTED,
                                 __ATOMIC_RELEASE );
        mount_attempt_recorded = true;
    }
    else if( !mount_attempt_recorded )
    {
        (void)__atomic_fetch_or( &pending_events, STORAGE_EVENT_MOUNT_FAILED, __ATOMIC_RELEASE );
        mount_attempt_recorded = true;
    }
}

/* Synchronous subscribers are called only after leaving SPI/storage arbitration. */
void storage_dispatch_events( void )
{
    const uint32_t events = __atomic_exchange_n( &pending_events, 0U, __ATOMIC_ACQ_REL );
    if( 0U != ( events & STORAGE_EVENT_MOUNTED ) )
        rfscout_event_emit( RFSCOUT_EVENT_STORAGE_MOUNTED, RFSCOUT_EVENT_SOURCE_SYSTEM, ESP_OK );
    if( 0U != ( events & STORAGE_EVENT_MOUNT_FAILED ) )
        rfscout_event_emit(
            RFSCOUT_EVENT_STORAGE_MOUNT_FAILED, RFSCOUT_EVENT_SOURCE_SYSTEM, ESP_ERR_NOT_FOUND );
    if( 0U != ( events & STORAGE_EVENT_LOST ) )
        rfscout_event_emit(
            RFSCOUT_EVENT_STORAGE_LOST, RFSCOUT_EVENT_SOURCE_SYSTEM, ESP_ERR_NOT_FOUND );
    if( 0U != ( events & STORAGE_EVENT_RECOVERED ) )
        rfscout_event_emit( RFSCOUT_EVENT_STORAGE_RECOVERED, RFSCOUT_EVENT_SOURCE_SYSTEM, ESP_OK );
}

bool storage_retry_delayed( void )
{
    portENTER_CRITICAL( &retry_lock );
    const int64_t deadline = retry_after_us;
    portEXIT_CRITICAL( &retry_lock );
    return esp_timer_get_time() < deadline;
}

static void storage_start_cooldown( void )
{
    portENTER_CRITICAL( &retry_lock );
    retry_after_us = esp_timer_get_time() + (int64_t)STORAGE_FAILURE_COOLDOWN_MS * 1000LL;
    portEXIT_CRITICAL( &retry_lock );
}

void storage_note_media_failure( void )
{
    /* A failed SD transaction invalidates the mounted card, unlike bus contention. */
    if( mounted && !media_lost )
    {
        media_lost = true;
        (void)__atomic_fetch_or( &pending_events, STORAGE_EVENT_LOST, __ATOMIC_RELEASE );
    }
    storage_start_cooldown();
}

static void ensure_io_mutex( void )
{
    if( NULL == io_mutex )
    {
        io_mutex = xSemaphoreCreateRecursiveMutexStatic( &io_mutex_control );
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
    if( pdTRUE != xSemaphoreTakeRecursive( io_mutex, timeout ) )
    {
        return false;
    }

    /* Every SD transaction also holds the board SPI lock so it cannot contend with the LCD or
       radios inside the shared-bus driver. Lock order is always storage I/O, then board SPI. */
    const uint32_t bus_timeout_ms = ( RFSCOUT_STORAGE_WAIT_FOREVER == a_timeout_ms )
                                        ? RFSCOUT_BOARD_SPI_WAIT_FOREVER
                                        : a_timeout_ms;
    if( !board_spi_lock( bus_timeout_ms ) )
    {
        (void)xSemaphoreGiveRecursive( io_mutex );
        return false;
    }
    return true;
}

void storage_io_release( void )
{
    if( NULL != io_mutex )
    {
        board_spi_unlock();
        (void)xSemaphoreGiveRecursive( io_mutex );
    }
}

esp_err_t storage_init( void )
{
    if( storage_retry_delayed() )
    {
        return ESP_ERR_NOT_FOUND;
    }
    ensure_io_mutex();
    if( NULL == io_mutex )
    {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t bus_result = board_spi_init();
    if( ESP_OK != bus_result )
    {
        storage_start_cooldown();
        storage_record_mount_result( bus_result );
        ESP_LOGW( TAG, "shared SPI bus unavailable: %s", esp_err_to_name( bus_result ) );
        return bus_result;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = RFSCOUT_BOARD_SPI_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = RFSCOUT_BOARD_SPI_HOST;
    slot.gpio_cs = RFSCOUT_BOARD_SD_CS_PIN;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = { .format_if_mount_failed = false,
                                                            .max_files =
                                                                RFSCOUT_STORAGE_MAX_OPEN_FILES,
                                                            .allocation_unit_size = 16U * 1024U };

    /* Mounting talks to the card; hot-plug probes can run it while other SPI clients are active. */
    if( !storage_io_acquire( STORAGE_PROBE_IO_TIMEOUT_MS ) )
    {
        /* SPI contention is not evidence that the card failed. */
        return ESP_ERR_TIMEOUT;
    }
    if( storage_retry_delayed() )
    {
        storage_io_release();
        return ESP_ERR_NOT_FOUND;
    }
    if( mounted )
    {
        const esp_err_t existing = media_lost ? ESP_ERR_INVALID_STATE : ESP_OK;
        storage_io_release();
        return existing;
    }
    const esp_err_t result =
        esp_vfs_fat_sdspi_mount( RFSCOUT_STORAGE_MOUNT_POINT, &host, &slot, &mount_config, &card );
    if( ESP_OK != result )
    {
        card = NULL;
        storage_start_cooldown();
        storage_record_mount_result( result );
        storage_io_release();
        ESP_LOGW( TAG, "SD card unavailable: %s", esp_err_to_name( result ) );
        return result;
    }

    mounted = true;
    media_lost = false;
    storage_record_mount_result( ESP_OK );
    ESP_LOGI( TAG,
              "SD card mounted: %llu MiB",
              (unsigned long long)( ( (uint64_t)card->csd.capacity * card->csd.sector_size ) /
                                    ( 1024ULL * 1024ULL ) ) );
    storage_io_release();
    return ESP_OK;
}

esp_err_t storage_recover_lost_media( void )
{
    /* The caller owns storage arbitration and has invalidated the media slots. */
    if( !mounted || !media_lost )
    {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = esp_vfs_fat_sdcard_unmount( RFSCOUT_STORAGE_MOUNT_POINT, card );
    if( ESP_OK != result )
    {
        storage_start_cooldown();
        ESP_LOGW( TAG, "SD unmount after media loss failed: %s", esp_err_to_name( result ) );
        return result;
    }
    card = NULL;
    mounted = false;
    media_lost = false;
    return storage_init();
}

bool storage_is_mounted( void )
{
    return mounted;
}

bool storage_is_available( void )
{
    return mounted && !media_lost && !storage_retry_delayed();
}

esp_err_t storage_probe_card( void )
{
    if( storage_retry_delayed() )
    {
        return ESP_ERR_NOT_FOUND;
    }
    if( !mounted || NULL == card || media_lost )
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* sdmmc_get_status() bypasses FatFs. Serialize the raw command with bounded filesystem
       transactions so it cannot land in the middle of a multi-command filesystem operation. */
    if( !storage_io_acquire( STORAGE_PROBE_IO_TIMEOUT_MS ) )
    {
        return ESP_OK;
    }

    if( !mounted || NULL == card || media_lost )
    {
        storage_io_release();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = sdmmc_get_status( card );
    if( ESP_OK != result )
    {
        storage_note_media_failure();
        ESP_LOGW( TAG,
                  "SD card presence probe failed and media is now latched unavailable: %s",
                  esp_err_to_name( result ) );
    }
    storage_io_release();

    return result;
}

esp_err_t storage_get_capacity( uint64_t *a_total_bytes, uint64_t *a_free_bytes )
{
    if( !storage_is_available() )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( !storage_io_acquire( STORAGE_PROBE_IO_TIMEOUT_MS ) )
    {
        return ESP_ERR_NO_MEM;
    }

    uint64_t total_bytes = 0U;
    uint64_t free_bytes = 0U;
    const esp_err_t result =
        esp_vfs_fat_info( RFSCOUT_STORAGE_MOUNT_POINT, &total_bytes, &free_bytes );
    if( ESP_FAIL == result )
    {
        storage_note_media_failure();
    }
    storage_io_release();
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
