/** Wardriving binary PCAPNG writer and bounded radio-to-SD queue. */
#include "capture.h"

#include "ble_monitor.h"
#include "device_time.h"
#include "gps.h"
#include "log_writer.h"
#include "media_store.h"
#include "radio_monitor.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define CAPTURE_QUEUE_COUNT 48U
#define CAPTURE_MAX_FRAME 4096U
#define CAPTURE_TASK_STACK 4096U
#define CAPTURE_SYNC_INTERVAL_US 10000000LL
#define CAPTURE_FILE_MIN ( 1024U * 1024U )
#define CAPTURE_FILE_MAX ( 128U * 1024U * 1024U )
#define CAPTURE_GPS_MAX_AGE_MS 5000U

typedef struct
{
    uint64_t timestamp_us;
    uint16_t length;
    uint8_t source;
    int8_t rssi;
    uint8_t channel;
    uint8_t address[ 6 ];
    uint8_t address_type;
    uint8_t event_type;
    bool gps_valid;
    int32_t latitude_e7;
    int32_t longitude_e7;
    uint32_t gps_age_ms;
    uint8_t payload[ CAPTURE_MAX_FRAME ];
} capture_packet_t;

typedef struct
{
    uint32_t count;
    uint32_t oldest;
    uint32_t newest;
    bool found;
} capture_files_t;

static const char *TAG = "wardrive";
static QueueHandle_t pending_queue;
static QueueHandle_t free_queue;
static capture_packet_t *packet_pool;
static uint8_t *writer_frame;
static TaskHandle_t capture_task;
static volatile bool accepting;
static volatile bool stopping;
static wardrive_config_t config;
static wardrive_stats_t stats;
static rfscout_media_collection_t collection;
static rfscout_media_file_t output;
static bool output_open;
static uint32_t next_segment;
static uint32_t current_bytes;
static portMUX_TYPE gps_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t gps_updated_us;
static bool gps_fix_valid;
static int32_t gps_latitude_e7;
static int32_t gps_longitude_e7;
static uint32_t gps_fix_age_ms;

void wardrive_capture_update_gps( void )
{
    rfscout_gps_snapshot_t gps = { 0 };
    const bool available = gps_snapshot_get( &gps ) && gps.location_valid &&
                           gps.location_age_ms <= CAPTURE_GPS_MAX_AGE_MS;
    portENTER_CRITICAL( &gps_lock );
    gps_fix_valid = available;
    if( available )
    {
        gps_latitude_e7 = (int32_t)( gps.latitude_deg * 10000000.0 );
        gps_longitude_e7 = (int32_t)( gps.longitude_deg * 10000000.0 );
        gps_fix_age_ms = gps.location_age_ms;
    }
    gps_updated_us = esp_timer_get_time();
    portEXIT_CRITICAL( &gps_lock );
}

static void le16( uint8_t *a_dst, uint16_t a_value )
{
    a_dst[ 0 ] = (uint8_t)a_value;
    a_dst[ 1 ] = (uint8_t)( a_value >> 8U );
}

static void le32( uint8_t *a_dst, uint32_t a_value )
{
    for( unsigned i = 0U; i < 4U; ++i )
    {
        a_dst[ i ] = (uint8_t)( a_value >> ( 8U * i ) );
    }
}

static void le64( uint8_t *a_dst, uint64_t a_value )
{
    le32( a_dst, (uint32_t)a_value );
    le32( a_dst + 4U, (uint32_t)( a_value >> 32U ) );
}

static bool write_bytes( const void *a_data, size_t a_length )
{
    if( ESP_OK != media_write( &output, a_data, a_length ) )
    {
        stats.storage_error = true;
        return false;
    }
    current_bytes += (uint32_t)a_length;
    stats.bytes += a_length;
    return true;
}

static bool write_section( void )
{
    uint8_t block[ 28 ] = { 0 };
    le32( block, 0x0A0D0D0AU );
    le32( block + 4U, sizeof( block ) );
    le32( block + 8U, 0x1A2B3C4DU );
    le16( block + 12U, 1U );
    le16( block + 14U, 0U );
    le64( block + 16U, UINT64_MAX );
    le32( block + 24U, sizeof( block ) );
    return write_bytes( block, sizeof( block ) );
}

