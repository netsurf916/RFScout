/**
    breadcrumbs.c : RFScout GPS breadcrumb recorder
    Description: Records durable GPS-UTC location breadcrumbs to a bounded SD journal.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "breadcrumbs.h"

#include "gps.h"
#include "device_config.h"
#include "geo.h"
#include "event.h"
#include "media_journal.h"
#include "persistent_store.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Disable after consecutive storage-write failures; success resets the streak. */
#define BREADCRUMB_FAILURE_LIMIT 10U
#define BREADCRUMB_PERSIST_RETRY_LIMIT 3U
#define BREADCRUMB_PERSIST_RETRY_US 30000000LL
#define BREADCRUMB_COLLECTION "breadcrumbs"
#define BREADCRUMB_SEGMENT_BYTES ( 64U * 1024U )
#define BREADCRUMB_SETTINGS_KEY "breadcrumbs"
#define BREADCRUMB_SETTINGS_VERSION 1U
#define BREADCRUMB_SETTINGS_SIZE 4U
/* Timestamps have whole-second precision; never write multiple records per UTC second. */
#define BREADCRUMB_MIN_INTERVAL_US 1000000LL
#define BREADCRUMB_QUEUE_DEPTH 4U
#define BREADCRUMB_TASK_STACK 4096U
#define BREADCRUMB_TASK_PRIORITY 2U
#define BREADCRUMB_WORKER_POLL_MS 500U
#define BREADCRUMB_SHUTDOWN_POLL_MS 20U

typedef struct
{
    time_t utc_epoch;
    double latitude_deg;
    double longitude_deg;
    float speed_mph;
    bool speed_valid;
    float filtered_speed_mph;
    int current_rate_ms;
    int target_rate_ms;
    bool rate_valid;
} breadcrumb_record_t;

static const char *TAG = "breadcrumbs";
static StaticQueue_t queue_control;
static uint8_t queue_storage[ BREADCRUMB_QUEUE_DEPTH * sizeof( breadcrumb_record_t ) ];
static QueueHandle_t queue;
static TaskHandle_t worker_task;
static int64_t last_capture_us;
static geo_movement_gate_t movement_gate;
static const uint16_t FILE_LIMIT_OPTIONS[] = { 24U, 72U, 144U, 288U, 1008U };
static time_t last_queued_utc;
static volatile bool trim_requested;
static volatile bool shutdown_requested;
static volatile bool worker_quiesced;
static bool settings_loaded;
static uint8_t consecutive_write_failures;
static uint8_t disable_persist_attempts;
static int64_t disable_persist_retry_us;
static bool disable_persist_pending;
static volatile bool enabled;
static volatile uint16_t file_limit = RFSCOUT_BREADCRUMB_FILE_LIMIT_DEFAULT;
static media_journal_t journal;
static StaticSemaphore_t write_mutex_control;
static SemaphoreHandle_t write_mutex;

static esp_err_t persist_settings( bool a_enabled, uint16_t a_file_limit )
{
    const uint8_t record[ BREADCRUMB_SETTINGS_SIZE ] = {
        BREADCRUMB_SETTINGS_VERSION,
        a_enabled ? 1U : 0U,
        (uint8_t)( a_file_limit & 0xffU ),
        (uint8_t)( a_file_limit >> 8U ),
    };
    return system_store_write( BREADCRUMB_SETTINGS_KEY, record, sizeof( record ) );
}

static uint16_t stored_file_limit( const uint8_t *a_record )
{
    return (uint16_t)a_record[ 2 ] | ( (uint16_t)a_record[ 3 ] << 8U );
}

static bool settings_valid( const void *a_record )
{
    const uint8_t *record = a_record;
    const uint16_t limit = stored_file_limit( record );
    return BREADCRUMB_SETTINGS_VERSION == record[ 0 ] && record[ 1 ] <= 1U && 0U != limit &&
           limit <= RFSCOUT_BREADCRUMB_FILE_LIMIT_MAX;
}

