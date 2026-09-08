/**
    breadcrumbs.c : RFScout GPS breadcrumb recorder
    Description: Records durable GPS-UTC location breadcrumbs to bounded CSV files on SD.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "breadcrumbs.h"

#include "gps.h"
#include "persistent_store.h"
#include "storage.h"
#include "storage_internal.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BREADCRUMB_DIRECTORY RFSCOUT_STORAGE_LOG_DIRECTORY "/breadcrumbs"
#define BREADCRUMB_SETTINGS_KEY "breadcrumbs"
#define BREADCRUMB_SETTINGS_VERSION 1U
#define BREADCRUMB_SETTINGS_SIZE 4U
#define BREADCRUMB_INTERVAL_US 5000000LL
#define BREADCRUMB_QUEUE_DEPTH 4U
#define BREADCRUMB_TASK_STACK 4096U
#define BREADCRUMB_TASK_PRIORITY 2U
#define BREADCRUMB_WORKER_POLL_MS 500U
#define BREADCRUMB_FILENAME_LENGTH 15U
#define BREADCRUMB_FILENAME_CAPACITY ( BREADCRUMB_FILENAME_LENGTH + 1U )
#define BREADCRUMB_PATH_CAPACITY \
    ( sizeof( BREADCRUMB_DIRECTORY ) + BREADCRUMB_FILENAME_LENGTH + 1U )

typedef struct
{
    time_t utc_epoch;
    double latitude_deg;
    double longitude_deg;
} breadcrumb_record_t;

static const char *TAG = "breadcrumbs";
static StaticQueue_t queue_control;
static uint8_t queue_storage[ BREADCRUMB_QUEUE_DEPTH * sizeof( breadcrumb_record_t ) ];
static QueueHandle_t queue;
static TaskHandle_t worker_task;
static int64_t last_capture_us;
static time_t last_queued_utc;
static volatile bool trim_requested;
static bool settings_loaded;
static volatile bool enabled;
static volatile uint16_t file_limit = RFSCOUT_BREADCRUMB_FILE_LIMIT_DEFAULT;

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

static bool ensure_directory_locked( void )
{
    if( 0 == mkdir( BREADCRUMB_DIRECTORY, 0775 ) || EEXIST == errno )
    {
        return true;
    }
    ESP_LOGE( TAG, "breadcrumb directory creation failed: errno=%d", errno );
    return false;
}

static bool breadcrumb_filename_valid( const char *a_name )
{
    if( NULL == a_name || BREADCRUMB_FILENAME_LENGTH != strlen( a_name ) ||
        0 != strcmp( &a_name[ 11 ], ".csv" ) )
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

static bool breadcrumb_path_from_filename( const char *a_filename,
                                           char *a_path,
                                           size_t a_capacity )
{
    const size_t directory_length = sizeof( BREADCRUMB_DIRECTORY ) - 1U;
    const size_t required = directory_length + 1U + BREADCRUMB_FILENAME_CAPACITY;
    if( NULL == a_filename || NULL == a_path || a_capacity < required ||
        !breadcrumb_filename_valid( a_filename ) )
    {
        return false;
    }

    memcpy( a_path, BREADCRUMB_DIRECTORY, directory_length );
    a_path[ directory_length ] = '/';
    memcpy( &a_path[ directory_length + 1U ], a_filename, BREADCRUMB_FILENAME_CAPACITY );
    return true;
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

static void trim_files_locked( void )
{
    const uint16_t limit = file_limit;
    if( 0U == limit )
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

    DIR *directory = opendir( BREADCRUMB_DIRECTORY );
    if( NULL == directory )
    {
        heap_caps_free( retained );
        return;
    }

    size_t count = 0U;
    size_t retained_count = 0U;
    struct dirent *entry;
    while( NULL != ( entry = readdir( directory ) ) )
    {
        if( !breadcrumb_filename_valid( entry->d_name ) )
        {
            continue;
        }
        ++count;
        if( retained_count < limit )
        {
            copy_breadcrumb_filename( retained_slot( retained, retained_count ), entry->d_name );
            ++retained_count;
            continue;
        }

        const size_t oldest = oldest_retained_index( retained, retained_count );
        if( strcmp( entry->d_name, retained_slot( retained, oldest ) ) > 0 )
        {
            copy_breadcrumb_filename( retained_slot( retained, oldest ), entry->d_name );
        }
    }
    closedir( directory );

    if( count <= limit || 0U == retained_count )
    {
        heap_caps_free( retained );
        return;
    }

    const size_t threshold_index = oldest_retained_index( retained, retained_count );
    char threshold[ BREADCRUMB_FILENAME_CAPACITY ];
    copy_breadcrumb_filename( threshold, retained_slot( retained, threshold_index ) );
    heap_caps_free( retained );

    directory = opendir( BREADCRUMB_DIRECTORY );
    if( NULL == directory )
    {
        return;
    }
    while( NULL != ( entry = readdir( directory ) ) )
    {
        if( !breadcrumb_filename_valid( entry->d_name ) || 0 <= strcmp( entry->d_name, threshold ) )
        {
            continue;
        }
        char path[ BREADCRUMB_PATH_CAPACITY ];
        if( !breadcrumb_path_from_filename( entry->d_name, path, sizeof( path ) ) )
        {
            continue;
        }
        if( 0 != unlink( path ) )
        {
            ESP_LOGE( TAG, "breadcrumb retention delete failed: errno=%d", errno );
        }
    }
    closedir( directory );
}

static void trim_files( void )
{
    if( !storage_is_available() || !storage_io_acquire( RFSCOUT_STORAGE_WAIT_FOREVER ) )
    {
        return;
    }
    if( ensure_directory_locked() )
    {
        trim_files_locked();
    }
    storage_io_release();
}

static void request_trim( void )
{
    if( NULL != worker_task )
    {
        trim_requested = true;
    }
}

static bool format_bucket_path( time_t a_utc_epoch, char *a_path, size_t a_capacity )
{
    struct tm utc;
    if( NULL == gmtime_r( &a_utc_epoch, &utc ) )
    {
        return false;
    }

    char filename[ BREADCRUMB_FILENAME_CAPACITY ];
    if( 10U != strftime( filename, sizeof( filename ), "%Y%m%d%H", &utc ) )
    {
        return false;
    }
    filename[ 10 ] = (char)( '0' + ( utc.tm_min / 10 ) );
    memcpy( &filename[ 11 ], ".csv", 5U );
    return breadcrumb_path_from_filename( filename, a_path, a_capacity );
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

static bool prepare_append( FILE *a_file, bool a_new_file, off_t a_size )
{
    if( a_new_file || 0 == a_size )
    {
        return 0 <= fprintf( a_file, "timestamp_utc,latitude_deg,longitude_deg\n" );
    }

    if( 0 != fseek( a_file, -1L, SEEK_END ) )
    {
        return false;
    }
    const int tail = fgetc( a_file );
    if( EOF == tail )
    {
        return false;
    }
    if( 0 != fseek( a_file, 0L, SEEK_END ) )
    {
        return false;
    }
    return '\n' == tail || EOF != fputc( '\n', a_file );
}

static void write_record( const breadcrumb_record_t *a_record )
{
    if( NULL == a_record || !storage_is_available() ||
        !storage_io_acquire( RFSCOUT_STORAGE_WAIT_FOREVER ) )
    {
        return;
    }

    if( !ensure_directory_locked() )
    {
        storage_io_release();
        return;
    }

    char path[ BREADCRUMB_PATH_CAPACITY ];
    char timestamp[ 32 ];
    if( !format_bucket_path( a_record->utc_epoch, path, sizeof( path ) ) ||
        !format_timestamp( a_record->utc_epoch, timestamp, sizeof( timestamp ) ) )
    {
        storage_io_release();
        return;
    }

    struct stat state;
    const bool new_file = 0 != stat( path, &state );
    if( new_file && ENOENT != errno )
    {
        ESP_LOGE( TAG, "breadcrumb size query failed: errno=%d", errno );
        storage_io_release();
        return;
    }

    FILE *file = fopen( path, "a+" );
    if( NULL == file )
    {
        ESP_LOGE( TAG, "breadcrumb append open failed: errno=%d", errno );
        storage_io_release();
        return;
    }

    bool success = prepare_append( file, new_file, new_file ? 0 : state.st_size );
    if( success && 0 > fprintf( file,
                                "%s,%.7f,%.7f\n",
                                timestamp,
                                a_record->latitude_deg,
                                a_record->longitude_deg ) )
    {
        success = false;
    }
    if( success && 0 != fflush( file ) )
    {
        success = false;
    }
    if( success && 0 != fsync( fileno( file ) ) )
    {
        success = false;
    }
    if( 0 != fclose( file ) )
    {
        success = false;
    }

    if( !success )
    {
        ESP_LOGE( TAG, "breadcrumb write failed: errno=%d", errno );
    }
    else if( new_file )
    {
        trim_files_locked();
    }
    storage_io_release();
}

static void worker( void *a_argument )
{
    (void)a_argument;
    trim_files();
    while( true )
    {
        breadcrumb_record_t record;
        if( pdTRUE == xQueueReceive( queue, &record, pdMS_TO_TICKS( BREADCRUMB_WORKER_POLL_MS ) ) &&
            enabled )
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
        return ESP_OK;
    }
    if( !gps_available() )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if( !storage_is_available() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    queue = xQueueCreateStatic(
        BREADCRUMB_QUEUE_DEPTH, sizeof( breadcrumb_record_t ), queue_storage, &queue_control );
    if( NULL == queue )
    {
        return ESP_ERR_NO_MEM;
    }

    last_capture_us = 0;
    last_queued_utc = 0;
    trim_requested = false;
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
    if( NULL == queue || !enabled || !storage_is_available() )
    {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if( 0 != last_capture_us && ( now_us - last_capture_us ) < BREADCRUMB_INTERVAL_US )
    {
        return;
    }
    last_capture_us = now_us;

    rfscout_gps_snapshot_t gps;
    if( !gps_snapshot_get( &gps ) || !gps.location_valid || !gps.time_valid ||
        gps.utc_epoch == last_queued_utc )
    {
        return;
    }

    const breadcrumb_record_t record = {
        .utc_epoch = gps.utc_epoch,
        .latitude_deg = gps.latitude_deg,
        .longitude_deg = gps.longitude_deg,
    };
    if( pdTRUE == xQueueSend( queue, &record, 0U ) )
    {
        last_queued_utc = gps.utc_epoch;
    }
}

bool breadcrumbs_available( void )
{
    return NULL != worker_task && gps_available() && storage_is_available();
}

bool breadcrumbs_enabled( void )
{
    load_settings();
    return enabled;
}

esp_err_t breadcrumbs_set_enabled( bool a_enabled )
{
    load_settings();
    if( enabled == a_enabled )
    {
        return ESP_OK;
    }
    const esp_err_t result = persist_settings( a_enabled, file_limit );
    if( ESP_OK == result )
    {
        enabled = a_enabled;
        last_capture_us = 0;
        last_queued_utc = 0;
        if( !a_enabled && NULL != queue )
        {
            (void)xQueueReset( queue );
        }
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
    if( file_limit == a_file_limit )
    {
        return ESP_OK;
    }
    const esp_err_t result = persist_settings( enabled, a_file_limit );
    if( ESP_OK == result )
    {
        file_limit = a_file_limit;
        request_trim();
    }
    return result;
}
