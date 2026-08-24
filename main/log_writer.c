/**
    log_writer.c : RFScout asynchronous SD log writer
    Description: Writes normalized observations to a 100-segment circular text log.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "log_writer.h"

#include "storage.h"
#include "surveillance.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_QUEUE_DEPTH 128U
#define LOG_LINE_MAX 256U
#define LOG_TASK_STACK_SIZE 4096U
#define LOG_TASK_PRIORITY 2U
#define LOG_FLUSH_INTERVAL_US 1000000LL
#define LOG_SHUTDOWN_POLL_MS 20U
#define LOG_SEQUENCE_PREFIX "# RFSCOUT LOG SEQUENCE "

typedef struct
{
    uint32_t timestamp_ms;
    rf_source_t source;
    uint8_t address[ 6 ];
    int8_t rssi;
    uint16_t channel;
    surveillance_signature_id_t signature;
    uint8_t score;
    uint8_t evidence;
    char vendor[ RFSCOUT_VENDOR_NAME_MAX ];
    char name[ RFSCOUT_DEVICE_NAME_MAX ];
} log_record_t;

static const char *TAG = "log_writer";
static StaticQueue_t queue_control;
static uint8_t queue_storage[ LOG_QUEUE_DEPTH * sizeof( log_record_t ) ];
static QueueHandle_t queue;
static TaskHandle_t writer_task;
static FILE *active_file;
static uint32_t active_sequence;
static unsigned active_slot;
static size_t active_size;
static volatile bool accepting;
static volatile bool shutdown_requested;
static volatile bool writer_stopped;
static uint32_t dropped_records;

/** @brief Build the path for one circular log slot. */
static void segment_path( unsigned a_slot, char *a_path, size_t a_capacity )
{
    snprintf( a_path,
              a_capacity,
              RFSCOUT_STORAGE_LOG_DIRECTORY "/rfscout-%02u.log",
              a_slot % RFSCOUT_LOG_SEGMENT_COUNT );
}

/** @brief Read the monotonic sequence marker from an existing log segment. */
static bool read_sequence( unsigned a_slot, uint32_t *a_sequence )
{
    char path[ 64 ];
    segment_path( a_slot, path, sizeof( path ) );
    FILE *file = fopen( path, "r" );
    if( NULL == file )
    {
        return false;
    }

    char line[ 64 ];
    const bool read_ok = ( NULL != fgets( line, sizeof( line ), file ) );
    fclose( file );
    if( !read_ok )
    {
        return false;
    }

    unsigned long sequence = 0;
    if( 1 != sscanf( line, LOG_SEQUENCE_PREFIX "%lu", &sequence ) )
    {
        return false;
    }

    *a_sequence = (uint32_t)sequence;
    return true;
}

/** @brief Locate the newest valid segment after boot without persistent index state. */
static void find_next_segment( void )
{
    bool found = false;
    uint32_t newest_sequence = 0;
    unsigned newest_slot = 0;

    for( unsigned slot = 0; slot < RFSCOUT_LOG_SEGMENT_COUNT; ++slot )
    {
        uint32_t sequence = 0;
        if( !read_sequence( slot, &sequence ) )
        {
            continue;
        }

        if( !found || ( (int32_t)( sequence - newest_sequence ) > 0 ) )
        {
            found = true;
            newest_sequence = sequence;
            newest_slot = slot;
        }
    }

    active_sequence = found ? newest_sequence + 1U : 0U;
    active_slot = found ? ( newest_slot + 1U ) % RFSCOUT_LOG_SEGMENT_COUNT : 0U;
}

/** @brief Delete the oldest valid segment other than the active slot. */
static bool delete_oldest_segment( void )
{
    bool found = false;
    uint32_t oldest_sequence = 0;
    unsigned oldest_slot = 0;

    for( unsigned slot = 0; slot < RFSCOUT_LOG_SEGMENT_COUNT; ++slot )
    {
        if( slot == active_slot )
        {
            continue;
        }

        uint32_t sequence = 0;
        if( !read_sequence( slot, &sequence ) )
        {
            continue;
        }

        if( !found || ( (int32_t)( sequence - oldest_sequence ) < 0 ) )
        {
            found = true;
            oldest_sequence = sequence;
            oldest_slot = slot;
        }
    }

    if( !found )
    {
        return false;
    }

    char path[ 64 ];
    segment_path( oldest_slot, path, sizeof( path ) );
    return ( 0 == unlink( path ) );
}

