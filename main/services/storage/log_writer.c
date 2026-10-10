/**
    log_writer.c : RFScout asynchronous SD log writer
    Description: Builds JSON-line records and commits bounded bursts to SD.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "log_writer.h"

#include "device_time.h"
#include "gps.h"
#include "media_store.h"
#include "storage_internal.h"
#include "memory_manager.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define LOG_BUFFER_INITIAL 512U
#define LOG_BUFFER_MAX 4096U
#define LOG_FLUSH_THRESHOLD 3328U
#define LOG_RECORD_MAX 768U
#define LOG_WRITE_CHUNK 512U
#define LOG_BATCH_INTERVAL_US 10000000LL
#define LOG_TIMESTAMP_MAX 32U
#define LOG_TASK_STACK_SIZE 4096U
#define LOG_TASK_PRIORITY 2U
#define LOG_BATCH_POLL_MS 100U
#define LOG_SHUTDOWN_POLL_MS 20U
#define LOG_SEQUENCE_PREFIX "{\"type\":\"sequence\",\"sequence\":"
#define LOG_COLLECTION "logs"
#define LOG_SEGMENT_NAME_MAX 24U
#define LOG_HEADER_MAX 256U

static const char *TAG = "log_writer";
static SemaphoreHandle_t buffer_lock;
static TaskHandle_t writer_task;
static char *buffer;
static size_t buffer_used;
static size_t buffer_capacity;
static size_t record_start;
static int64_t buffer_started_us;
static rfscout_media_collection_t log_collection;
static rfscout_media_file_t active_file;
static bool active_open;
static uint32_t active_sequence;
static unsigned active_slot;
static size_t active_size;
static uint32_t active_media_generation;
static volatile bool accepting;
static volatile bool shutdown_requested;
static volatile bool writer_stopped;
static volatile bool io_error;
static volatile bool flush_requested;

static const rfscout_memory_owner_t log_owner = {
    .owner_class = RFSCOUT_MEMORY_OWNER_PLATFORM,
};

static void mark_io_error( const char *a_operation, esp_err_t a_result )
{
    io_error = true;
    ESP_LOGE( TAG, "%s failed: %s", a_operation, esp_err_to_name( a_result ) );
}

static void segment_name( unsigned a_slot, char *a_name, size_t a_capacity )
{
    snprintf( a_name, a_capacity, "rfscout-%02u.log", a_slot % RFSCOUT_LOG_SEGMENT_COUNT );
}

static bool read_sequence( unsigned a_slot, uint32_t *a_sequence )
{
    char name[ LOG_SEGMENT_NAME_MAX ];
    segment_name( a_slot, name, sizeof( name ) );
    rfscout_media_file_t file;
    if( ESP_OK != media_open( &log_collection, name, RFSCOUT_MEDIA_OPEN_READ, &file ) )
    {
        return false;
    }

    char line[ 64 ];
    size_t length = 0U;
    const bool read_ok =
        ( ESP_OK == media_read( &file, line, sizeof( line ) - 1U, &length ) ) && ( length > 0U );
    (void)media_close( &file );
    if( !read_ok )
    {
        return false;
    }
    line[ length ] = '\0';

    unsigned long sequence = 0;
    if( 1 != sscanf( line, LOG_SEQUENCE_PREFIX "%lu}", &sequence ) )
    {
        return false;
    }
    *a_sequence = (uint32_t)sequence;
    return true;
}

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
    active_size = 0U;
}

static bool delete_oldest_segment( void )
{
    bool found = false;
    uint32_t oldest_sequence = 0;
    unsigned oldest_slot = 0;
    for( unsigned slot = 0U; slot < RFSCOUT_LOG_SEGMENT_COUNT; ++slot )
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

    char name[ LOG_SEGMENT_NAME_MAX ];
    segment_name( oldest_slot, name, sizeof( name ) );
    const esp_err_t result = media_remove( &log_collection, name );
    if( ESP_OK != result )
    {
        mark_io_error( "oldest log deletion", result );
        return false;
    }
    return true;
}

static void enforce_free_space_reserve( void )
{
    uint64_t free_bytes = 0;
    while( ( ESP_OK == media_store_space( NULL, &free_bytes ) ) &&
           ( free_bytes < RFSCOUT_LOG_FREE_RESERVE ) )
    {
        if( !delete_oldest_segment() )
        {
            ESP_LOGW( TAG, "unable to recover configured SD free-space reserve" );
            break;
        }
    }
}

static void advance_segment( void )
{
    active_slot = ( active_slot + 1U ) % RFSCOUT_LOG_SEGMENT_COUNT;
    ++active_sequence;
    active_size = 0U;
}

/** @brief Write one formatted header line to the active segment. */
static bool write_header_line( const char *a_line, int a_length, const char *a_operation )
{
    if( a_length < 0 || (size_t)a_length >= LOG_HEADER_MAX )
    {
        io_error = true;
        ESP_LOGE( TAG, "%s exceeded header capacity", a_operation );
        return false;
    }
    const esp_err_t result = media_write( &active_file, a_line, (size_t)a_length );
    if( ESP_OK != result )
    {
        mark_io_error( a_operation, result );
        return false;
    }
    active_size += (size_t)a_length;
    return true;
}

