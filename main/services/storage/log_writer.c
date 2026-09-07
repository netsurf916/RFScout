/**
    log_writer.c : RFScout asynchronous SD log writer
    Description: Writes device and application records to a bounded circular text log.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "log_writer.h"

#include "device_time.h"
#include "gps.h"
#include "storage.h"
#include "surveillance.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LOG_QUEUE_DEPTH 128U
#define LOG_LINE_MAX 448U
#define LOG_IDENTITY_MAX 96U
#define LOG_APP_NAME_MAX 16U
#define LOG_APP_DETAIL_MAX 160U
#define LOG_TIMESTAMP_MAX 32U
#define LOG_LOCATION_MAX 72U
#define LOG_TASK_STACK_SIZE 4096U
#define LOG_TASK_PRIORITY 2U
#define LOG_FLUSH_INTERVAL_US 1000000LL
#define LOG_SHUTDOWN_POLL_MS 20U
#define LOG_SEQUENCE_PREFIX "# RFSCOUT LOG SEQUENCE "

typedef enum
{
    LOG_RECORD_DEVICE = 0,
    LOG_RECORD_APP
} log_record_type_t;

typedef struct
{
    time_t wall_time;
    uint32_t uptime_ms;
    bool wall_time_valid;
    bool location_valid;
    double latitude_deg;
    double longitude_deg;
    log_record_type_t type;
    observation_store_event_t event;
    rf_source_t source;
    uint8_t address[ 6 ];
    int8_t rssi;
    uint16_t channel;
    surveillance_signature_id_t signature;
    uint8_t evidence;
    char vendor[ RFSCOUT_VENDOR_NAME_MAX ];
    char name[ RFSCOUT_DEVICE_NAME_MAX ];
    char app[ LOG_APP_NAME_MAX ];
    char detail[ LOG_APP_DETAIL_MAX ];
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
static volatile bool io_error;
static uint32_t dropped_records;

/** @brief Mark the logging subsystem unhealthy after a filesystem operation fails. */
static void mark_io_error( const char *a_operation )
{
    io_error = true;
    ESP_LOGE( TAG, "%s failed: errno=%d", a_operation, errno );
}

/** @brief Construct the path for one bounded log-ring slot. */
static void segment_path( unsigned a_slot, char *a_path, size_t a_capacity )
{
    snprintf( a_path,
              a_capacity,
              RFSCOUT_STORAGE_LOG_DIRECTORY "/rfscout-%02u.log",
              a_slot % RFSCOUT_LOG_SEGMENT_COUNT );
}

/** @brief Read the sequence header from one existing ring slot. */
static bool read_sequence( unsigned a_slot, uint32_t *a_sequence )
{
    char path[ 64 ];
    segment_path( a_slot, path, sizeof( path ) );
    FILE *file = fopen( path, "r" );
    if( NULL == file )
        return false;
    char line[ 64 ];
    const bool read_ok = ( NULL != fgets( line, sizeof( line ), file ) );
    fclose( file );
    if( !read_ok )
        return false;
    unsigned long sequence = 0;
    if( 1 != sscanf( line, LOG_SEQUENCE_PREFIX "%lu", &sequence ) )
        return false;
    *a_sequence = (uint32_t)sequence;
    return true;
}

