/**
    breadcrumbs.c : RFScout GPS breadcrumb recorder
    Description: Records durable GPS-UTC location breadcrumbs to bounded CSV files on SD.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "breadcrumbs.h"

#include "gps.h"
#include "event.h"
#include "media_store.h"
#include "persistent_store.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Disable after consecutive storage-write failures; success resets the streak. */
#define BREADCRUMB_FAILURE_LIMIT 10U
#define BREADCRUMB_PERSIST_RETRY_LIMIT 3U
#define BREADCRUMB_PERSIST_RETRY_US 30000000LL
#define BREADCRUMB_COLLECTION "breadcrumbs"
#define BREADCRUMB_SETTINGS_KEY "breadcrumbs"
#define BREADCRUMB_SETTINGS_VERSION 1U
#define BREADCRUMB_SETTINGS_SIZE 4U
#define BREADCRUMB_FALLBACK_INTERVAL_US 5000000LL
/* CSV timestamps have whole-second precision; never write multiple records per UTC second. */
#define BREADCRUMB_MIN_INTERVAL_US 1000000LL
#define BREADCRUMB_QUEUE_DEPTH 4U
#define BREADCRUMB_TASK_STACK 4096U
#define BREADCRUMB_TASK_PRIORITY 2U
#define BREADCRUMB_WORKER_POLL_MS 500U
#define BREADCRUMB_SHUTDOWN_POLL_MS 20U
#define BREADCRUMB_FILENAME_LENGTH 16U
#define BREADCRUMB_FILENAME_CAPACITY ( BREADCRUMB_FILENAME_LENGTH + 1U )
#define BREADCRUMB_CSV_HEADER                                                                      \
    "timestamp_utc,latitude_deg,longitude_deg,speed_mph,filtered_speed_mph,current_rate_ms,"       \
    "target_rate_ms\n"
#define BREADCRUMB_CSV_HEADER_LENGTH ( sizeof( BREADCRUMB_CSV_HEADER ) - 1U )
#define BREADCRUMB_LINE_MAX 192U

typedef struct
{
    time_t utc_epoch;
    double latitude_deg;
    double longitude_deg;
    float speed_mph;
    float filtered_speed_mph;
    int current_rate_ms;
    int target_rate_ms;
    bool rate_valid;
} breadcrumb_record_t;

typedef enum
{
    BREADCRUMB_HEADER_VALID = 0,
    BREADCRUMB_HEADER_INVALID,
    BREADCRUMB_HEADER_IO_ERROR,
} breadcrumb_header_status_t;

static const char *TAG = "breadcrumbs";
static StaticQueue_t queue_control;
static uint8_t queue_storage[ BREADCRUMB_QUEUE_DEPTH * sizeof( breadcrumb_record_t ) ];
static QueueHandle_t queue;
static TaskHandle_t worker_task;
static int64_t last_capture_us;
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
static rfscout_media_collection_t collection;
static StaticSemaphore_t write_mutex_control;
static SemaphoreHandle_t write_mutex;

/** @brief Retention scan state shared by the two media_list() passes. */
typedef struct
{
    char *retained;
    size_t retained_count;
    size_t limit;
    size_t count;
    const char *threshold;
} breadcrumb_trim_scan_t;

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
    size_t length = 0U;
    const esp_err_t result =
        system_store_read( BREADCRUMB_SETTINGS_KEY, record, sizeof( record ), &length );
    if( ESP_ERR_NOT_FOUND == result )
    {
        return;
    }
    if( ESP_OK != result || sizeof( record ) != length ||
        BREADCRUMB_SETTINGS_VERSION != record[ 0 ] || record[ 1 ] > 1U )
    {
        ESP_LOGW( TAG, "discarding incompatible breadcrumb settings" );
        (void)system_store_remove( BREADCRUMB_SETTINGS_KEY );
        return;
    }

    const uint16_t stored_limit = (uint16_t)record[ 2 ] | ( (uint16_t)record[ 3 ] << 8U );
    if( 0U == stored_limit || stored_limit > RFSCOUT_BREADCRUMB_FILE_LIMIT_MAX )
    {
        ESP_LOGW( TAG, "discarding invalid breadcrumb file limit" );
        (void)system_store_remove( BREADCRUMB_SETTINGS_KEY );
        return;
    }

    enabled = 0U != record[ 1 ];
    file_limit = stored_limit;
}