static bool open_segment( void )
{
    const uint32_t generation = media_store_generation();
    if( generation != active_media_generation )
    {
        /* Media replacement invalidates the old handle and all per-volume segment state.
           Rebuild from the newly mounted card before any append. */
        active_open = false;
        memset( &active_file, 0, sizeof( active_file ) );
        find_next_segment();
        active_media_generation = generation;
        ESP_LOGI( TAG, "SD media generation changed; log segment state rebuilt" );
    }
    if( active_open )
    {
        return true;
    }

    char name[ LOG_SEGMENT_NAME_MAX ];
    segment_name( active_slot, name, sizeof( name ) );

    if( active_size > 0U )
    {
        rfscout_media_entry_t entry;
        const esp_err_t stat_result = media_stat( &log_collection, name, &entry );
        if( ESP_OK != stat_result )
        {
            mark_io_error( "log size query", stat_result );
            return false;
        }
        active_size = (size_t)entry.size_bytes;
    }

    if( active_size >= RFSCOUT_LOG_SEGMENT_SIZE )
    {
        advance_segment();
        segment_name( active_slot, name, sizeof( name ) );
    }

    if( 0U == active_size )
    {
        const esp_err_t remove_result = media_remove( &log_collection, name );
        if( ( ESP_OK != remove_result ) && ( ESP_ERR_NOT_FOUND != remove_result ) )
        {
            mark_io_error( "log replacement", remove_result );
            return false;
        }
        enforce_free_space_reserve();
        const esp_err_t open_result =
            media_open( &log_collection, name, RFSCOUT_MEDIA_OPEN_REPLACE, &active_file );
        if( ESP_OK != open_result )
        {
            mark_io_error( "log open", open_result );
            return false;
        }
        active_open = true;

        char header[ LOG_HEADER_MAX ];
        int length = snprintf( header,
                               sizeof( header ),
                               LOG_SEQUENCE_PREFIX "%lu}\n",
                               (unsigned long)active_sequence );
        if( !write_header_line( header, length, "log header write" ) )
        {
            (void)media_close( &active_file );
            active_open = false;
            return false;
        }

        const esp_app_desc_t *app = esp_app_get_description();
        length = snprintf( header,
                           sizeof( header ),
                           "{\"type\":\"version\",\"version\":\"%s\","
                           "\"project\":\"%s\",\"idf\":\"%s\","
                           "\"build_date\":\"%s\",\"build_time\":\"%s\"}\n",
                           app->version,
                           app->project_name,
                           app->idf_ver,
                           app->date,
                           app->time );
        if( !write_header_line( header, length, "log version header write" ) )
        {
            (void)media_close( &active_file );
            active_open = false;
            return false;
        }
        ESP_LOGI( TAG,
                  "logging to %s/%s sequence=%lu version=%s",
                  LOG_COLLECTION,
                  name,
                  (unsigned long)active_sequence,
                  app->version );
        return true;
    }

    const esp_err_t append_result =
        media_open( &log_collection, name, RFSCOUT_MEDIA_OPEN_APPEND, &active_file );
    if( ESP_OK != append_result )
    {
        mark_io_error( "log append open", append_result );
        return false;
    }
    active_open = true;
    return true;
}

/* Keep generation inspection, segment selection and file operations within
   one storage transaction. Media operations recursively acquire this same
   arbitration, so a recovery cannot replace the volume between them. */
static bool open_segment_serialized( void )
{
    if( !storage_io_acquire( RFSCOUT_STORAGE_WAIT_FOREVER ) )
    {
        return false;
    }
    const bool result = open_segment_serialized();
    storage_io_release();
    return result;
}