/** @brief Preserve the configured filesystem free-space reserve when possible. */
static void enforce_free_space_reserve( void )
{
    uint64_t free_bytes = 0;
    while( ( ESP_OK == storage_get_capacity( NULL, &free_bytes ) ) &&
           ( free_bytes < RFSCOUT_LOG_FREE_RESERVE ) )
    {
        if( !delete_oldest_segment() )
        {
            ESP_LOGW( TAG, "unable to recover configured SD free-space reserve" );
            break;
        }
    }
}

/** @brief Open a fresh circular segment, replacing the slot being wrapped into. */
static bool open_segment( void )
{
    enforce_free_space_reserve();

    char path[ 64 ];
    segment_path( active_slot, path, sizeof( path ) );
    if( 0 != unlink( path ) && ( ENOENT != errno ) )
    {
        ESP_LOGW( TAG, "could not replace %s: errno=%d", path, errno );
    }

    active_file = fopen( path, "w" );
    if( NULL == active_file )
    {
        ESP_LOGE( TAG, "could not open %s: errno=%d", path, errno );
        return false;
    }

    const int header_length = fprintf( active_file,
                                       LOG_SEQUENCE_PREFIX "%lu\n",
                                       (unsigned long)active_sequence );
    if( header_length < 0 )
    {
        fclose( active_file );
        active_file = NULL;
        return false;
    }

    active_size = (size_t)header_length;
    ESP_LOGI( TAG, "logging to %s sequence=%lu", path,
              (unsigned long)active_sequence );
    return true;
}

/** @brief Close the current segment after forcing stdio data to the VFS. */
static void close_segment( void )
{
    if( NULL == active_file )
    {
        return;
    }

    fflush( active_file );
    fsync( fileno( active_file ) );
    fclose( active_file );
    active_file = NULL;
    active_size = 0U;
}

/** @brief Advance the two-digit circular file ring. */
static bool rotate_segment( void )
{
    close_segment();
    active_slot = ( active_slot + 1U ) % RFSCOUT_LOG_SEGMENT_COUNT;
    ++active_sequence;
    return open_segment();
}

/** @brief Convert the evidence bitmap to a compact stable textual representation. */
static void format_evidence( uint8_t a_evidence, char *a_output, size_t a_capacity )
{
    struct evidence_name
    {
        uint8_t bit;
        const char *name;
    };
    static const struct evidence_name names[] =
    {
        { SURVEILLANCE_EVIDENCE_OUI, "OUI" },
        { SURVEILLANCE_EVIDENCE_NAME, "NAME" },
        { SURVEILLANCE_EVIDENCE_COMPANY_ID, "COMPANY" },
        { SURVEILLANCE_EVIDENCE_SERVICE_UUID, "SERVICE" },
        { SURVEILLANCE_EVIDENCE_REMOTE_ID, "REMOTEID" },
        { SURVEILLANCE_EVIDENCE_PAYLOAD, "PAYLOAD" }
    };

    a_output[ 0 ] = '\0';
    for( size_t i = 0; i < sizeof( names ) / sizeof( names[ 0 ] ); ++i )
    {
        if( 0U == ( a_evidence & names[ i ].bit ) )
        {
            continue;
        }

        if( '\0' != a_output[ 0 ] )
        {
            strlcat( a_output, "|", a_capacity );
        }
        strlcat( a_output, names[ i ].name, a_capacity );
    }

    if( '\0' == a_output[ 0 ] )
    {
        strlcpy( a_output, "-", a_capacity );
    }
}

/** @brief Serialize one queued observation into the active text segment. */
static bool write_record( const log_record_t *a_record )
{
    if( ( NULL == active_file ) && !open_segment() )
    {
        return false;
    }

    char evidence[ 64 ];
    format_evidence( a_record->evidence, evidence, sizeof( evidence ) );
    const char *source = ( RF_SOURCE_WIFI == a_record->source ) ? "WIFI" :
                         ( RF_SOURCE_BLE == a_record->source ) ? "BLE" : "RF";
    const char *identity = ( '\0' != a_record->vendor[ 0 ] ) ? a_record->vendor :
                           ( '\0' != a_record->name[ 0 ] ) ? a_record->name : "UNKNOWN";
    const char *signature = ( SURVEILLANCE_SIGNATURE_NONE != a_record->signature ) ?
                            surveillance_signature_label( a_record->signature ) : "-";

    char line[ LOG_LINE_MAX ];
    const int length = snprintf(
        line,
        sizeof( line ),
        "%lu %s rssi=%d ch=%u mac=%02X:%02X:%02X:%02X:%02X:%02X identity=\"%s\" surveillance=%s score=%u evidence=%s\n",
        (unsigned long)a_record->timestamp_ms,
        source,
        a_record->rssi,
        (unsigned)a_record->channel,
        a_record->address[ 0 ], a_record->address[ 1 ], a_record->address[ 2 ],
        a_record->address[ 3 ], a_record->address[ 4 ], a_record->address[ 5 ],
        identity,
        signature,
        (unsigned)a_record->score,
        evidence );
    if( ( length <= 0 ) || ( (size_t)length >= sizeof( line ) ) )
    {
        return false;
    }

    if( ( active_size + (size_t)length ) > RFSCOUT_LOG_SEGMENT_SIZE )
    {
        if( !rotate_segment() )
        {
            return false;
        }
    }

    if( (size_t)length != fwrite( line, 1U, (size_t)length, active_file ) )
    {
        ESP_LOGE( TAG, "SD log write failed: errno=%d", errno );
        return false;
    }

    active_size += (size_t)length;
    return true;
}

