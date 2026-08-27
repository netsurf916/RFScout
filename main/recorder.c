/**
    recorder.c : RFScout SD-backed audio recorder implementation
    Description: Captures PDM microphone audio to validated PCM WAV files and manages playback entries.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "recorder.h"

#include "audio_output.h"
#include "board.h"
#include "device_time.h"
#include "storage.h"

#include "driver/i2s_pdm.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define RECORDER_DMA_BUFFER_COUNT 8U
#define RECORDER_DMA_BUFFER_LENGTH 256U
#define RECORDER_DMA_READ_SAMPLES 512U
#define RECORDER_TASK_STACK 6144U
#define RECORDER_TASK_PRIORITY 4U
#define RECORDER_WAV_HEADER_SIZE 44U
#define RECORDER_CHANNELS 1U
#define RECORDER_BITS_PER_SAMPLE 16U

static const char *TAG = "recorder";
static i2s_chan_handle_t rx_channel;
static TaskHandle_t record_task;
static volatile bool record_running;
static FILE *record_file;
static rfscout_recorder_state_t state = RFSCOUT_RECORDER_NO_STORAGE;
static char active_name[ RFSCOUT_RECORDER_NAME_MAX ];
static uint32_t data_bytes;
static rfscout_recording_t recordings[ RFSCOUT_RECORDER_MAX_FILES ];
static size_t recording_count;

static void write_u16_le( uint8_t *a_target, uint16_t a_value )
{
    a_target[ 0 ] = (uint8_t)( a_value & 0xffU );
    a_target[ 1 ] = (uint8_t)( ( a_value >> 8U ) & 0xffU );
}

static void write_u32_le( uint8_t *a_target, uint32_t a_value )
{
    a_target[ 0 ] = (uint8_t)( a_value & 0xffU );
    a_target[ 1 ] = (uint8_t)( ( a_value >> 8U ) & 0xffU );
    a_target[ 2 ] = (uint8_t)( ( a_value >> 16U ) & 0xffU );
    a_target[ 3 ] = (uint8_t)( ( a_value >> 24U ) & 0xffU );
}

static uint16_t read_u16_le( const uint8_t *a_source )
{
    return (uint16_t)a_source[ 0 ] | ( (uint16_t)a_source[ 1 ] << 8U );
}

static uint32_t read_u32_le( const uint8_t *a_source )
{
    return (uint32_t)a_source[ 0 ] | ( (uint32_t)a_source[ 1 ] << 8U ) |
           ( (uint32_t)a_source[ 2 ] << 16U ) | ( (uint32_t)a_source[ 3 ] << 24U );
}

static void build_wav_header( uint8_t a_header[ RECORDER_WAV_HEADER_SIZE ], uint32_t a_data_bytes )
{
    memset( a_header, 0, RECORDER_WAV_HEADER_SIZE );
    memcpy( &a_header[ 0 ], "RIFF", 4U );
    write_u32_le( &a_header[ 4 ], 36U + a_data_bytes );
    memcpy( &a_header[ 8 ], "WAVE", 4U );
    memcpy( &a_header[ 12 ], "fmt ", 4U );
    write_u32_le( &a_header[ 16 ], 16U );
    write_u16_le( &a_header[ 20 ], 1U );
    write_u16_le( &a_header[ 22 ], RECORDER_CHANNELS );
    write_u32_le( &a_header[ 24 ], RFSCOUT_RECORDER_SAMPLE_RATE );
    write_u32_le( &a_header[ 28 ],
                  RFSCOUT_RECORDER_SAMPLE_RATE * RECORDER_CHANNELS * RECORDER_BITS_PER_SAMPLE /
                      8U );
    write_u16_le( &a_header[ 32 ], RECORDER_CHANNELS * RECORDER_BITS_PER_SAMPLE / 8U );
    write_u16_le( &a_header[ 34 ], RECORDER_BITS_PER_SAMPLE );
    memcpy( &a_header[ 36 ], "data", 4U );
    write_u32_le( &a_header[ 40 ], a_data_bytes );
}

static bool validate_wav( const char *a_path, rfscout_recording_t *a_recording )
{
    FILE *file = fopen( a_path, "rb" );
    if( NULL == file )
    {
        return false;
    }

    uint8_t header[ RECORDER_WAV_HEADER_SIZE ];
    const size_t read = fread( header, 1U, sizeof( header ), file );
    fclose( file );

    if( read != sizeof( header ) || 0 != memcmp( &header[ 0 ], "RIFF", 4U ) ||
        0 != memcmp( &header[ 8 ], "WAVE", 4U ) || 0 != memcmp( &header[ 12 ], "fmt ", 4U ) ||
        0 != memcmp( &header[ 36 ], "data", 4U ) || 1U != read_u16_le( &header[ 20 ] ) ||
        RECORDER_CHANNELS != read_u16_le( &header[ 22 ] ) ||
        RFSCOUT_RECORDER_SAMPLE_RATE != read_u32_le( &header[ 24 ] ) ||
        RECORDER_BITS_PER_SAMPLE != read_u16_le( &header[ 34 ] ) )
    {
        return false;
    }

    const uint32_t bytes = read_u32_le( &header[ 40 ] );
    if( NULL != a_recording )
    {
        a_recording->data_bytes = bytes;
        a_recording->duration_seconds = bytes / ( RFSCOUT_RECORDER_SAMPLE_RATE * RECORDER_CHANNELS *
                                                  RECORDER_BITS_PER_SAMPLE / 8U );
    }
    return true;
}

static int recording_name_compare( const void *a_left, const void *a_right )
{
    const rfscout_recording_t *left = (const rfscout_recording_t *)a_left;
    const rfscout_recording_t *right = (const rfscout_recording_t *)a_right;
    return strcmp( right->name, left->name );
}

static esp_err_t ensure_directory( void )
{
    struct stat status;
    if( 0 == stat( RFSCOUT_RECORDER_DIRECTORY, &status ) )
    {
        return S_ISDIR( status.st_mode ) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    if( ( 0 == mkdir( RFSCOUT_RECORDER_DIRECTORY, 0775 ) ) || ( EEXIST == errno ) )
    {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static void release_channel( void )
{
    if( NULL == rx_channel )
    {
        return;
    }

    (void)i2s_channel_disable( rx_channel );
    (void)i2s_del_channel( rx_channel );
    rx_channel = NULL;
}

static esp_err_t open_microphone( void )
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG( I2S_NUM_0, I2S_ROLE_MASTER );
    channel_config.dma_desc_num = RECORDER_DMA_BUFFER_COUNT;
    channel_config.dma_frame_num = RECORDER_DMA_BUFFER_LENGTH;

    esp_err_t result = i2s_new_channel( &channel_config, NULL, &rx_channel );
    if( ESP_OK != result )
    {
        rx_channel = NULL;
        return result;
    }

    i2s_pdm_rx_config_t pdm_config = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG( RFSCOUT_RECORDER_SAMPLE_RATE ),
        .slot_cfg =
            I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG( I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO ),
        .gpio_cfg = { .clk = (gpio_num_t)RFSCOUT_BOARD_MIC_CLOCK_PIN,
                      .din = (gpio_num_t)RFSCOUT_BOARD_MIC_DATA_PIN,
                      .invert_flags = { .clk_inv = false } } };

#if CONFIG_IDF_TARGET_ESP32S3
    pdm_config.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
    pdm_config.slot_cfg.slot_mask = I2S_PDM_LINE_SLOT_ALL;
#endif

#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
    pdm_config.slot_cfg.hp_en = false;
#endif

    result = i2s_channel_init_pdm_rx_mode( rx_channel, &pdm_config );
    if( ESP_OK == result )
    {
        result = i2s_channel_enable( rx_channel );
    }
    if( ESP_OK != result )
    {
        release_channel();
    }
    return result;
}

static void make_recording_name( char *a_name, size_t a_capacity )
{
    time_t epoch;
    if( device_time_now( &epoch ) )
    {
        struct tm local;
        localtime_r( &epoch, &local );
        (void)strftime( a_name, a_capacity, "%Y%m%d-%H%M%S.wav", &local );
        return;
    }

    const uint64_t seconds = (uint64_t)esp_timer_get_time() / 1000000ULL;
    snprintf( a_name, a_capacity, "rec-%010llu.wav", (unsigned long long)seconds );
}

static void finalize_recording( void )
{
    if( NULL == record_file )
    {
        return;
    }

    uint8_t header[ RECORDER_WAV_HEADER_SIZE ];
    build_wav_header( header, data_bytes );
    (void)fflush( record_file );
    if( 0 == fseek( record_file, 0L, SEEK_SET ) )
    {
        (void)fwrite( header, 1U, sizeof( header ), record_file );
        (void)fflush( record_file );
    }
    fclose( record_file );
    record_file = NULL;
}

static void record_worker( void *a_argument )
{
    (void)a_argument;
    int16_t dma_samples[ RECORDER_DMA_READ_SAMPLES ];
    int16_t mono[ RECORDER_DMA_READ_SAMPLES / 2U ];

    while( record_running )
    {
        size_t bytes = 0U;
        const esp_err_t result = i2s_channel_read(
            rx_channel, dma_samples, sizeof( dma_samples ), &bytes, pdMS_TO_TICKS( 100 ) );
        if( ESP_ERR_TIMEOUT == result )
        {
            continue;
        }
        if( ESP_OK != result )
        {
            if( record_running )
            {
                ESP_LOGW( TAG, "microphone read failed: %s", esp_err_to_name( result ) );
                state = RFSCOUT_RECORDER_ERROR;
            }
            break;
        }

        const size_t samples = bytes / sizeof( int16_t );
        size_t mono_count = 0U;
        for( size_t index = 0U; ( index + 1U ) < samples; index += 2U )
        {
            mono[ mono_count++ ] = dma_samples[ index ];
        }

        if( ( mono_count > 0U ) &&
            ( mono_count != fwrite( mono, sizeof( int16_t ), mono_count, record_file ) ) )
        {
            ESP_LOGW( TAG, "recording write failed" );
            state = RFSCOUT_RECORDER_ERROR;
            break;
        }
        data_bytes += (uint32_t)( mono_count * sizeof( int16_t ) );
    }

    record_running = false;
    record_task = NULL;
    vTaskDelete( NULL );
}

esp_err_t recorder_init( void )
{
    return recorder_refresh();
}

esp_err_t recorder_refresh( void )
{
    if( ( RFSCOUT_RECORDER_RECORDING == state ) || ( RFSCOUT_RECORDER_PLAYING == state ) )
    {
        return ESP_ERR_INVALID_STATE;
    }

    recording_count = 0U;
    if( !storage_is_available() )
    {
        state = RFSCOUT_RECORDER_NO_STORAGE;
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ensure_directory();
    if( ESP_OK != result )
    {
        state = RFSCOUT_RECORDER_ERROR;
        return result;
    }

    DIR *directory = opendir( RFSCOUT_RECORDER_DIRECTORY );
    if( NULL == directory )
    {
        state = RFSCOUT_RECORDER_ERROR;
        return ESP_FAIL;
    }

    struct dirent *entry;
    while( ( NULL != ( entry = readdir( directory ) ) ) &&
           ( recording_count < RFSCOUT_RECORDER_MAX_FILES ) )
    {
        const size_t length = strlen( entry->d_name );
        rfscout_recording_t candidate = { 0 };
        if( ( length < 5U ) || ( length >= sizeof( candidate.name ) ) ||
            ( 0 != strcasecmp( &entry->d_name[ length - 4U ], ".wav" ) ) )
        {
            continue;
        }

        memcpy( candidate.name, entry->d_name, length + 1U );

        char path[ 160 ];
        const int path_length =
            snprintf( path, sizeof( path ), "%s/%s", RFSCOUT_RECORDER_DIRECTORY, candidate.name );
        if( ( path_length < 0 ) || ( (size_t)path_length >= sizeof( path ) ) ||
            !validate_wav( path, &candidate ) )
        {
            continue;
        }

        recordings[ recording_count++ ] = candidate;
    }
    closedir( directory );

    qsort( recordings, recording_count, sizeof( recordings[ 0 ] ), recording_name_compare );
    state = RFSCOUT_RECORDER_IDLE;
    return ESP_OK;
}

esp_err_t recorder_start_recording( void )
{
    if( !storage_is_available() )
    {
        state = RFSCOUT_RECORDER_NO_STORAGE;
        return ESP_ERR_INVALID_STATE;
    }
    if( RFSCOUT_RECORDER_IDLE != state )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ensure_directory();
    if( ESP_OK != result )
    {
        state = RFSCOUT_RECORDER_ERROR;
        return result;
    }

    make_recording_name( active_name, sizeof( active_name ) );
    char path[ 160 ];
    snprintf( path, sizeof( path ), "%s/%s", RFSCOUT_RECORDER_DIRECTORY, active_name );
    record_file = fopen( path, "wb+" );
    if( NULL == record_file )
    {
        state = RFSCOUT_RECORDER_ERROR;
        return ESP_FAIL;
    }

    uint8_t header[ RECORDER_WAV_HEADER_SIZE ];
    build_wav_header( header, 0U );
    if( sizeof( header ) != fwrite( header, 1U, sizeof( header ), record_file ) )
    {
        fclose( record_file );
        record_file = NULL;
        state = RFSCOUT_RECORDER_ERROR;
        return ESP_FAIL;
    }

    result = open_microphone();
    if( ESP_OK != result )
    {
        fclose( record_file );
        record_file = NULL;
        (void)remove( path );
        state = RFSCOUT_RECORDER_ERROR;
        return result;
    }

    data_bytes = 0U;
    record_running = true;
    if( pdPASS != xTaskCreate( record_worker,
                               "recorder",
                               RECORDER_TASK_STACK,
                               NULL,
                               RECORDER_TASK_PRIORITY,
                               &record_task ) )
    {
        record_running = false;
        release_channel();
        fclose( record_file );
        record_file = NULL;
        (void)remove( path );
        state = RFSCOUT_RECORDER_ERROR;
        return ESP_ERR_NO_MEM;
    }

    state = RFSCOUT_RECORDER_RECORDING;
    ESP_LOGI( TAG, "recording %s", path );
    return ESP_OK;
}

void recorder_stop_recording( void )
{
    if( RFSCOUT_RECORDER_RECORDING != state && !record_running )
    {
        return;
    }

    record_running = false;
    for( unsigned attempt = 0U; ( NULL != record_task ) && ( attempt < 50U ); ++attempt )
    {
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }
    release_channel();
    finalize_recording();
    active_name[ 0 ] = '\0';
    state = storage_is_available() ? RFSCOUT_RECORDER_IDLE : RFSCOUT_RECORDER_NO_STORAGE;
    if( storage_is_available() )
    {
        (void)recorder_refresh();
    }
}

esp_err_t recorder_start_playback( size_t a_index, uint8_t a_volume_percent )
{
    if( !storage_is_available() )
    {
        state = RFSCOUT_RECORDER_NO_STORAGE;
        return ESP_ERR_INVALID_STATE;
    }
    if( ( RFSCOUT_RECORDER_IDLE != state ) || ( a_index >= recording_count ) )
    {
        return ESP_ERR_INVALID_ARG;
    }

    char path[ 160 ];
    snprintf(
        path, sizeof( path ), "%s/%s", RFSCOUT_RECORDER_DIRECTORY, recordings[ a_index ].name );
    if( !audio_output_play_wav( path, a_volume_percent ) )
    {
        return ESP_FAIL;
    }

    state = RFSCOUT_RECORDER_PLAYING;
    return ESP_OK;
}

void recorder_stop( void )
{
    if( RFSCOUT_RECORDER_RECORDING == state || record_running )
    {
        recorder_stop_recording();
        return;
    }

    if( RFSCOUT_RECORDER_PLAYING == state )
    {
        audio_output_stop_playback();
    }
    state = storage_is_available() ? RFSCOUT_RECORDER_IDLE : RFSCOUT_RECORDER_NO_STORAGE;
}

rfscout_recorder_state_t recorder_state( void )
{
    if( !storage_is_available() && ( RFSCOUT_RECORDER_RECORDING != state ) )
    {
        return RFSCOUT_RECORDER_NO_STORAGE;
    }
    if( ( RFSCOUT_RECORDER_PLAYING == state ) && !audio_output_playback_active() )
    {
        state = RFSCOUT_RECORDER_IDLE;
    }
    return state;
}

const char *recorder_active_name( void )
{
    return active_name;
}

uint32_t recorder_recorded_bytes( void )
{
    return data_bytes;
}

size_t recorder_file_count( void )
{
    return recording_count;
}

const rfscout_recording_t *recorder_file( size_t a_index )
{
    return ( a_index < recording_count ) ? &recordings[ a_index ] : NULL;
}