static bool close_segment( void )
{
    if( !active_open )
    {
        return true;
    }

    bool success = true;
    const esp_err_t sync_result = media_sync( &active_file );
    if( ESP_OK != sync_result )
    {
        mark_io_error( "log sync", sync_result );
        success = false;
    }
    const esp_err_t close_result = media_close( &active_file );
    if( ESP_OK != close_result )
    {
        mark_io_error( "log close", close_result );
        success = false;
    }
    active_open = false;
    return success;
}

static bool ensure_capacity( size_t a_required )
{
    if( a_required <= buffer_capacity )
    {
        return true;
    }
    if( a_required > LOG_BUFFER_MAX )
    {
        return false;
    }

    size_t capacity = ( 0U == buffer_capacity ) ? LOG_BUFFER_INITIAL : buffer_capacity;
    while( ( capacity < a_required ) && ( capacity < LOG_BUFFER_MAX ) )
    {
        capacity *= 2U;
    }
    if( capacity > LOG_BUFFER_MAX )
    {
        capacity = LOG_BUFFER_MAX;
    }

    char *replacement = memory_manager_resize( log_owner, buffer, capacity, 1U );
    if( NULL == replacement )
    {
        flush_requested = true;
        return false;
    }
    buffer = replacement;
    buffer_capacity = capacity;
    return true;
}

static bool append_bytes( rfscout_log_record_t *a_record, const char *a_data, size_t a_length )
{
    if( ( NULL == a_record ) || !a_record->active ||
        ( ( buffer_used - record_start ) + a_length > LOG_RECORD_MAX ) ||
        !ensure_capacity( buffer_used + a_length ) )
    {
        return false;
    }
    memcpy( &buffer[ buffer_used ], a_data, a_length );
    buffer_used += a_length;
    return true;
}

static bool append_char( rfscout_log_record_t *a_record, char a_value )
{
    return append_bytes( a_record, &a_value, 1U );
}

static size_t valid_utf8_length( const unsigned char *a_text )
{
    const unsigned char first = a_text[ 0 ];
    if( first < 0x80U )
    {
        return 1U;
    }
    if( ( first >= 0xC2U ) && ( first <= 0xDFU ) && ( a_text[ 1 ] >= 0x80U ) &&
        ( a_text[ 1 ] <= 0xBFU ) )
    {
        return 2U;
    }
    if( ( first >= 0xE0U ) && ( first <= 0xEFU ) && ( 0U != a_text[ 1 ] ) &&
        ( 0U != a_text[ 2 ] ) && ( a_text[ 2 ] >= 0x80U ) && ( a_text[ 2 ] <= 0xBFU ) )
    {
        const bool second_valid =
            ( 0xE0U == first )   ? ( ( a_text[ 1 ] >= 0xA0U ) && ( a_text[ 1 ] <= 0xBFU ) )
            : ( 0xEDU == first ) ? ( ( a_text[ 1 ] >= 0x80U ) && ( a_text[ 1 ] <= 0x9FU ) )
                                 : ( ( a_text[ 1 ] >= 0x80U ) && ( a_text[ 1 ] <= 0xBFU ) );
        return second_valid ? 3U : 0U;
    }
    if( ( first >= 0xF0U ) && ( first <= 0xF4U ) && ( 0U != a_text[ 1 ] ) &&
        ( 0U != a_text[ 2 ] ) && ( 0U != a_text[ 3 ] ) && ( a_text[ 2 ] >= 0x80U ) &&
        ( a_text[ 2 ] <= 0xBFU ) && ( a_text[ 3 ] >= 0x80U ) && ( a_text[ 3 ] <= 0xBFU ) )
    {
        const bool second_valid =
            ( 0xF0U == first )   ? ( ( a_text[ 1 ] >= 0x90U ) && ( a_text[ 1 ] <= 0xBFU ) )
            : ( 0xF4U == first ) ? ( ( a_text[ 1 ] >= 0x80U ) && ( a_text[ 1 ] <= 0x8FU ) )
                                 : ( ( a_text[ 1 ] >= 0x80U ) && ( a_text[ 1 ] <= 0xBFU ) );
        return second_valid ? 4U : 0U;
    }
    return 0U;
}