/** @brief Find the newest ring segment and select its successor. */
static void find_next_segment( void )
{
    bool found = false;
    uint32_t newest_sequence = 0;
    unsigned newest_slot = 0;
    for( unsigned slot = 0; slot < RFSCOUT_LOG_SEGMENT_COUNT; ++slot )
    {
        uint32_t sequence = 0;
        if( !read_sequence( slot, &sequence ) )
            continue;
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

/** @brief Delete the oldest inactive segment when free space is constrained. */
static bool delete_oldest_segment( void )
{
    bool found = false;
    uint32_t oldest_sequence = 0;
    unsigned oldest_slot = 0;
    for( unsigned slot = 0; slot < RFSCOUT_LOG_SEGMENT_COUNT; ++slot )
    {
        if( slot == active_slot )
            continue;
        uint32_t sequence = 0;
        if( !read_sequence( slot, &sequence ) )
            continue;
        if( !found || ( (int32_t)( sequence - oldest_sequence ) < 0 ) )
        {
            found = true;
            oldest_sequence = sequence;
            oldest_slot = slot;
        }
    }
    if( !found )
        return false;
    char path[ 64 ];
    segment_path( oldest_slot, path, sizeof( path ) );
    if( 0 != unlink( path ) )
    {
        mark_io_error( "oldest log deletion" );
        return false;
    }
    return true;
}

/** @brief Preserve the configured filesystem reserve by deleting oldest segments. */
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

/** @brief Open a fresh active segment for the current ring slot. */
static bool open_segment( void )
{
    char path[ 64 ];
    segment_path( active_slot, path, sizeof( path ) );
    if( ( 0 != unlink( path ) ) && ( ENOENT != errno ) )
    {
        mark_io_error( "log replacement" );
        return false;
    }
    enforce_free_space_reserve();
    active_file = fopen( path, "w" );
    if( NULL == active_file )
    {
        mark_io_error( "log open" );
        return false;
    }
    const int header_length =
        fprintf( active_file, LOG_SEQUENCE_PREFIX "%lu\n", (unsigned long)active_sequence );
    if( header_length < 0 )
    {
        mark_io_error( "log header write" );
        fclose( active_file );
        active_file = NULL;
        return false;
    }
    active_size = (size_t)header_length;
    ESP_LOGI( TAG, "logging to %s sequence=%lu", path, (unsigned long)active_sequence );
    return true;
}

/** @brief Flush, sync, and close the current segment. */
static void close_segment( void )
{
    if( NULL == active_file )
        return;
    if( 0 != fflush( active_file ) )
        mark_io_error( "log flush" );
    if( 0 != fsync( fileno( active_file ) ) )
        mark_io_error( "log sync" );
    if( 0 != fclose( active_file ) )
        mark_io_error( "log close" );
    active_file = NULL;
    active_size = 0U;
}

/** @brief Rotate to the next bounded ring segment. */
static bool rotate_segment( void )
{
    close_segment();
    active_slot = ( active_slot + 1U ) % RFSCOUT_LOG_SEGMENT_COUNT;
    ++active_sequence;
    return open_segment();
}

/** @brief Convert evidence bits to stable pipe-separated log tokens. */
static void format_evidence( uint8_t a_evidence, char *a_output, size_t a_capacity )
{
    struct evidence_name
    {
        uint8_t bit;
        const char *name;
    };
    static const struct evidence_name names[] = { { SURVEILLANCE_EVIDENCE_OUI, "OUI" },
                                                  { SURVEILLANCE_EVIDENCE_NAME, "NAME" },
                                                  { SURVEILLANCE_EVIDENCE_COMPANY_ID, "COMPANY" },
                                                  { SURVEILLANCE_EVIDENCE_SERVICE_UUID, "SERVICE" },
                                                  { SURVEILLANCE_EVIDENCE_REMOTE_ID, "REMOTEID" },
                                                  { SURVEILLANCE_EVIDENCE_PAYLOAD, "PAYLOAD" } };
    a_output[ 0 ] = '\0';
    for( size_t i = 0; i < sizeof( names ) / sizeof( names[ 0 ] ); ++i )
    {
        if( 0U == ( a_evidence & names[ i ].bit ) )
            continue;
        if( '\0' != a_output[ 0 ] )
            strlcat( a_output, "|", a_capacity );
        strlcat( a_output, names[ i ].name, a_capacity );
    }
    if( '\0' == a_output[ 0 ] )
        strlcpy( a_output, "-", a_capacity );
}

/** @brief Escape arbitrary text for one-line log records. */
static void format_text( const char *a_input, char *a_output, size_t a_capacity )
{
    size_t output = 0U;
    if( 0U == a_capacity )
        return;
    for( size_t input = 0U; ( NULL != a_input ) && ( '\0' != a_input[ input ] ); ++input )
    {
        const unsigned char value = (unsigned char)a_input[ input ];
        const char *escape = NULL;
        char escaped[ 5 ];
        if( '"' == value )
            escape = "\\\"";
        else if( '\\' == value )
            escape = "\\\\";
        else if( '\n' == value )
            escape = "\\n";
        else if( '\r' == value )
            escape = "\\r";
        else if( '\t' == value )
            escape = "\\t";
        else if( !isprint( value ) )
        {
            snprintf( escaped, sizeof( escaped ), "\\x%02X", value );
            escape = escaped;
        }
        if( NULL != escape )
        {
            const size_t length = strlen( escape );
            if( ( output + length ) >= a_capacity )
                break;
            memcpy( &a_output[ output ], escape, length );
            output += length;
        }
        else
        {
            if( ( output + 1U ) >= a_capacity )
                break;
            a_output[ output++ ] = (char)value;
        }
    }
    a_output[ output ] = '\0';
}

/** @brief Capture wall time, monotonic fallback, and any fresh GPS fix at event creation. */
static void stamp_record( log_record_t *a_record )
{
    a_record->uptime_ms = (uint32_t)( esp_timer_get_time() / 1000ULL );
    a_record->wall_time_valid = device_time_now( &a_record->wall_time );

    rfscout_gps_snapshot_t gps;
    if( gps_snapshot_get( &gps ) && gps.location_valid )
    {
        a_record->location_valid = true;
        a_record->latitude_deg = gps.latitude_deg;
        a_record->longitude_deg = gps.longitude_deg;
    }
}

/** @brief Format a UTC wall-clock timestamp or explicit monotonic fallback. */
static void format_timestamp( const log_record_t *a_record, char *a_output, size_t a_capacity )
{
    if( a_record->wall_time_valid &&
        device_time_format_iso8601_utc( a_record->wall_time, a_output, a_capacity ) )
    {
        return;
    }
    snprintf( a_output, a_capacity, "UP+%lu", (unsigned long)a_record->uptime_ms );
}

/** @brief Format an optional event-time GPS fix as stable log key/value fields. */
static void format_location( const log_record_t *a_record, char *a_output, size_t a_capacity )
{
    if( !a_record->location_valid )
    {
        a_output[ 0 ] = '\0';
        return;
    }
    snprintf( a_output,
              a_capacity,
              " lat=%.6f lon=%.6f",
              a_record->latitude_deg,
              a_record->longitude_deg );
}

/** @brief Serialize one queued record to the active log segment. */
static bool write_record( const log_record_t *a_record )
{
    if( ( NULL == active_file ) && !open_segment() )
        return false;

    char line[ LOG_LINE_MAX ];
    char timestamp[ LOG_TIMESTAMP_MAX ];
    char location[ LOG_LOCATION_MAX ];
    format_timestamp( a_record, timestamp, sizeof( timestamp ) );
    format_location( a_record, location, sizeof( location ) );
    int length;

    if( LOG_RECORD_APP == a_record->type )
    {
        char detail[ LOG_APP_DETAIL_MAX * 2U ];
        format_text( a_record->detail, detail, sizeof( detail ) );
        length = snprintf(
            line, sizeof( line ), "%s APP %s %s%s\n", timestamp, a_record->app, detail, location );
    }
    else
    {
        const char *source = ( RF_SOURCE_WIFI == a_record->source )  ? "WIFI"
                             : ( RF_SOURCE_BLE == a_record->source ) ? "BLE"
                                                                     : "RF";
        const char *raw_identity = ( '\0' != a_record->vendor[ 0 ] ) ? a_record->vendor
                                   : ( '\0' != a_record->name[ 0 ] ) ? a_record->name
                                                                     : "UNKNOWN";
        char identity[ LOG_IDENTITY_MAX ];
        format_text( raw_identity, identity, sizeof( identity ) );
        if( ( OBSERVATION_STORE_EVENT_ADDED == a_record->event ) ||
            ( OBSERVATION_STORE_EVENT_SURVEILLANCE_CHANGED == a_record->event ) )
        {
            char evidence[ 64 ];
            format_evidence( a_record->evidence, evidence, sizeof( evidence ) );
            const char *signature = ( SURVEILLANCE_SIGNATURE_NONE != a_record->signature )
                                        ? surveillance_signature_label( a_record->signature )
                                        : "-";
            length = snprintf(
                line,
                sizeof( line ),
                "%s %s %s rssi=%d ch=%u mac=%02X:%02X:%02X:%02X:%02X:%02X "
                "identity=\"%s\" surveillance=%s evidence=%s%s\n",
                timestamp,
                ( OBSERVATION_STORE_EVENT_ADDED == a_record->event ) ? "ADDED" : "SURVEILLANCE",
                source,
                a_record->rssi,
                (unsigned)a_record->channel,
                a_record->address[ 0 ],
                a_record->address[ 1 ],
                a_record->address[ 2 ],
                a_record->address[ 3 ],
                a_record->address[ 4 ],
                a_record->address[ 5 ],
                identity,
                signature,
                evidence,
                location );
        }
        else
        {
            const char *reason = ( OBSERVATION_STORE_EVENT_DROPPED_CAPACITY == a_record->event )
                                     ? "capacity"
                                     : "timeout";
            length = snprintf(
                line,
                sizeof( line ),
                "%s DROPPED %s mac=%02X:%02X:%02X:%02X:%02X:%02X identity=\"%s\" reason=%s%s\n",
                timestamp,
                source,
                a_record->address[ 0 ],
                a_record->address[ 1 ],
                a_record->address[ 2 ],
                a_record->address[ 3 ],
                a_record->address[ 4 ],
                a_record->address[ 5 ],
                identity,
                reason,
                location );
        }
    }

    if( ( length <= 0 ) || ( (size_t)length >= sizeof( line ) ) )
    {
        io_error = true;
        return false;
    }
    if( ( active_size + (size_t)length ) > RFSCOUT_LOG_SEGMENT_SIZE && !rotate_segment() )
        return false;
    if( (size_t)length != fwrite( line, 1U, (size_t)length, active_file ) )
    {
        mark_io_error( "SD log write" );
        return false;
    }
    active_size += (size_t)length;
    return true;
}

/** @brief Enqueue one immutable log record without blocking collectors. */
static bool queue_record( const log_record_t *a_record )
{
    if( !accepting || ( NULL == queue ) || ( NULL == a_record ) )
        return false;
    if( pdTRUE == xQueueSend( queue, a_record, 0 ) )
        return true;
    ++dropped_records;
    return false;
}

/** @brief Drain queued records and own all filesystem writes in one task. */
static void writer_task_main( void *a_argument )
{
    (void)a_argument;
    int64_t last_flush_us = esp_timer_get_time();
    log_record_t record;
    while( true )
    {
        if( shutdown_requested && ( 0U == uxQueueMessagesWaiting( queue ) ) )
            break;
        if( pdTRUE == xQueueReceive( queue, &record, pdMS_TO_TICKS( 100 ) ) )
        {
            bool written = false;
            if( storage_io_acquire( RFSCOUT_STORAGE_WAIT_FOREVER ) )
            {
                written = write_record( &record );
                storage_io_release();
            }
            if( !written )
            {
                ++dropped_records;
            }
        }
        const int64_t now_us = esp_timer_get_time();
        if( ( NULL != active_file ) && ( ( now_us - last_flush_us ) >= LOG_FLUSH_INTERVAL_US ) )
        {
            if( storage_io_acquire( RFSCOUT_STORAGE_WAIT_FOREVER ) )
            {
                if( 0 != fflush( active_file ) )
                    mark_io_error( "periodic log flush" );
                storage_io_release();
            }
            else
            {
                io_error = true;
                ESP_LOGE( TAG, "could not acquire SD I/O lock for periodic log flush" );
            }
            last_flush_us = now_us;
        }
    }
    if( storage_io_acquire( RFSCOUT_STORAGE_WAIT_FOREVER ) )
    {
        close_segment();
        storage_io_release();
    }
    else
    {
        io_error = true;
        ESP_LOGE( TAG, "could not acquire SD I/O lock for log shutdown" );
    }
    writer_stopped = true;
    writer_task = NULL;
    vTaskDelete( NULL );
}

esp_err_t log_writer_init( void )
{
    io_error = false;
    if( !storage_is_available() )
        return ESP_ERR_INVALID_STATE;
    if( ( 0 != mkdir( RFSCOUT_STORAGE_LOG_DIRECTORY, 0775 ) ) && ( EEXIST != errno ) )
    {
        mark_io_error( "log directory creation" );
        return ESP_FAIL;
    }
    find_next_segment();
    queue = xQueueCreateStatic(
        LOG_QUEUE_DEPTH, sizeof( log_record_t ), queue_storage, &queue_control );
    if( NULL == queue )
        return ESP_ERR_NO_MEM;
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

bool log_writer_device_event( observation_store_event_t a_event, const rf_device_t *a_device )
{
    if( NULL == a_device )
        return false;

    log_record_t record = { .type = LOG_RECORD_DEVICE,
                            .event = a_event,
                            .source = a_device->last_source,
                            .rssi = a_device->rssi,
                            .channel = a_device->channel,
                            .signature = a_device->surveillance_signature,
                            .evidence = a_device->surveillance_evidence };
    stamp_record( &record );
    memcpy( record.address, a_device->address, sizeof( record.address ) );
    strlcpy( record.vendor, a_device->vendor, sizeof( record.vendor ) );
    if( a_device->has_name )
        strlcpy( record.name, a_device->name, sizeof( record.name ) );
    return queue_record( &record );
}

void log_writer_store_event( observation_store_event_t a_event,
                             const rf_device_t *a_device,
                             void *a_context )
{
    (void)a_context;
    (void)log_writer_device_event( a_event, a_device );
}

bool log_writer_app_record( const char *a_app, const char *a_detail )
{
    if( ( NULL == a_app ) || ( '\0' == a_app[ 0 ] ) || ( NULL == a_detail ) )
        return false;

    log_record_t record = { .type = LOG_RECORD_APP };
    stamp_record( &record );
    strlcpy( record.app, a_app, sizeof( record.app ) );
    strlcpy( record.detail, a_detail, sizeof( record.detail ) );
    return queue_record( &record );
}

bool log_writer_shutdown( uint32_t a_timeout_ms )
{
    if( writer_stopped || ( NULL == writer_task ) )
    {
        accepting = false;
        return true;
    }
    accepting = false;
    shutdown_requested = true;
    const TickType_t delay = pdMS_TO_TICKS( LOG_SHUTDOWN_POLL_MS );
    uint32_t waited_ms = 0U;
    while( !writer_stopped && ( waited_ms < a_timeout_ms ) )
    {
        vTaskDelay( delay );
        waited_ms += LOG_SHUTDOWN_POLL_MS;
    }
    if( writer_stopped )
        return true;
    shutdown_requested = false;
    accepting = true;
    ESP_LOGW( TAG, "log shutdown timed out; storage remains mounted and logging resumes" );
    return false;
}

uint32_t log_writer_dropped_count( void )
{
    return dropped_records;
}

bool log_writer_has_error( void )
{
    return io_error;
}
