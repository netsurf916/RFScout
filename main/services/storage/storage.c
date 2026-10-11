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
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

#if FF_FS_NORTC != 0
#error RFScout requires FatFs timestamps from the ESP-IDF system clock
#endif

#define STORAGE_PROBE_IO_TIMEOUT_MS 250U
#define STORAGE_FAILURE_COOLDOWN_MS 3000U

#define STORAGE_EVENT_QUEUE_CAPACITY 8U

static const char *TAG = "storage";
static sdmmc_card_t *card;
static bool mounted;
static bool media_lost;
static StaticSemaphore_t io_mutex_control;
static SemaphoreHandle_t io_mutex;
/* The 64-bit monotonic retry deadline is synchronized on 32-bit ESP targets. */
static portMUX_TYPE retry_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t retry_after_us;

typedef struct
{
    rfscout_event_type_t type;
    int32_t value;
} storage_transition_t;

/* Queue real state transitions in order, without performing media I/O or notifying
   synchronous observers while holding the storage/SPI locks. */
static storage_transition_t pending_events[ STORAGE_EVENT_QUEUE_CAPACITY ];
static size_t pending_event_count;
static portMUX_TYPE event_lock = portMUX_INITIALIZER_UNLOCKED;
static bool mount_attempt_recorded;
static bool recovery_confirmation_pending;

static void storage_queue_event( rfscout_event_type_t a_type, esp_err_t a_result )
{
    portENTER_CRITICAL( &event_lock );
    if( pending_event_count == STORAGE_EVENT_QUEUE_CAPACITY )
    {
        /* A bounded ring of transitions: retain the newest event on overflow. */
        for( size_t i = 1U; i < pending_event_count; ++i )
            pending_events[ i - 1U ] = pending_events[ i ];
        --pending_event_count;
    }
    pending_events[ pending_event_count++ ] =
        ( storage_transition_t ){ .type = a_type, .value = (int32_t)a_result };
    portEXIT_CRITICAL( &event_lock );
}

static void storage_record_mount_result( esp_err_t a_result )
{
    if( ESP_OK == a_result )
    {
        if( mount_attempt_recorded )
        {
            /* Only publish recovery after a real post-mount card status probe. */
            recovery_confirmation_pending = true;
        }
        else
        {
            storage_queue_event( RFSCOUT_EVENT_STORAGE_MOUNTED, ESP_OK );
        }
        mount_attempt_recorded = true;
    }
    else if( !mount_attempt_recorded )
    {
        storage_queue_event( RFSCOUT_EVENT_STORAGE_MOUNT_FAILED, a_result );
        mount_attempt_recorded = true;
    }
}

void storage_dispatch_events( void )
{
    storage_transition_t events[ STORAGE_EVENT_QUEUE_CAPACITY ];
    size_t count;
    portENTER_CRITICAL( &event_lock );
    count = pending_event_count;
    for( size_t i = 0U; i < count; ++i )
        events[ i ] = pending_events[ i ];
    pending_event_count = 0U;
    portEXIT_CRITICAL( &event_lock );

    for( size_t i = 0U; i < count; ++i )
        rfscout_event_emit( events[ i ].type, RFSCOUT_EVENT_SOURCE_SYSTEM, events[ i ].value );
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
        recovery_confirmation_pending = false;
        storage_queue_event( RFSCOUT_EVENT_STORAGE_LOST, ESP_ERR_NOT_FOUND );
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
    if( ESP_OK == result && recovery_confirmation_pending )
    {
        recovery_confirmation_pending = false;
        storage_queue_event( RFSCOUT_EVENT_STORAGE_RECOVERED, ESP_OK );
    }
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