static bool append_json_string( rfscout_log_record_t *a_record, const char *a_value )
{
    if( !append_char( a_record, '"' ) )
    {
        return false;
    }

    for( size_t i = 0U; NULL != a_value && '\0' != a_value[ i ]; ++i )
    {
        const unsigned char value = (unsigned char)a_value[ i ];
        char escaped[ 7 ];
        const char *text;
        size_t length;
        if( '"' == value )
        {
            text = "\\\"";
            length = 2U;
        }
        else if( '\\' == value )
        {
            text = "\\\\";
            length = 2U;
        }
        else if( '\b' == value )
        {
            text = "\\b";
            length = 2U;
        }
        else if( '\f' == value )
        {
            text = "\\f";
            length = 2U;
        }
        else if( '\n' == value )
        {
            text = "\\n";
            length = 2U;
        }
        else if( '\r' == value )
        {
            text = "\\r";
            length = 2U;
        }
        else if( '\t' == value )
        {
            text = "\\t";
            length = 2U;
        }
        else if( value < 0x20U )
        {
            snprintf( escaped, sizeof( escaped ), "\\u%04X", value );
            text = escaped;
            length = 6U;
        }
        else if( value >= 0x80U )
        {
            length = valid_utf8_length( (const unsigned char *)&a_value[ i ] );
            if( 0U == length )
            {
                text = "\\uFFFD";
                length = 6U;
            }
            else
            {
                text = &a_value[ i ];
                i += length - 1U;
            }
        }
        else
        {
            escaped[ 0 ] = (char)value;
            text = escaped;
            length = 1U;
        }
        if( !append_bytes( a_record, text, length ) )
        {
            return false;
        }
    }
    return append_char( a_record, '"' );
}

static bool append_field_name( rfscout_log_record_t *a_record, const char *a_key )
{
    if( ( NULL == a_key ) || ( '\0' == a_key[ 0 ] ) )
    {
        return false;
    }
    if( !a_record->first_field && !append_char( a_record, ',' ) )
    {
        return false;
    }
    a_record->first_field = false;
    return append_json_string( a_record, a_key ) && append_char( a_record, ':' );
}

static bool append_number( rfscout_log_record_t *a_record, const char *a_key, const char *a_value )
{
    return append_field_name( a_record, a_key ) &&
           append_bytes( a_record, a_value, strlen( a_value ) );
}

static void abort_record( rfscout_log_record_t *a_record )
{
    if( ( NULL == a_record ) || !a_record->active )
    {
        return;
    }
    buffer_used = record_start;
    a_record->active = false;
    flush_requested = true;
    xSemaphoreGive( buffer_lock );
}

bool log_begin( rfscout_log_record_t *a_record, const char *a_type )
{
    if( ( NULL == a_record ) || ( NULL == a_type ) || ( '\0' == a_type[ 0 ] ) || !accepting ||
        ( NULL == buffer_lock ) )
    {
        return false;
    }

    xSemaphoreTake( buffer_lock, portMAX_DELAY );
    if( !accepting )
    {
        xSemaphoreGive( buffer_lock );
        return false;
    }

    record_start = buffer_used;
    *a_record = ( rfscout_log_record_t ){
        .first_field = true,
        .active = true,
    };

    if( !append_char( a_record, '{' ) || !log_str( a_record, "type", a_type ) )
    {
        abort_record( a_record );
        return false;
    }

    time_t wall_time = 0;
    char timestamp[ LOG_TIMESTAMP_MAX ];
    if( device_time_now( &wall_time ) &&
        device_time_format_iso8601_utc( wall_time, timestamp, sizeof( timestamp ) ) )
    {
        if( !log_str( a_record, "time", timestamp ) )
        {
            abort_record( a_record );
            return false;
        }
    }
    else if( !log_uint( a_record, "uptime_ms", (uint64_t)( esp_timer_get_time() / 1000ULL ) ) )
    {
        abort_record( a_record );
        return false;
    }

    rfscout_gps_snapshot_t gps;
    if( gps_snapshot_get( &gps ) && gps.location_valid &&
        ( !log_double( a_record, "lat", gps.latitude_deg ) ||
          !log_double( a_record, "lon", gps.longitude_deg ) ) )
    {
        abort_record( a_record );
        return false;
    }
    return true;
}

bool log_str( rfscout_log_record_t *a_record, const char *a_key, const char *a_value )
{
    if( ( NULL == a_value ) || ( '\0' == a_value[ 0 ] ) )
    {
        return true;
    }
    if( !append_field_name( a_record, a_key ) || !append_json_string( a_record, a_value ) )
    {
        abort_record( a_record );
        return false;
    }
    return true;
}