static void load_settings( void )
{
    if( settings_loaded )
    {
        return;
    }
    settings_loaded = true;
    enabled = false;
    file_limit = RFSCOUT_BREADCRUMB_FILE_LIMIT_DEFAULT;

    uint8_t record[ BREADCRUMB_SETTINGS_SIZE ];
    if( ESP_OK ==
        system_store_load( BREADCRUMB_SETTINGS_KEY, record, sizeof( record ), settings_valid ) )
    {
        enabled = 0U != record[ 1 ];
        file_limit = stored_file_limit( record );
    }
}

static void request_trim( void )
{
    if( NULL != worker_task )
    {
        trim_requested = true;
    }
}

static int format_record( void *a_context, size_t a_index, char *a_line, size_t a_capacity )
{
    (void)a_index;
    const breadcrumb_record_t *record = a_context;
    int length =
        media_journal_printf( a_line,
                              a_capacity,
                              0,
                              "{\"type\":\"breadcrumb\",\"utc\":%lld,\"lat\":%.7f,\"lon\":%.7f",
                              (long long)record->utc_epoch,
                              record->latitude_deg,
                              record->longitude_deg );
    if( record->speed_valid )
    {
        length = media_journal_printf(
            a_line, a_capacity, length, ",\"speed_mph\":%.2f", (double)record->speed_mph );
    }
    if( record->rate_valid )
    {
        length = media_journal_printf(
            a_line,
            a_capacity,
            length,
            ",\"filtered_speed_mph\":%.2f,\"rate_ms\":%d,\"target_rate_ms\":%d",
            (double)record->filtered_speed_mph,
            record->current_rate_ms,
            record->target_rate_ms );
    }
    return media_journal_printf( a_line, a_capacity, length, "}" );
}

/** @brief Append one record; caller holds write_mutex so disable cannot race a write. */
static bool write_record_locked( const breadcrumb_record_t *a_record )
{
    const esp_err_t result = media_journal_append( &journal, format_record, (void *)a_record, 1U );
    if( ESP_OK != result )
    {
        ESP_LOGE( TAG, "breadcrumb write failed: %s", esp_err_to_name( result ) );
    }
    return ESP_OK == result;
}

static void trim_files( void )
{
    media_journal_set_max_segments( &journal, file_limit );
    if( media_store_available() )
    {
        (void)media_journal_trim( &journal );
    }
}

static void write_record( const breadcrumb_record_t *a_record )
{
    if( NULL == a_record || NULL == write_mutex )
    {
        return;
    }
    (void)xSemaphoreTake( write_mutex, portMAX_DELAY );
    if( enabled )
    {
        const bool success = write_record_locked( a_record );
        if( success )
        {
            consecutive_write_failures = 0U;
        }
        else if( consecutive_write_failures < BREADCRUMB_FAILURE_LIMIT )
        {
            ++consecutive_write_failures;
            if( BREADCRUMB_FAILURE_LIMIT == consecutive_write_failures )
            {
                ESP_LOGW( TAG,
                          "disabling breadcrumbs after %u consecutive write failures",
                          (unsigned)consecutive_write_failures );
                /* Contain this session even if internal flash persistence fails. */
                enabled = false;
                (void)xQueueReset( queue );
                rfscout_event_emit(
                    RFSCOUT_EVENT_STORAGE_SERVICE_DISABLED, RFSCOUT_EVENT_SOURCE_SYSTEM, 1 );
                const esp_err_t result = persist_settings( false, file_limit );
                if( ESP_OK != result )
                {
                    disable_persist_pending = true;
                    disable_persist_attempts = 0U;
                    disable_persist_retry_us = esp_timer_get_time() + BREADCRUMB_PERSIST_RETRY_US;
                    ESP_LOGE( TAG,
                              "cannot persist automatic breadcrumb disable: %s",
                              esp_err_to_name( result ) );
                }
            }
        }
    }
    (void)xSemaphoreGive( write_mutex );
}