static bool breadcrumb_filename_valid( const char *a_name )
{
    if( NULL == a_name || BREADCRUMB_FILENAME_LENGTH != strlen( a_name ) ||
        0 != strcmp( &a_name[ 11 ], "2.csv" ) )
    {
        return false;
    }
    for( size_t index = 0U; index < 11U; ++index )
    {
        if( !isdigit( (unsigned char)a_name[ index ] ) )
        {
            return false;
        }
    }
    return true;
}

static void copy_breadcrumb_filename( char *a_destination, const char *a_source )
{
    memcpy( a_destination, a_source, BREADCRUMB_FILENAME_CAPACITY );
}

static char *retained_slot( char *a_retained, size_t a_index )
{
    return &a_retained[ a_index * BREADCRUMB_FILENAME_CAPACITY ];
}

static size_t oldest_retained_index( char *a_retained, size_t a_count )
{
    size_t oldest = 0U;
    for( size_t index = 1U; index < a_count; ++index )
    {
        if( strcmp( retained_slot( a_retained, index ), retained_slot( a_retained, oldest ) ) < 0 )
        {
            oldest = index;
        }
    }
    return oldest;
}

/** @brief First retention pass: count bucket files and remember the newest `limit` names. */
static bool collect_retained( const rfscout_media_entry_t *a_entry, void *a_context )
{
    breadcrumb_trim_scan_t *scan = a_context;
    if( !breadcrumb_filename_valid( a_entry->name ) )
    {
        return true;
    }
    ++scan->count;
    if( scan->retained_count < scan->limit )
    {
        copy_breadcrumb_filename( retained_slot( scan->retained, scan->retained_count ),
                                  a_entry->name );
        ++scan->retained_count;
        return true;
    }

    const size_t oldest = oldest_retained_index( scan->retained, scan->retained_count );
    if( strcmp( a_entry->name, retained_slot( scan->retained, oldest ) ) > 0 )
    {
        copy_breadcrumb_filename( retained_slot( scan->retained, oldest ), a_entry->name );
    }
    return true;
}

/** @brief Second retention pass: delete bucket files older than the retained threshold. */
static bool delete_expired( const rfscout_media_entry_t *a_entry, void *a_context )
{
    const breadcrumb_trim_scan_t *scan = a_context;
    if( !breadcrumb_filename_valid( a_entry->name ) ||
        0 <= strcmp( a_entry->name, scan->threshold ) )
    {
        return true;
    }
    const esp_err_t result = media_remove( &collection, a_entry->name );
    if( ESP_OK != result )
    {
        ESP_LOGE( TAG, "breadcrumb retention delete failed: %s", esp_err_to_name( result ) );
    }
    return true;
}