static bool write_interface( uint16_t a_link_type )
{
    uint8_t block[ 20 ] = { 0 };
    le32( block, 1U );
    le32( block + 4U, sizeof( block ) );
    le16( block + 8U, a_link_type );
    le32( block + 12U, CAPTURE_MAX_FRAME + 20U );
    le32( block + 16U, sizeof( block ) );
    return write_bytes( block, sizeof( block ) );
}

static void segment_name( uint32_t a_sequence, char *a_name, size_t a_capacity )
{
    (void)snprintf( a_name, a_capacity, "drive-%08lu.pcapng", (unsigned long)a_sequence );
}

static bool visit_capture( const rfscout_media_entry_t *a_entry, void *a_context )
{
    capture_files_t *files = a_context;
    unsigned long sequence;
    char suffix;
    if( 1 != sscanf( a_entry->name, "drive-%8lu.pcapng%c", &sequence, &suffix ) )
    {
        return true;
    }
    const uint32_t id = (uint32_t)sequence;
    if( !files->found || id < files->oldest )
    {
        files->oldest = id;
    }
    if( !files->found || id > files->newest )
    {
        files->newest = id;
    }
    files->found = true;
    ++files->count;
    return true;
}

static bool prune_files( uint32_t a_allowed )
{
    for( unsigned tries = 0U; tries < 512U; ++tries )
    {
        capture_files_t files = { 0 };
        const esp_err_t result = media_list( &collection, visit_capture, &files );
        if( ESP_OK != result && ESP_ERR_NOT_FOUND != result )
        {
            return false;
        }
        if( files.count <= a_allowed )
        {
            return true;
        }
        char name[ 32 ];
        segment_name( files.oldest, name, sizeof( name ) );
        if( ESP_OK != media_remove( &collection, name ) )
        {
            return false;
        }
    }
    return false;
}

static void close_segment( void )
{
    if( output_open )
    {
        if( ESP_OK != media_sync( &output ) )
        {
            stats.storage_error = true;
        }
        if( ESP_OK != media_close( &output ) )
        {
            stats.storage_error = true;
        }
        output_open = false;
    }
}

static bool open_segment( void )
{
    if( !prune_files( config.max_files - 1U ) )
    {
        return false;
    }
    uint64_t free_bytes = 0U;
    if( ESP_OK != media_store_space( NULL, &free_bytes ) ||
        free_bytes < RFSCOUT_LOG_FREE_RESERVE + config.file_limit_bytes )
    {
        return false;
    }
    char name[ 32 ];
    for( unsigned attempts = 0U; attempts < 128U; ++attempts )
    {
        segment_name( next_segment++, name, sizeof( name ) );
        const esp_err_t result =
            media_open( &collection, name, RFSCOUT_MEDIA_OPEN_CREATE_NEW, &output );
        if( ESP_OK == result )
        {
            output_open = true;
            break;
        }
        if( ESP_ERR_NOT_ALLOWED != result )
        {
            return false;
        }
    }
    if( !output_open )
    {
        return false;
    }
    current_bytes = 0U;
    stats.files++;
    if( !write_section() || !write_interface( 127U ) || !write_interface( 201U ) )
    {
        close_segment();
        return false;
    }
    return true;
}

static uint64_t timestamp_now( void )
{
    struct timeval now;
    (void)gettimeofday( &now, NULL );
    return (uint64_t)now.tv_sec * 1000000ULL + (uint64_t)now.tv_usec;
}