static void worker( void *a_argument )
{
    (void)a_argument;
    trim_files();
    while( true )
    {
        if( shutdown_requested && 0U == uxQueueMessagesWaiting( queue ) && !trim_requested )
        {
            worker_quiesced = true;
            while( shutdown_requested )
            {
                vTaskDelay( pdMS_TO_TICKS( BREADCRUMB_SHUTDOWN_POLL_MS ) );
            }
            worker_quiesced = false;
            continue;
        }

        breadcrumb_record_t record;
        if( pdTRUE == xQueueReceive( queue, &record, pdMS_TO_TICKS( BREADCRUMB_WORKER_POLL_MS ) ) )
        {
            write_record( &record );
        }
        if( trim_requested )
        {
            trim_requested = false;
            trim_files();
        }
    }
}

esp_err_t breadcrumbs_init( void )
{
    load_settings();
    if( NULL != worker_task )
    {
        shutdown_requested = false;
        return ESP_OK;
    }
    rfscout_media_collection_t collection;
    esp_err_t result = media_system_collection( BREADCRUMB_COLLECTION, &collection );
    if( ESP_OK == result )
    {
        result = media_journal_init(
            &journal, &collection, "breadcrumbs", BREADCRUMB_SEGMENT_BYTES, file_limit );
    }
    if( ESP_OK != result )
    {
        return result;
    }
    if( NULL == write_mutex )
    {
        write_mutex = xSemaphoreCreateMutexStatic( &write_mutex_control );
    }

    queue = xQueueCreateStatic(
        BREADCRUMB_QUEUE_DEPTH, sizeof( breadcrumb_record_t ), queue_storage, &queue_control );
    if( NULL == queue )
    {
        return ESP_ERR_NO_MEM;
    }

    last_capture_us = 0;
    last_queued_utc = 0;
    consecutive_write_failures = 0U;
    disable_persist_pending = false;
    disable_persist_attempts = 0U;
    trim_requested = false;
    shutdown_requested = false;
    worker_quiesced = false;
    if( pdPASS != xTaskCreate( worker,
                               "breadcrumbs",
                               BREADCRUMB_TASK_STACK,
                               NULL,
                               BREADCRUMB_TASK_PRIORITY,
                               &worker_task ) )
    {
        queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void breadcrumbs_poll( void )
{
    if( disable_persist_pending && NULL != write_mutex &&
        esp_timer_get_time() >= disable_persist_retry_us &&
        pdTRUE == xSemaphoreTake( write_mutex, 0U ) )
    {
        if( disable_persist_pending )
        {
            const esp_err_t result = persist_settings( false, file_limit );
            if( ESP_OK == result )
            {
                disable_persist_pending = false;
            }
            else
            {
                ++disable_persist_attempts;
                if( disable_persist_attempts >= BREADCRUMB_PERSIST_RETRY_LIMIT )
                {
                    disable_persist_pending = false;
                    ESP_LOGE( TAG, "breadcrumb auto-disable persistence retries exhausted" );
                }
                else
                {
                    disable_persist_retry_us = esp_timer_get_time() + BREADCRUMB_PERSIST_RETRY_US;
                }
            }
        }
        (void)xSemaphoreGive( write_mutex );
    }

    if( NULL == queue || shutdown_requested || !enabled || !gps_available() )
    {
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if( 0 != last_capture_us && ( now_us - last_capture_us ) < BREADCRUMB_MIN_INTERVAL_US )
    {
        return;
    }

    rfscout_gps_snapshot_t gps;
    if( !gps_snapshot_get( &gps ) || !gps.location_valid || !gps.time_valid ||
        gps.utc_epoch == last_queued_utc || 0U == uxQueueSpacesAvailable( queue ) ||
        !geo_movement_gate_accept( &movement_gate,
                                   gps.latitude_deg,
                                   gps.longitude_deg,
                                   device_config_get()->gps_movement_m ) )
    {
        return;
    }
    last_capture_us = now_us;
    float filtered_speed_mps = 0.0f;
    int current_rate_ms = 0;
    int target_rate_ms = 0;
    const bool rate_valid =
        gps_rate_diagnostics_get( &filtered_speed_mps, &current_rate_ms, &target_rate_ms );
    const breadcrumb_record_t record = {
        .utc_epoch = gps.utc_epoch,
        .latitude_deg = gps.latitude_deg,
        .longitude_deg = gps.longitude_deg,
        .speed_mph = gps.speed_mps * 2.23693629f,
        .speed_valid = gps.speed_valid,
        .filtered_speed_mph = filtered_speed_mps * 2.23693629f,
        .current_rate_ms = current_rate_ms,
        .target_rate_ms = target_rate_ms,
        .rate_valid = rate_valid,
    };
    if( pdTRUE == xQueueSend( queue, &record, 0U ) )
    {
        last_queued_utc = gps.utc_epoch;
    }
}

bool breadcrumbs_enabled( void )
{
    load_settings();
    return enabled;
}

esp_err_t breadcrumbs_set_enabled( bool a_enabled )
{
    load_settings();
    if( NULL != write_mutex )
    {
        (void)xSemaphoreTake( write_mutex, portMAX_DELAY );
    }

    esp_err_t result = ESP_OK;
    if( enabled != a_enabled || disable_persist_pending )
    {
        result = persist_settings( a_enabled, file_limit );
        if( ESP_OK == result )
        {
            enabled = a_enabled;
            disable_persist_pending = false;
            disable_persist_attempts = 0U;
            consecutive_write_failures = 0U;
            last_capture_us = 0;
            last_queued_utc = 0;
            memset( &movement_gate, 0, sizeof( movement_gate ) );
            if( !a_enabled && NULL != queue )
            {
                (void)xQueueReset( queue );
            }
        }
    }

    if( NULL != write_mutex )
    {
        (void)xSemaphoreGive( write_mutex );
    }
    return result;
}

uint16_t breadcrumbs_file_limit_step( uint16_t a_file_limit, int a_direction )
{
    const size_t count = sizeof( FILE_LIMIT_OPTIONS ) / sizeof( FILE_LIMIT_OPTIONS[ 0 ] );
    for( size_t index = 0U; index < count; ++index )
    {
        if( FILE_LIMIT_OPTIONS[ index ] == a_file_limit )
        {
            return FILE_LIMIT_OPTIONS[ ( index + ( a_direction > 0 ? 1U : count - 1U ) ) % count ];
        }
    }
    return RFSCOUT_BREADCRUMB_FILE_LIMIT_DEFAULT;
}

uint16_t breadcrumbs_file_limit( void )
{
    load_settings();
    return file_limit;
}

esp_err_t breadcrumbs_set_file_limit( uint16_t a_file_limit )
{
    load_settings();
    if( 0U == a_file_limit || a_file_limit > RFSCOUT_BREADCRUMB_FILE_LIMIT_MAX )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( NULL != write_mutex )
    {
        (void)xSemaphoreTake( write_mutex, portMAX_DELAY );
    }

    esp_err_t result = ESP_OK;
    if( file_limit != a_file_limit || disable_persist_pending )
    {
        result = persist_settings( enabled, a_file_limit );
        if( ESP_OK == result )
        {
            file_limit = a_file_limit;
            disable_persist_pending = false;
            disable_persist_attempts = 0U;
            request_trim();
        }
    }

    if( NULL != write_mutex )
    {
        (void)xSemaphoreGive( write_mutex );
    }
    return result;
}

bool breadcrumbs_shutdown( uint32_t a_timeout_ms )
{
    if( NULL == worker_task )
    {
        return true;
    }

    shutdown_requested = true;
    const TickType_t delay = pdMS_TO_TICKS( BREADCRUMB_SHUTDOWN_POLL_MS );
    uint32_t waited_ms = 0U;
    while( !worker_quiesced && waited_ms < a_timeout_ms )
    {
        vTaskDelay( delay );
        waited_ms += BREADCRUMB_SHUTDOWN_POLL_MS;
    }
    if( worker_quiesced )
    {
        return true;
    }

    shutdown_requested = false;
    ESP_LOGW( TAG, "breadcrumb shutdown timed out; recording resumes" );
    return false;
}
