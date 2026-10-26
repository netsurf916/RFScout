/**
    breadcrumbs.c : RFScout GPS breadcrumb recorder
    Description: Appends GPS-UTC breadcrumbs to daily files without deleting previous days.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "breadcrumbs.h"
#include "breadcrumb_rules.h"

#include "gps.h"
#include "device_time.h"
#include "device_config.h"
#include "geo.h"
#include "event.h"
#include "media_journal.h"
#include "media_store.h"
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
#define BREADCRUMB_SETTINGS_KEY "breadcrumbs"
#define BREADCRUMB_SETTINGS_VERSION 2U
/* Preserve four-byte settings layout; v1 file-limit bytes are ignored. */
#define BREADCRUMB_SETTINGS_SIZE 4U
#define BREADCRUMB_QUEUE_DEPTH 4U
#define BREADCRUMB_TASK_STACK 4096U
#define BREADCRUMB_TASK_PRIORITY 2U
#define BREADCRUMB_WORKER_POLL_MS 500U
#define BREADCRUMB_SHUTDOWN_POLL_MS 20U
#define BREADCRUMB_PRUNE_RETRY_US 60000000LL
#define BREADCRUMB_PRUNE_STEP_US 500000LL

typedef struct
{
    time_t utc_epoch;
    double latitude_deg;
    double longitude_deg;
    float speed_mph;
    bool speed_valid;
    float filtered_speed_mph;
    bool filtered_speed_valid;
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
static time_t last_queued_utc;
static volatile bool shutdown_requested;
static volatile bool worker_quiesced;
static bool settings_loaded;
static uint8_t consecutive_write_failures;
static uint8_t disable_persist_attempts;
static int64_t disable_persist_retry_us;
static bool disable_persist_pending;
static volatile bool enabled;
static volatile breadcrumb_retention_t retention = BREADCRUMB_RETENTION_FOREVER;
static bool prune_pending;
static int64_t next_prune_us;
static int64_t prune_day = -1;
static rfscout_media_collection_t collection;
static StaticSemaphore_t write_mutex_control;
static SemaphoreHandle_t write_mutex;

static esp_err_t persist_settings( bool a_enabled, breadcrumb_retention_t a_retention )
{
    const uint8_t record[ BREADCRUMB_SETTINGS_SIZE ] = {
        BREADCRUMB_SETTINGS_VERSION,
        a_enabled ? 1U : 0U,
        (uint8_t)a_retention,
        0U,
    };
    return system_store_write( BREADCRUMB_SETTINGS_KEY, record, sizeof( record ) );
}

static bool settings_valid( const void *a_record )
{
    const uint8_t *record = a_record;
    /* V1 stores a segment count in bytes 2/3: do not reinterpret it. */
    return record[ 1 ] <= 1U &&
           ( 1U == record[ 0 ] ||
             ( BREADCRUMB_SETTINGS_VERSION == record[ 0 ] &&
               record[ 2 ] < (uint8_t)BREADCRUMB_RETENTION_COUNT ) );
}

static void load_settings( void )
{
    if( settings_loaded )
    {
        return;
    }
    settings_loaded = true;
    enabled = false;
    retention = BREADCRUMB_RETENTION_FOREVER;

    uint8_t record[ BREADCRUMB_SETTINGS_SIZE ];
    if( ESP_OK ==
        system_store_load( BREADCRUMB_SETTINGS_KEY, record, sizeof( record ), settings_valid ) )
    {
        enabled = 0U != record[ 1 ];
        if( BREADCRUMB_SETTINGS_VERSION == record[ 0 ] )
        {
            retention = (breadcrumb_retention_t)record[ 2 ];
        }
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
    if( record->filtered_speed_valid )
    {
        length = media_journal_printf( a_line,
                                       a_capacity,
                                       length,
                                       ",\"filtered_speed_mph\":%.2f",
                                       (double)record->filtered_speed_mph );
    }
    if( record->rate_valid )
    {
        length = media_journal_printf( a_line,
                                       a_capacity,
                                       length,
                                       ",\"rate_ms\":%d,\"target_rate_ms\":%d",
                                       record->current_rate_ms,
                                       record->target_rate_ms );
    }
    return media_journal_printf( a_line, a_capacity, length, "}" );
}

/** @brief Write one JSONL line using the shared media store and append-only mode. */
static bool write_record_locked( const breadcrumb_record_t *a_record )
{
    char filename[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
    char line[ RFSCOUT_MEDIA_JOURNAL_LINE_MAX ];
    if( !breadcrumb_daily_filename( a_record->utc_epoch, filename, sizeof( filename ) ) )
    {
        return false;
    }
    const int length = format_record( (void *)a_record, 0U, line, sizeof( line ) );
    if( length < 0 || (size_t)length + 1U >= sizeof( line ) )
    {
        return false;
    }
    line[ length ] = '\n';
    rfscout_media_file_t file = { 0 };
    esp_err_t result = media_open( &collection, filename, RFSCOUT_MEDIA_OPEN_APPEND, &file );
    uint64_t prior_size = 0U;
    if( ESP_OK == result )
    {
        result = media_size( &file, &prior_size );
        if( ESP_OK == result )
        {
            result = media_write( &file, line, (size_t)length + 1U );
            if( ESP_OK == result )
            {
                result = media_sync( &file );
            }
            if( ESP_OK != result )
            {
                /* Best effort: do not append after an incomplete JSONL line. */
                (void)media_truncate( &file, prior_size );
            }
        }
        const esp_err_t close_result = media_close( &file );
        if( ESP_OK == result )
        {
            result = close_result;
        }
    }
    if( ESP_OK != result )
    {
        ESP_LOGE( TAG, "breadcrumb append failed: %s", esp_err_to_name( result ) );
    }
    return ESP_OK == result;
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
                const esp_err_t result = persist_settings( false, retention );
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

typedef struct
{
    time_t cutoff_utc;
    char filename[ RFSCOUT_MEDIA_NAME_MAX + 1U ];
} breadcrumb_expiry_candidate_t;

static bool find_expired( const rfscout_media_entry_t *a_entry, void *a_context )
{
    breadcrumb_expiry_candidate_t *candidate = a_context;
    if( breadcrumb_filename_expired( a_entry->name, candidate->cutoff_utc ) )
    {
        (void)snprintf(
            candidate->filename, sizeof( candidate->filename ), "%s", a_entry->name );
        return false;
    }
    return true;
}

/** @brief Delete at most one expired day: 1 removed, 0 done, -1 retry later. */
static int prune_one( time_t a_now_utc, breadcrumb_retention_t a_retention )
{
    const unsigned days = BREADCRUMB_RETENTION_DAY == a_retention     ? 1U
                          : BREADCRUMB_RETENTION_WEEK == a_retention  ? 7U
                          : BREADCRUMB_RETENTION_MONTH == a_retention ? 30U
                                                                      : 0U;
    if( 0U == days )
    {
        return 0;
    }
    breadcrumb_expiry_candidate_t candidate = {
        .cutoff_utc = a_now_utc - (time_t)days * 86400,
    };
    const esp_err_t listed = media_list( &collection, find_expired, &candidate );
    if( ESP_OK != listed )
    {
        ESP_LOGW( TAG, "breadcrumb retention listing failed: %s", esp_err_to_name( listed ) );
        return -1;
    }
    if( '\0' == candidate.filename[ 0 ] )
    {
        return 0;
    }
    const esp_err_t removed = media_remove( &collection, candidate.filename );
    if( ESP_OK != removed )
    {
        ESP_LOGW( TAG,
                  "breadcrumb retention could not remove %s: %s",
                  candidate.filename,
                  esp_err_to_name( removed ) );
        return -1;
    }
    return 1; /* Another expired file may remain. */
}

/** @brief Run bounded retention I/O exclusively on the existing storage worker. */
static void prune_poll( int64_t a_now_us )
{
    if( NULL == write_mutex || a_now_us < next_prune_us ||
        pdTRUE != xSemaphoreTake( write_mutex, 0U ) )
    {
        return;
    }
    if( BREADCRUMB_RETENTION_FOREVER == retention )
    {
        prune_pending = false;
        next_prune_us = a_now_us + BREADCRUMB_PRUNE_RETRY_US;
        (void)xSemaphoreGive( write_mutex );
        return;
    }
    time_t now_utc = 0;
    if( !device_time_network_ready() || !device_time_now( &now_utc ) )
    {
        next_prune_us = a_now_us + BREADCRUMB_PRUNE_RETRY_US;
        (void)xSemaphoreGive( write_mutex );
        return; /* Refuse deletion without recently verified GPS or NTP UTC. */
    }
    const int64_t day = (int64_t)now_utc / 86400LL;
    if( day != prune_day )
    {
        prune_day = day;
        prune_pending = true;
    }
    if( prune_pending )
    {
        const int outcome = prune_one( now_utc, retention );
        prune_pending = 0 != outcome;
        next_prune_us = a_now_us + ( outcome > 0 ? BREADCRUMB_PRUNE_STEP_US
                                                : BREADCRUMB_PRUNE_RETRY_US );
    }
    else
    {
        next_prune_us = a_now_us + BREADCRUMB_PRUNE_RETRY_US;
    }
    (void)xSemaphoreGive( write_mutex );
}

static void worker( void *a_argument )
{
    (void)a_argument;
    while( true )
    {
        if( shutdown_requested && 0U == uxQueueMessagesWaiting( queue ) )
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
        prune_poll( esp_timer_get_time() );
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
    const esp_err_t result = media_system_collection( BREADCRUMB_COLLECTION, &collection );
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
    shutdown_requested = false;
    worker_quiesced = false;
    prune_pending = true;
    prune_day = -1;
    next_prune_us = 0;
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
            const esp_err_t result = persist_settings( false, retention );
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
    rfscout_gps_snapshot_t gps = { 0 };
    if( !gps_snapshot_get( &gps ) || !gps_location_fresh( &gps ) || !gps.time_valid ||
        gps.utc_epoch == last_queued_utc || 0U == uxQueueSpacesAvailable( queue ) )
    {
        return;
    }
    float filtered_speed_mps = 0.0f;
    int current_rate_ms = 0;
    int target_rate_ms = 0;
    const bool rate_valid =
        gps_rate_diagnostics_get( &filtered_speed_mps, &current_rate_ms, &target_rate_ms );
    float smoothed_speed = 0.0f;
    const bool filtered_valid = gps_filtered_speed_get( &smoothed_speed );
    const float speed_mps = filtered_valid ? smoothed_speed
                                         : ( gps.speed_valid ? gps.speed_mps : 0.0f );
    geo_movement_gate_t next_gate = movement_gate;
    const bool moved = geo_movement_gate_accept(
        &next_gate, gps.latitude_deg, gps.longitude_deg, device_config_get()->gps_movement_m );
    if( !breadcrumb_capture_due( now_us, last_capture_us, speed_mps, moved ) )
    {
        return;
    }
    if( !moved )
    {
        /* A stationary heartbeat is still a queued point: reset the movement anchor. */
        memset( &next_gate, 0, sizeof( next_gate ) );
        (void)geo_movement_gate_accept(
            &next_gate, gps.latitude_deg, gps.longitude_deg, device_config_get()->gps_movement_m );
    }
    const breadcrumb_record_t record = {
        .utc_epoch = gps.utc_epoch,
        .latitude_deg = gps.latitude_deg,
        .longitude_deg = gps.longitude_deg,
        .speed_mph = gps.speed_mps * 2.23693629f,
        .speed_valid = gps.speed_valid,
        .filtered_speed_mph = smoothed_speed * 2.23693629f,
        .filtered_speed_valid = filtered_valid,
        .current_rate_ms = current_rate_ms,
        .target_rate_ms = target_rate_ms,
        .rate_valid = rate_valid,
    };
    if( pdTRUE == xQueueSend( queue, &record, 0U ) )
    {
        last_capture_us = now_us;
        last_queued_utc = gps.utc_epoch;
        movement_gate = next_gate;
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
        result = persist_settings( a_enabled, retention );
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

breadcrumb_retention_t breadcrumbs_retention( void )
{
    load_settings();
    return retention;
}

const char *breadcrumbs_retention_name( breadcrumb_retention_t a_retention )
{
    switch( a_retention )
    {
        case BREADCRUMB_RETENTION_DAY:
            return "1 day";
        case BREADCRUMB_RETENTION_WEEK:
            return "1 week";
        case BREADCRUMB_RETENTION_MONTH:
            return "1 month";
        case BREADCRUMB_RETENTION_FOREVER:
            return "Forever";
        default:
            return "Forever";
    }
}

breadcrumb_retention_t breadcrumbs_retention_step( breadcrumb_retention_t a_current,
                                                   int a_direction )
{
    if( a_current >= BREADCRUMB_RETENTION_COUNT )
    {
        return BREADCRUMB_RETENTION_FOREVER;
    }
    return (breadcrumb_retention_t)(
        ( (unsigned)a_current +
          ( a_direction > 0 ? 1U : (unsigned)BREADCRUMB_RETENTION_COUNT - 1U ) ) %
        (unsigned)BREADCRUMB_RETENTION_COUNT );
}

esp_err_t breadcrumbs_set_retention( breadcrumb_retention_t a_retention )
{
    if( a_retention >= BREADCRUMB_RETENTION_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    load_settings();
    if( NULL != write_mutex )
    {
        (void)xSemaphoreTake( write_mutex, portMAX_DELAY );
    }
    esp_err_t result = ESP_OK;
    if( a_retention != retention )
    {
        result = persist_settings( enabled, a_retention );
        if( ESP_OK == result )
        {
            retention = a_retention;
            prune_pending = true;
            next_prune_us = 0;
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