static void enqueue( uint8_t a_source,
                     const uint8_t *a_data,
                     size_t a_length,
                     int8_t a_rssi,
                     uint8_t a_channel,
                     const uint8_t a_address[ 6 ],
                     uint8_t a_address_type,
                     uint8_t a_event_type )
{
    if( !accepting || NULL == free_queue || NULL == pending_queue )
    {
        return;
    }
    if( 1U == a_source )
    {
        __atomic_add_fetch( &stats.received_wifi, 1U, __ATOMIC_RELAXED );
    }
    else
    {
        __atomic_add_fetch( &stats.received_ble, 1U, __ATOMIC_RELAXED );
    }
    capture_packet_t *packet = NULL;
    if( ( NULL == a_data && a_length > 0U ) || a_length > CAPTURE_MAX_FRAME ||
        pdTRUE != xQueueReceive( free_queue, &packet, 0U ) )
    {
        __atomic_add_fetch( &stats.dropped, 1U, __ATOMIC_RELAXED );
        return;
    }
    packet->timestamp_us = timestamp_now();
    packet->length = (uint16_t)a_length;
    packet->source = a_source;
    packet->rssi = a_rssi;
    packet->channel = a_channel;
    packet->address_type = a_address_type;
    packet->event_type = a_event_type;
    const int64_t arrival_us = esp_timer_get_time();
    portENTER_CRITICAL( &gps_lock );
    const int64_t elapsed_us = arrival_us - gps_updated_us;
    packet->gps_valid =
        config.gps_metadata && gps_fix_valid && elapsed_us >= 0 && elapsed_us <= 1000000LL &&
        gps_fix_age_ms + (uint32_t)( elapsed_us / 1000LL ) <= CAPTURE_GPS_MAX_AGE_MS;
    if( packet->gps_valid )
    {
        packet->latitude_e7 = gps_latitude_e7;
        packet->longitude_e7 = gps_longitude_e7;
        packet->gps_age_ms = gps_fix_age_ms + (uint32_t)( elapsed_us / 1000LL );
    }
    portEXIT_CRITICAL( &gps_lock );
    if( NULL != a_address )
    {
        memcpy( packet->address, a_address, sizeof( packet->address ) );
    }
    if( a_length > 0U )
    {
        memcpy( packet->payload, a_data, a_length );
    }
    if( pdTRUE != xQueueSend( pending_queue, &packet, 0U ) )
    {
        (void)xQueueSend( free_queue, &packet, 0U );
        __atomic_add_fetch( &stats.dropped, 1U, __ATOMIC_RELAXED );
    }
}

static void on_wifi( const uint8_t *a_data, size_t a_length, int8_t a_rssi, uint8_t a_channel )
{
    if( config.wifi && a_length >= 2U )
    {
        const uint8_t type = ( a_data[ 0 ] >> 2U ) & 3U;
        if( type <= 2U && 0U != ( config.wifi_types & ( 1U << type ) ) )
        {
            enqueue( 1U, a_data, a_length, a_rssi, a_channel, NULL, 0U, 0U );
        }
    }
}

static void on_ble( const uint8_t *a_data,
                    size_t a_length,
                    const uint8_t a_address[ 6 ],
                    uint8_t a_address_type,
                    uint8_t a_event_type,
                    int8_t a_rssi )
{
    if( config.ble )
    {
        if( a_length > 243U )
        {
            __atomic_add_fetch( &stats.dropped, 1U, __ATOMIC_RELAXED );
            return;
        }
        enqueue( 2U, a_data, a_length, a_rssi, 0U, a_address, a_address_type, a_event_type );
    }
}

static size_t build_radio_packet( const capture_packet_t *a_packet, uint8_t *a_output )
{
    if( 1U == a_packet->source )
    {
        /* Radiotap: channel frequency and dBm antenna signal, no invented FCS. */
        memset( a_output, 0, 13U );
        le16( a_output + 2U, 13U );
        le32( a_output + 4U, ( 1U << 3U ) | ( 1U << 5U ) );
        const uint16_t frequency =
            a_packet->channel == 14U ? 2484U : (uint16_t)( 2407U + a_packet->channel * 5U );
        le16( a_output + 8U, frequency );
        le16( a_output + 10U, 0x0080U );
        a_output[ 12 ] = (uint8_t)a_packet->rssi;
        memcpy( a_output + 13U, a_packet->payload, a_packet->length );
        return (size_t)a_packet->length + 13U;
    }

    /* HCI H4 event with direction pseudoheader, reconstructing an LE advertising report
       from NimBLE's exposed report fields; payload is kept byte-for-byte. */
    const size_t event_size = (size_t)a_packet->length + 12U;
    memset( a_output, 0, 19U );
    a_output[ 3 ] = 1U;    /* received */
    a_output[ 4 ] = 4U;    /* HCI event packet */
    a_output[ 5 ] = 0x3EU; /* LE meta event */
    a_output[ 6 ] = (uint8_t)event_size;
    a_output[ 7 ] = 2U; /* LE Advertising Report */
    a_output[ 8 ] = 1U; /* one report */
    a_output[ 9 ] = a_packet->event_type;
    a_output[ 10 ] = a_packet->address_type;
    memcpy( a_output + 11U, a_packet->address, 6U );
    a_output[ 17 ] = (uint8_t)a_packet->length;
    memcpy( a_output + 18U, a_packet->payload, a_packet->length );
    a_output[ 18U + a_packet->length ] = (uint8_t)a_packet->rssi;
    return a_packet->length + 19U;
}