bool log_int( rfscout_log_record_t *a_record, const char *a_key, int64_t a_value )
{
    char value[ 24 ];
    snprintf( value, sizeof( value ), "%lld", (long long)a_value );
    if( !append_number( a_record, a_key, value ) )
    {
        abort_record( a_record );
        return false;
    }
    return true;
}

bool log_uint( rfscout_log_record_t *a_record, const char *a_key, uint64_t a_value )
{
    char value[ 24 ];
    snprintf( value, sizeof( value ), "%llu", (unsigned long long)a_value );
    if( !append_number( a_record, a_key, value ) )
    {
        abort_record( a_record );
        return false;
    }
    return true;
}

bool log_bool( rfscout_log_record_t *a_record, const char *a_key, bool a_value )
{
    if( !append_field_name( a_record, a_key ) ||
        !append_bytes( a_record, a_value ? "true" : "false", a_value ? 4U : 5U ) )
    {
        abort_record( a_record );
        return false;
    }
    return true;
}

bool log_double( rfscout_log_record_t *a_record, const char *a_key, double a_value )
{
    if( !isfinite( a_value ) )
    {
        abort_record( a_record );
        return false;
    }

    char value[ 32 ];
    const int length = snprintf( value, sizeof( value ), "%.6f", a_value );
    if( ( length <= 0 ) || ( (size_t)length >= sizeof( value ) ) ||
        !append_number( a_record, a_key, value ) )
    {
        abort_record( a_record );
        return false;
    }
    return true;
}

bool log_hex( rfscout_log_record_t *a_record,
              const char *a_key,
              const void *a_data,
              size_t a_length )
{
    if( ( NULL == a_data ) || ( 0U == a_length ) )
    {
        return true;
    }
    if( !append_field_name( a_record, a_key ) || !append_char( a_record, '"' ) )
    {
        abort_record( a_record );
        return false;
    }

    static const char digits[] = "0123456789ABCDEF";
    const uint8_t *data = a_data;
    for( size_t i = 0U; i < a_length; ++i )
    {
        const char pair[ 2 ] = { digits[ data[ i ] >> 4U ], digits[ data[ i ] & 0x0FU ] };
        if( !append_bytes( a_record, pair, sizeof( pair ) ) )
        {
            abort_record( a_record );
            return false;
        }
    }
    if( !append_char( a_record, '"' ) )
    {
        abort_record( a_record );
        return false;
    }
    return true;
}

bool log_end( rfscout_log_record_t *a_record )
{
    if( ( NULL == a_record ) || !a_record->active )
    {
        return false;
    }
    if( !append_bytes( a_record, "}\n", 2U ) )
    {
        abort_record( a_record );
        return false;
    }

    if( 0 == buffer_started_us )
    {
        buffer_started_us = esp_timer_get_time();
    }
    if( buffer_used >= LOG_FLUSH_THRESHOLD )
    {
        flush_requested = true;
    }
    a_record->active = false;
    xSemaphoreGive( buffer_lock );
    return true;
}

static void consume_buffer_prefix( size_t a_length )
{
    xSemaphoreTake( buffer_lock, portMAX_DELAY );
    if( a_length > buffer_used )
    {
        a_length = buffer_used;
    }
    buffer_used -= a_length;
    if( buffer_used > 0U )
    {
        memmove( buffer, &buffer[ a_length ], buffer_used );
    }
    xSemaphoreGive( buffer_lock );
}