static void trim_files( void )
{
    const uint16_t limit = file_limit;
    if( 0U == limit || !media_store_available() )
    {
        return;
    }

    char *retained = heap_caps_calloc(
        limit, BREADCRUMB_FILENAME_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    if( NULL == retained )
    {
        ESP_LOGE( TAG, "breadcrumb retention workspace unavailable" );
        return;
    }

    breadcrumb_trim_scan_t scan = {
        .retained = retained,
        .limit = limit,
    };
    if( ESP_OK != media_list( &collection, collect_retained, &scan ) || scan.count <= limit ||
        0U == scan.retained_count )
    {
        heap_caps_free( retained );
        return;
    }

    const size_t threshold_index = oldest_retained_index( retained, scan.retained_count );
    char threshold[ BREADCRUMB_FILENAME_CAPACITY ];
    copy_breadcrumb_filename( threshold, retained_slot( retained, threshold_index ) );
    heap_caps_free( retained );

    scan.threshold = threshold;
    (void)media_list( &collection, delete_expired, &scan );
}

static void request_trim( void )
{
    if( NULL != worker_task )
    {
        trim_requested = true;
    }
}

static bool format_bucket_name( time_t a_utc_epoch,
                                char a_filename[ BREADCRUMB_FILENAME_CAPACITY ] )
{
    struct tm utc;
    if( NULL == gmtime_r( &a_utc_epoch, &utc ) )
    {
        return false;
    }

    if( 10U != strftime( a_filename, BREADCRUMB_FILENAME_CAPACITY, "%Y%m%d%H", &utc ) )
    {
        return false;
    }
    a_filename[ 10 ] = (char)( '0' + ( utc.tm_min / 10 ) );
    memcpy( &a_filename[ 11 ], "2.csv", 6U );
    return breadcrumb_filename_valid( a_filename );
}

static bool format_timestamp( time_t a_utc_epoch, char *a_output, size_t a_capacity )
{
    struct tm utc;
    if( NULL == gmtime_r( &a_utc_epoch, &utc ) )
    {
        return false;
    }
    return 0U != strftime( a_output, a_capacity, "%Y-%m-%dT%H:%M:%SZ", &utc );
}

static breadcrumb_header_status_t existing_header_status( const char *a_name )
{
    rfscout_media_file_t file;
    if( ESP_OK != media_open( &collection, a_name, RFSCOUT_MEDIA_OPEN_READ, &file ) )
    {
        return BREADCRUMB_HEADER_IO_ERROR;
    }

    char header[ BREADCRUMB_CSV_HEADER_LENGTH ];
    size_t length = 0U;
    const esp_err_t read_result = media_read( &file, header, sizeof( header ), &length );
    if( ESP_OK != media_close( &file ) || ESP_OK != read_result )
    {
        return BREADCRUMB_HEADER_IO_ERROR;
    }
    return ( sizeof( header ) == length &&
             0 == memcmp( header, BREADCRUMB_CSV_HEADER, BREADCRUMB_CSV_HEADER_LENGTH ) )
               ? BREADCRUMB_HEADER_VALID
               : BREADCRUMB_HEADER_INVALID;
}

static bool prepare_append( const rfscout_media_file_t *a_file, bool a_empty_file )
{
    if( a_empty_file )
    {
        return ESP_OK == media_write( a_file, BREADCRUMB_CSV_HEADER, BREADCRUMB_CSV_HEADER_LENGTH );
    }

    char tail = '\0';
    size_t length = 0U;
    if( ESP_OK != media_seek( a_file, -1, RFSCOUT_MEDIA_SEEK_END ) ||
        ESP_OK != media_read( a_file, &tail, 1U, &length ) || 1U != length )
    {
        return false;
    }
    /* Reposition before switching from reading to writing; append mode then writes at the end. */
    if( ESP_OK != media_seek( a_file, 0, RFSCOUT_MEDIA_SEEK_END ) )
    {
        return false;
    }
    return '\n' == tail || ESP_OK == media_write( a_file, "\n", 1U );
}

/** @brief Append one record; caller holds write_mutex so disable cannot race a write. */
static bool write_record_locked( const breadcrumb_record_t *a_record )
{
    char name[ BREADCRUMB_FILENAME_CAPACITY ];
    char timestamp[ 32 ];
    if( !format_bucket_name( a_record->utc_epoch, name ) ||
        !format_timestamp( a_record->utc_epoch, timestamp, sizeof( timestamp ) ) )
    {
        return false;
    }

    rfscout_media_entry_t entry;
    const esp_err_t stat_result = media_stat( &collection, name, &entry );
    const bool new_file = ESP_OK != stat_result;
    if( new_file && ESP_ERR_NOT_FOUND != stat_result )
    {
        ESP_LOGE( TAG, "breadcrumb size query failed: %s", esp_err_to_name( stat_result ) );
        return false;
    }

    breadcrumb_header_status_t header_status = BREADCRUMB_HEADER_VALID;
    if( !new_file )
    {
        header_status = existing_header_status( name );
        if( BREADCRUMB_HEADER_IO_ERROR == header_status )
        {
            ESP_LOGE( TAG, "breadcrumb header read failed" );
            return false;
        }
    }

    const bool recreate_file = BREADCRUMB_HEADER_INVALID == header_status;
    if( recreate_file )
    {
        ESP_LOGW( TAG, "recreating breadcrumb bucket with incomplete header" );
    }

    rfscout_media_file_t file;
    const esp_err_t open_result =
        media_open( &collection,
                    name,
                    recreate_file ? RFSCOUT_MEDIA_OPEN_REPLACE : RFSCOUT_MEDIA_OPEN_APPEND,
                    &file );
    if( ESP_OK != open_result )
    {
        ESP_LOGE( TAG, "breadcrumb append open failed: %s", esp_err_to_name( open_result ) );
        return false;
    }

    const bool empty_file = new_file || recreate_file || 0U == entry.size_bytes;
    bool success = prepare_append( &file, empty_file );

    char line[ BREADCRUMB_LINE_MAX ];
    const int length = snprintf( line,
                                 sizeof( line ),
                                 "%s,%.7f,%.7f,%.2f,%.2f,%d,%d\n",
                                 timestamp,
                                 a_record->latitude_deg,
                                 a_record->longitude_deg,
                                 (double)a_record->speed_mph,
                                 a_record->rate_valid ? (double)a_record->filtered_speed_mph : 0.0,
                                 a_record->rate_valid ? a_record->current_rate_ms : 0,
                                 a_record->rate_valid ? a_record->target_rate_ms : 0 );
    if( success && ( length < 0 || (size_t)length >= sizeof( line ) ) )
    {
        success = false;
    }
    if( success && ESP_OK != media_write( &file, line, (size_t)length ) )
    {
        success = false;
    }
    if( success && ESP_OK != media_sync( &file ) )
    {
        success = false;
    }
    if( ESP_OK != media_close( &file ) )
    {
        success = false;
    }

    if( !success )
    {
        ESP_LOGE( TAG, "breadcrumb write failed" );
    }
    else if( new_file )
    {
        trim_files();
    }
    return success;
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
    const esp_err_t collection_result =
        media_system_collection( BREADCRUMB_COLLECTION, &collection );
    if( ESP_OK != collection_result )
    {
        return collection_result;
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
    float filtered_speed_mps = 0.0f;
    int current_rate_ms = 0;
    int target_rate_ms = 0;
    const bool rate_valid =
        gps_rate_diagnostics_get( &filtered_speed_mps, &current_rate_ms, &target_rate_ms );
    /* Follow verified receiver cadence, not a requested target. The current CSV
       timestamp and dedup contract permit at most one record per UTC second. */
    int64_t interval_us = BREADCRUMB_FALLBACK_INTERVAL_US;
    if( rate_valid && current_rate_ms >= 100 && current_rate_ms <= 1000 )
    {
        interval_us = (int64_t)current_rate_ms * 1000LL;
        if( interval_us < BREADCRUMB_MIN_INTERVAL_US )
        {
            interval_us = BREADCRUMB_MIN_INTERVAL_US;
        }
    }
    const int64_t now_us = esp_timer_get_time();
    if( 0 != last_capture_us && ( now_us - last_capture_us ) < interval_us )
    {
        return;
    }

    rfscout_gps_snapshot_t gps;
    if( !gps_snapshot_get( &gps ) || !gps.location_valid || !gps.time_valid ||
        gps.utc_epoch == last_queued_utc )
    {
        return;
    }
    last_capture_us = now_us;
    const breadcrumb_record_t record = {
        .utc_epoch = gps.utc_epoch,
        .latitude_deg = gps.latitude_deg,
        .longitude_deg = gps.longitude_deg,
        .speed_mph = gps.speed_valid ? gps.speed_mps * 2.23693629f : 0.0f,
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