static bool write_packet( const capture_packet_t *a_packet )
{
    uint8_t *frame = writer_frame;
    const size_t frame_length = build_radio_packet( a_packet, frame );
    const uint32_t padded = (uint32_t)( ( frame_length + 3U ) & ~3U );

    /* Kismet PCAPNG binary custom option 2989, PEN 55922, GPS v1.
       Fixed3_7 maps [-180,+180] degrees to unsigned units of 1e-7 degrees. */
    uint8_t gps_option[ 24 ] = { 0 };
    const uint32_t options = a_packet->gps_valid ? 28U : 0U;
    if( a_packet->gps_valid )
    {
        le16( gps_option, 2989U );
        le16( gps_option + 2U, 20U );
        le32( gps_option + 4U, 55922U );
        gps_option[ 8 ] = 0x47U;
        gps_option[ 9 ] = 1U;
        le16( gps_option + 10U, 16U );
        le32( gps_option + 12U, 0x06U );
        le32( gps_option + 16U, (uint32_t)( (int64_t)a_packet->longitude_e7 + 1800000000LL ) );
        le32( gps_option + 20U, (uint32_t)( (int64_t)a_packet->latitude_e7 + 1800000000LL ) );
    }
    const uint32_t length = 32U + padded + options;
    if( length > config.file_limit_bytes - 48U )
    {
        __atomic_add_fetch( &stats.dropped, 1U, __ATOMIC_RELAXED );
        return true;
    }
    if( current_bytes + length > config.file_limit_bytes )
    {
        close_segment();
        if( stats.storage_error || !open_segment() )
        {
            return false;
        }
    }

    uint8_t header[ 28 ] = { 0 };
    le32( header, 6U );
    le32( header + 4U, length );
    le32( header + 8U, a_packet->source == 1U ? 0U : 1U );
    le32( header + 12U, (uint32_t)( a_packet->timestamp_us >> 32U ) );
    le32( header + 16U, (uint32_t)a_packet->timestamp_us );
    le32( header + 20U, (uint32_t)frame_length );
    le32( header + 24U, (uint32_t)frame_length );
    uint8_t zero[ 4 ] = { 0 };
    if( !write_bytes( header, sizeof( header ) ) || !write_bytes( frame, frame_length ) ||
        ( padded > frame_length && !write_bytes( zero, padded - frame_length ) ) )
    {
        return false;
    }
    if( a_packet->gps_valid )
    {
        if( !write_bytes( gps_option, sizeof( gps_option ) ) ||
            !write_bytes( zero, sizeof( zero ) ) )
        {
            return false;
        }
    }
    uint8_t tail[ 4 ];
    le32( tail, length );
    if( !write_bytes( tail, sizeof( tail ) ) )
    {
        return false;
    }
    __atomic_add_fetch( &stats.written, 1U, __ATOMIC_RELAXED );
    return true;
}

static void writer_main( void *a_context )
{
    (void)a_context;
    int64_t last_sync_us = esp_timer_get_time();
    while( !stats.storage_error )
    {
        capture_packet_t *packet = NULL;
        if( pdTRUE == xQueueReceive( pending_queue, &packet, pdMS_TO_TICKS( 50U ) ) )
        {
            if( !write_packet( packet ) )
            {
                stats.storage_error = true;
            }
            (void)xQueueSend( free_queue, &packet, 0U );
        }
        const int64_t now_us = esp_timer_get_time();
        if( output_open && now_us - last_sync_us >= CAPTURE_SYNC_INTERVAL_US )
        {
            if( ESP_OK != media_sync( &output ) )
            {
                stats.storage_error = true;
            }
            last_sync_us = now_us;
        }
        if( stopping && 0U == uxQueueMessagesWaiting( pending_queue ) )
        {
            break;
        }
    }
    __atomic_add_fetch(
        &stats.dropped, (uint32_t)uxQueueMessagesWaiting( pending_queue ), __ATOMIC_RELAXED );
    close_segment();
    accepting = false;
    stats.recording = false;
    capture_task = NULL;
    vTaskDelete( NULL );
}