/** @brief Low-priority filesystem worker that isolates SD latency from RF callbacks. */
static void writer_task_main( void *a_argument )
{
    (void)a_argument;
    int64_t last_flush_us = esp_timer_get_time();
    log_record_t record;

    while( !shutdown_requested || ( 0U != uxQueueMessagesWaiting( queue ) ) )
    {
        if( pdTRUE == xQueueReceive( queue, &record, pdMS_TO_TICKS( 100 ) ) )
        {
            if( !write_record( &record ) )
            {
                ++dropped_records;
            }
        }

        const int64_t now_us = esp_timer_get_time();
        if( ( NULL != active_file ) &&
            ( ( now_us - last_flush_us ) >= LOG_FLUSH_INTERVAL_US ) )
        {
            fflush( active_file );
            last_flush_us = now_us;
        }
    }

    close_segment();
    writer_stopped = true;
    writer_task = NULL;
    vTaskDelete( NULL );
}

esp_err_t log_writer_init( void )
{
    if( !storage_is_available() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    if( ( 0 != mkdir( RFSCOUT_STORAGE_LOG_DIRECTORY, 0775 ) ) &&
        ( EEXIST != errno ) )
    {
        ESP_LOGE( TAG, "could not create log directory: errno=%d", errno );
        return ESP_FAIL;
    }

    find_next_segment();
    queue = xQueueCreateStatic( LOG_QUEUE_DEPTH,
                                sizeof( log_record_t ),
                                queue_storage,
                                &queue_control );
    if( NULL == queue )
    {
        return ESP_ERR_NO_MEM;
    }

    shutdown_requested = false;
    writer_stopped = false;
    dropped_records = 0U;
    accepting = true;
    if( pdPASS != xTaskCreate( writer_task_main,
                               "sd_log",
                               LOG_TASK_STACK_SIZE,
                               NULL,
                               LOG_TASK_PRIORITY,
                               &writer_task ) )
    {
        accepting = false;
        queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool log_writer_observation( const rf_device_t *a_observation )
{
    if( !accepting || ( NULL == queue ) || ( NULL == a_observation ) )
    {
        return false;
    }

    log_record_t record =
    {
        .timestamp_ms = (uint32_t)( esp_timer_get_time() / 1000ULL ),
        .source = a_observation->last_source,
        .rssi = a_observation->rssi,
        .channel = a_observation->channel,
        .signature = a_observation->surveillance_signature,
        .score = a_observation->surveillance_score,
        .evidence = a_observation->surveillance_evidence
    };
    memcpy( record.address, a_observation->address, sizeof( record.address ) );
    strlcpy( record.vendor, a_observation->vendor, sizeof( record.vendor ) );
    if( a_observation->has_name )
    {
        strlcpy( record.name, a_observation->name, sizeof( record.name ) );
    }

    if( pdTRUE != xQueueSend( queue, &record, 0 ) )
    {
        ++dropped_records;
        return false;
    }

    return true;
}

void log_writer_shutdown( uint32_t a_timeout_ms )
{
    accepting = false;
    shutdown_requested = true;

    const TickType_t delay = pdMS_TO_TICKS( LOG_SHUTDOWN_POLL_MS );
    uint32_t waited_ms = 0U;
    while( !writer_stopped && ( waited_ms < a_timeout_ms ) )
    {
        vTaskDelay( delay );
        waited_ms += LOG_SHUTDOWN_POLL_MS;
    }

    if( !writer_stopped )
    {
        ESP_LOGW( TAG, "log shutdown timed out with records still pending" );
    }
}

uint32_t log_writer_dropped_count( void )
{
    return dropped_records;
}