static bool flush_buffer( void )
{
    xSemaphoreTake( buffer_lock, portMAX_DELAY );
    const size_t burst_size = buffer_used;
    if( 0U == burst_size )
    {
        flush_requested = false;
        buffer_started_us = 0;
        xSemaphoreGive( buffer_lock );
        return true;
    }
    flush_requested = false;
    xSemaphoreGive( buffer_lock );

    bool success = open_segment_serialized();
    if( success && ( active_size + burst_size > RFSCOUT_LOG_SEGMENT_SIZE ) )
    {
        success = close_segment();
        if( success )
        {
            advance_segment();
            success = open_segment_serialized();
        }
    }

    size_t remaining = burst_size;
    char chunk[ LOG_WRITE_CHUNK ];
    while( success && ( remaining > 0U ) )
    {
        xSemaphoreTake( buffer_lock, portMAX_DELAY );
        size_t chunk_size = remaining;
        if( chunk_size > sizeof( chunk ) )
        {
            chunk_size = sizeof( chunk );
        }
        if( chunk_size > buffer_used )
        {
            chunk_size = buffer_used;
        }
        memcpy( chunk, buffer, chunk_size );
        xSemaphoreGive( buffer_lock );

        if( 0U == chunk_size )
        {
            break;
        }
        const esp_err_t write_result = media_write( &active_file, chunk, chunk_size );
        if( ESP_OK != write_result )
        {
            mark_io_error( "SD log write", write_result );
            success = false;
            break;
        }
        active_size += chunk_size;
        remaining -= chunk_size;
        consume_buffer_prefix( chunk_size );
    }

    if( !close_segment() )
    {
        success = false;
    }

    xSemaphoreTake( buffer_lock, portMAX_DELAY );
    if( 0U == buffer_used )
    {
        buffer_started_us = 0;
        flush_requested = false;
    }
    else if( success )
    {
        buffer_started_us = esp_timer_get_time();
        flush_requested = buffer_used >= LOG_FLUSH_THRESHOLD;
    }
    else
    {
        flush_requested = true;
        if( 0 == buffer_started_us )
        {
            buffer_started_us = esp_timer_get_time();
        }
    }
    xSemaphoreGive( buffer_lock );
    return success;
}

static void writer_task_main( void *a_argument )
{
    (void)a_argument;
    while( true )
    {
        bool should_flush = shutdown_requested || flush_requested;
        if( !should_flush )
        {
            xSemaphoreTake( buffer_lock, portMAX_DELAY );
            should_flush = ( buffer_used > 0U ) && ( buffer_started_us > 0 ) &&
                           ( esp_timer_get_time() - buffer_started_us >= LOG_BATCH_INTERVAL_US );
            xSemaphoreGive( buffer_lock );
        }

        if( should_flush )
        {
            (void)flush_buffer();
            xSemaphoreTake( buffer_lock, portMAX_DELAY );
            const bool empty = 0U == buffer_used;
            xSemaphoreGive( buffer_lock );
            if( shutdown_requested && empty )
            {
                break;
            }
        }
        else if( shutdown_requested )
        {
            break;
        }

        vTaskDelay( pdMS_TO_TICKS( LOG_BATCH_POLL_MS ) );
    }

    writer_stopped = true;
    writer_task = NULL;
    vTaskDelete( NULL );
}

esp_err_t log_writer_init( void )
{
    if( accepting && ( NULL != writer_task ) )
    {
        return ESP_OK;
    }

    io_error = false;
    active_open = false;
    if( !media_store_available() )
    {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t collection_result = media_system_collection( LOG_COLLECTION, &log_collection );
    if( ESP_OK != collection_result )
    {
        return collection_result;
    }
    if( NULL == buffer_lock )
    {
        buffer_lock = xSemaphoreCreateMutex();
        if( NULL == buffer_lock )
        {
            return ESP_ERR_NO_MEM;
        }
    }
    if( !ensure_capacity( LOG_BUFFER_INITIAL ) )
    {
        return ESP_ERR_NO_MEM;
    }

    find_next_segment();
    active_media_generation = media_store_generation();
    buffer_used = 0U;
    record_start = 0U;
    buffer_started_us = 0;
    shutdown_requested = false;
    writer_stopped = false;
    flush_requested = false;
    accepting = true;
    if( pdPASS != xTaskCreate( writer_task_main,
                               "sd_log",
                               LOG_TASK_STACK_SIZE,
                               NULL,
                               LOG_TASK_PRIORITY,
                               &writer_task ) )
    {
        accepting = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
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
    flush_requested = true;
    const TickType_t delay = pdMS_TO_TICKS( LOG_SHUTDOWN_POLL_MS );
    uint32_t waited_ms = 0U;
    while( !writer_stopped && ( waited_ms < a_timeout_ms ) )
    {
        vTaskDelay( delay );
        waited_ms += LOG_SHUTDOWN_POLL_MS;
    }
    if( writer_stopped )
    {
        return true;
    }
    shutdown_requested = false;
    accepting = true;
    ESP_LOGW( TAG, "log shutdown timed out; storage remains mounted and logging resumes" );
    return false;
}

bool log_writer_has_error( void )
{
    return io_error;
}