esp_err_t wardrive_capture_start( const wardrive_config_t *a_config )
{
    if( NULL == a_config || capture_task != NULL || packet_pool != NULL ||
        a_config->file_limit_bytes < CAPTURE_FILE_MIN ||
        a_config->file_limit_bytes > CAPTURE_FILE_MAX || a_config->max_files == 0U )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( !device_time_now( NULL ) )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( ESP_OK != media_app_collection( "captures", &collection ) )
    {
        return ESP_ERR_INVALID_STATE;
    }
    config = *a_config;
    stats = ( wardrive_stats_t ){ 0 };
    stopping = false;
    accepting = false;
    capture_files_t files = { 0 };
    if( ESP_OK == media_list( &collection, visit_capture, &files ) )
    {
        next_segment = files.found ? files.newest + 1U : 0U;
    }
    packet_pool = heap_caps_calloc(
        CAPTURE_QUEUE_COUNT, sizeof( *packet_pool ), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    writer_frame = heap_caps_malloc( CAPTURE_MAX_FRAME + 20U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT );
    pending_queue = xQueueCreate( CAPTURE_QUEUE_COUNT, sizeof( capture_packet_t * ) );
    free_queue = xQueueCreate( CAPTURE_QUEUE_COUNT, sizeof( capture_packet_t * ) );
    if( packet_pool == NULL || writer_frame == NULL || pending_queue == NULL || free_queue == NULL )
    {
        wardrive_capture_stop();
        return ESP_ERR_NO_MEM;
    }
    for( size_t i = 0U; i < CAPTURE_QUEUE_COUNT; ++i )
    {
        capture_packet_t *packet = &packet_pool[ i ];
        (void)xQueueSend( free_queue, &packet, 0U );
    }
    if( !open_segment() )
    {
        wardrive_capture_stop();
        return ESP_ERR_NOT_FOUND;
    }
    if( pdPASS !=
        xTaskCreate( writer_main, "wardrive_writer", CAPTURE_TASK_STACK, NULL, 2U, &capture_task ) )
    {
        wardrive_capture_stop();
        return ESP_ERR_NO_MEM;
    }
    radio_monitor_set_raw_observer( on_wifi );
    ble_monitor_set_raw_observer( on_ble );
    radio_monitor_set_capture_policy(
        true, config.fixed_channel ? config.channel : 0U, config.dwell_ms );
    accepting = true;
    stats.recording = true;
    ESP_LOGI( TAG,
              "capture started: Wi-Fi=%u BLE=%u file=%lu max_files=%u",
              config.wifi,
              config.ble,
              (unsigned long)config.file_limit_bytes,
              (unsigned)config.max_files );
    return ESP_OK;
}

void wardrive_capture_stop( void )
{
    accepting = false;
    radio_monitor_set_raw_observer( NULL );
    ble_monitor_set_raw_observer( NULL );
    radio_monitor_set_capture_policy( false, 0U, 0U );
    stopping = true;
    while( capture_task != NULL )
    {
        vTaskDelay( pdMS_TO_TICKS( 10U ) );
    }
    if( pending_queue != NULL )
    {
        vQueueDelete( pending_queue );
        pending_queue = NULL;
    }
    if( free_queue != NULL )
    {
        vQueueDelete( free_queue );
        free_queue = NULL;
    }
    close_segment();
    heap_caps_free( packet_pool );
    packet_pool = NULL;
    heap_caps_free( writer_frame );
    writer_frame = NULL;
    stats.recording = false;
}

void wardrive_capture_stats( wardrive_stats_t *a_stats )
{
    if( NULL != a_stats )
    {
        *a_stats = stats;
    }
}
