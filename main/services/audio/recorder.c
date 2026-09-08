/**
    services/audio/recorder.c : RFScout shared microphone, recorder, and playback service
    Description: Owns microphone capture/FFT plus SD-backed WAV recording and playback.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "recorder.h"

#include "audio_filter.h"
#include "audio_output.h"
#include "board.h"
#include "device_time.h"
#include "es7210_input.h"
#include "storage.h"

#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define RECORDER_DMA_BUFFER_COUNT 8U
#define RECORDER_DMA_BUFFER_LENGTH 256U
#define RECORDER_DMA_READ_SAMPLES 512U
#define RECORDER_TASK_STACK 6144U
#define RECORDER_TASK_PRIORITY 4U
#define RECORDER_WAV_HEADER_SIZE 44U
#define RECORDER_CHANNELS 1U
#define RECORDER_BITS_PER_SAMPLE 16U
#define RECORDER_STOP_TIMEOUT_MS 3000U
#define RECORDER_UNIQUE_SUFFIX_LIMIT 100U
#define RECORDER_MIC_WAKEUP_MS 20U
#define RECORDER_DIAGNOSTIC_INTERVAL_US 1000000LL
#define RECORDER_CLOCK_MEASURE_MS 10U
#define RECORDER_MIC_PROBE_MS 5U
#define RECORDER_MIC_PROBE_MIN_EDGES 32U
#define RECORDER_SIGNAL_SNAPSHOT_THRESHOLD 4000U
#define RECORDER_MIC_PDM_CLOCK_MIN_HZ 1000000U
#define RECORDER_MIC_PDM_CLOCK_MAX_HZ 3250000U
#define RECORDER_CLOCK_DERIVATION_MAX_REQUEST_HZ 96000U
#define RECORDER_NORMALIZE_TARGET_AVERAGE 5000U
#define RECORDER_NORMALIZE_MAX_GAIN_Q10 ( 16U * 1024U )
#define RECORDER_NORMALIZE_MIN_SIGNAL 32U
#define RECORDER_TRIM_START_MS 300U
#define RECORDER_TRIM_END_MS 600U
#define RECORDER_MONITOR_FFT_SIZE 256U
#define RECORDER_MONITOR_TASK_STACK 6144U
#define RECORDER_MONITOR_TASK_PRIORITY 3U
#define RECORDER_MONITOR_FIRST_FFT_BIN 1U
#define RECORDER_MONITOR_MIN_RANGE_DB 18.0f
#define RECORDER_MONITOR_HEADROOM_DB 3.0f
#define RECORDER_MONITOR_RANGE_SMOOTH 0.05f
#define RECORDER_EXPECTED_BYTES_PER_SECOND                                                         \
    ( RFSCOUT_RECORDER_SAMPLE_RATE * RECORDER_CHANNELS * ( RECORDER_BITS_PER_SAMPLE / 8U ) )

typedef enum
{
    RECORDER_INPUT_NONE = 0,
    RECORDER_INPUT_PDM,
    RECORDER_INPUT_ES7210,
} recorder_input_t;

static const char *TAG = "recorder";
static i2s_chan_handle_t rx_channel;
static recorder_input_t active_input;
static TaskHandle_t record_task;
static SemaphoreHandle_t record_done;
static volatile bool record_running;
static FILE *record_file;
static rfscout_recorder_state_t state = RFSCOUT_RECORDER_NO_STORAGE;
static char active_name[ RFSCOUT_RECORDER_NAME_MAX ];
static uint32_t data_bytes;
static rfscout_recording_t recordings[ RFSCOUT_RECORDER_MAX_FILES ];
static size_t recording_count;
static volatile uint8_t live_level_percent;
static volatile uint8_t live_peak_percent;
static volatile int active_pdm_slot = 1;
static int64_t recording_started_us;
static int64_t diagnostic_started_us;
static uint32_t diagnostic_input_bytes;
static uint32_t diagnostic_output_bytes;
static uint32_t diagnostic_zero_samples;
static uint32_t diagnostic_sample_count;
static int16_t diagnostic_min_sample;
static int16_t diagnostic_max_sample;
static int64_t playback_started_us;
static bool diagnostic_initial_snapshot_logged;
static bool diagnostic_signal_snapshot_logged;
static uint32_t measured_pdm_clock_hz;
static uint32_t recording_wav_sample_rate = 12800U;
static TaskHandle_t monitor_task;
static volatile bool monitor_running;
static uint8_t monitor_spectrum[ RFSCOUT_RECORDER_MONITOR_BINS ];
static bool monitor_range_initialized;
static float monitor_floor_db;
static float monitor_ceiling_db;
static volatile uint8_t monitor_range_db;
static volatile uint32_t monitor_sample_rate_hz = RFSCOUT_RECORDER_SAMPLE_RATE;
static rfscout_audio_highpass_t monitor_highpass;
static float monitor_fft_frame[ RECORDER_MONITOR_FFT_SIZE ];
static size_t monitor_fft_frame_count;

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

static void build_wav_header( uint8_t a_header[ RECORDER_WAV_HEADER_SIZE ],
                              uint32_t a_data_bytes,
                              uint32_t a_sample_rate_hz )
{
    memset( a_header, 0, RECORDER_WAV_HEADER_SIZE );
    memcpy( &a_header[ 0 ], "RIFF", 4U );
    write_u32_le( &a_header[ 4 ], 36U + a_data_bytes );
    memcpy( &a_header[ 8 ], "WAVE", 4U );
    memcpy( &a_header[ 12 ], "fmt ", 4U );
    write_u32_le( &a_header[ 16 ], 16U );
    write_u16_le( &a_header[ 20 ], 1U );
    write_u16_le( &a_header[ 22 ], RECORDER_CHANNELS );
    write_u32_le( &a_header[ 24 ], a_sample_rate_hz );
    write_u32_le( &a_header[ 28 ],
                  a_sample_rate_hz * RECORDER_CHANNELS * RECORDER_BITS_PER_SAMPLE / 8U );
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
    struct stat file_status;
    const bool stat_ok = ( 0 == fstat( fileno( file ), &file_status ) );
    fclose( file );

    if( read != sizeof( header ) || 0 != memcmp( &header[ 0 ], "RIFF", 4U ) ||
        0 != memcmp( &header[ 8 ], "WAVE", 4U ) || 0 != memcmp( &header[ 12 ], "fmt ", 4U ) ||
        0 != memcmp( &header[ 36 ], "data", 4U ) || 1U != read_u16_le( &header[ 20 ] ) ||
        RECORDER_CHANNELS != read_u16_le( &header[ 22 ] ) ||
        read_u32_le( &header[ 24 ] ) < RFSCOUT_RECORDER_WAV_RATE_MIN ||
        read_u32_le( &header[ 24 ] ) > RFSCOUT_RECORDER_WAV_RATE_MAX ||
        RECORDER_BITS_PER_SAMPLE != read_u16_le( &header[ 34 ] ) )
    {
        return false;
    }

    const uint32_t bytes = read_u32_le( &header[ 40 ] );
    if( !stat_ok || file_status.st_size < (off_t)( RECORDER_WAV_HEADER_SIZE + (uint64_t)bytes ) )
    {
        return false;
    }
    if( NULL != a_recording )
    {
        const uint32_t sample_rate_hz = read_u32_le( &header[ 24 ] );
        a_recording->data_bytes = bytes;
        a_recording->duration_seconds =
            bytes / ( sample_rate_hz * RECORDER_CHANNELS * RECORDER_BITS_PER_SAMPLE / 8U );
    }
    return true;
}

static int recording_name_compare( const void *a_left, const void *a_right )
{
    const rfscout_recording_t *left = (const rfscout_recording_t *)a_left;
    const rfscout_recording_t *right = (const rfscout_recording_t *)a_right;
    if( right->modified_time > left->modified_time )
    {
        return 1;
    }
    if( right->modified_time < left->modified_time )
    {
        return -1;
    }
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
    if( RECORDER_INPUT_ES7210 == active_input )
    {
        es7210_input_close();
        rx_channel = NULL;
        active_input = RECORDER_INPUT_NONE;
        return;
    }
    if( NULL != rx_channel )
    {
        (void)i2s_channel_disable( rx_channel );
        (void)i2s_del_channel( rx_channel );
        rx_channel = NULL;
    }
    active_input = RECORDER_INPUT_NONE;
}

static uint32_t measure_pdm_clock_hz( void )
{
    pcnt_unit_handle_t unit = NULL;
    pcnt_channel_handle_t channel = NULL;
    const pcnt_unit_config_t unit_config = { .low_limit = -1, .high_limit = 30000 };
    esp_err_t result = pcnt_new_unit( &unit_config, &unit );
    if( ESP_OK != result )
    {
        ESP_LOGW(
            TAG, "PDM clock measurement unavailable: pcnt unit: %s", esp_err_to_name( result ) );
        return 0U;
    }

    const pcnt_chan_config_t channel_config = {
        .edge_gpio_num = RFSCOUT_BOARD_MIC_CLOCK_PIN,
        .level_gpio_num = -1,
        .flags = { .virt_level_io_level = 1 },
    };
    result = pcnt_new_channel( unit, &channel_config, &channel );
    if( ESP_OK == result )
    {
        result = pcnt_channel_set_edge_action(
            channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD );
    }
    if( ESP_OK == result )
    {
        result = pcnt_unit_enable( unit );
    }
    if( ESP_OK == result )
    {
        (void)pcnt_unit_clear_count( unit );
        result = pcnt_unit_start( unit );
    }

    int count = 0;
    if( ESP_OK == result )
    {
        vTaskDelay( pdMS_TO_TICKS( RECORDER_CLOCK_MEASURE_MS ) );
        (void)pcnt_unit_stop( unit );
        result = pcnt_unit_get_count( unit, &count );
    }

    if( ESP_OK == result )
    {
        const uint32_t measured_hz =
            (uint32_t)( (uint64_t)( count > 0 ? count : 0 ) * 1000ULL / RECORDER_CLOCK_MEASURE_MS );
        ESP_LOGI( TAG,
                  "PDM clock measured: gpio=%d edges=%d window=%ums frequency=%u Hz",
                  RFSCOUT_BOARD_MIC_CLOCK_PIN,
                  count,
                  RECORDER_CLOCK_MEASURE_MS,
                  (unsigned)measured_hz );
        measured_pdm_clock_hz = measured_hz;
    }
    else
    {
        ESP_LOGW( TAG, "PDM clock measurement failed: %s", esp_err_to_name( result ) );
    }

    if( NULL != unit )
    {
        (void)pcnt_unit_disable( unit );
    }
    if( NULL != channel )
    {
        (void)pcnt_del_channel( channel );
    }
    if( NULL != unit )
    {
        (void)pcnt_del_unit( unit );
    }
    return ESP_OK == result ? measured_pdm_clock_hz : 0U;
}

static uint32_t measure_pdm_data_edges( void )
{
    pcnt_unit_handle_t unit = NULL;
    pcnt_channel_handle_t channel = NULL;
    const pcnt_unit_config_t unit_config = { .low_limit = -1, .high_limit = 30000 };
    esp_err_t result = pcnt_new_unit( &unit_config, &unit );
    if( ESP_OK != result )
    {
        return 0U;
    }

    const pcnt_chan_config_t channel_config = {
        .edge_gpio_num = RFSCOUT_BOARD_MIC_DATA_PIN,
        .level_gpio_num = -1,
        .flags = { .virt_level_io_level = 1 },
    };
    result = pcnt_new_channel( unit, &channel_config, &channel );
    if( ESP_OK == result )
    {
        result = pcnt_channel_set_edge_action(
            channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD );
    }
    if( ESP_OK == result )
    {
        result = pcnt_unit_enable( unit );
    }
    if( ESP_OK == result )
    {
        (void)pcnt_unit_clear_count( unit );
        result = pcnt_unit_start( unit );
    }

    int count = 0;
    if( ESP_OK == result )
    {
        const TickType_t requested_ticks = pdMS_TO_TICKS( RECORDER_MIC_PROBE_MS );
        vTaskDelay( requested_ticks > 0U ? requested_ticks : 1U );
        (void)pcnt_unit_stop( unit );
        result = pcnt_unit_get_count( unit, &count );
    }

    if( NULL != unit )
    {
        (void)pcnt_unit_disable( unit );
    }
    if( NULL != channel )
    {
        (void)pcnt_del_channel( channel );
    }
    if( NULL != unit )
    {
        (void)pcnt_del_unit( unit );
    }
    return ESP_OK == result && count > 0 ? (uint32_t)count : 0U;
}

static void log_pcm_snapshot( const char *a_label, const int16_t *a_samples, size_t a_count )
{
    if( NULL == a_samples || 0U == a_count )
    {
        return;
    }

    char line[ 256 ];
    size_t used = (size_t)snprintf( line, sizeof( line ), "%s:", a_label );
    const size_t limit = a_count < 24U ? a_count : 24U;
    for( size_t index = 0U; index < limit && used < sizeof( line ); ++index )
    {
        const int written =
            snprintf( &line[ used ], sizeof( line ) - used, " %d", (int)a_samples[ index ] );
        if( written < 0 || (size_t)written >= ( sizeof( line ) - used ) )
        {
            break;
        }
        used += (size_t)written;
    }
    ESP_LOGI( TAG, "%s", line );
}

static esp_err_t open_pdm_microphone( void )
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
    /* The board microphone SELECT input is pulled low, so capture its LEFT PDM slot directly. */
    pdm_config.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    pdm_config.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
#endif

#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
    pdm_config.slot_cfg.hp_en = true;
#endif

    ESP_LOGI( TAG,
              "PDM request: pcm=%u Hz clk_src=%d mclk_multiple=%d dn_sample=%d bclk_div=%u "
              "target_pdm=%u Hz dma_desc=%u dma_frames=%u",
              (unsigned)pdm_config.clk_cfg.sample_rate_hz,
              (int)pdm_config.clk_cfg.clk_src,
              (int)pdm_config.clk_cfg.mclk_multiple,
              (int)pdm_config.clk_cfg.dn_sample_mode,
              (unsigned)pdm_config.clk_cfg.bclk_div,
              (unsigned)( pdm_config.clk_cfg.sample_rate_hz * 64U ),
              (unsigned)channel_config.dma_desc_num,
              (unsigned)channel_config.dma_frame_num );

    result = i2s_channel_init_pdm_rx_mode( rx_channel, &pdm_config );
    if( ESP_OK == result )
    {
        result = i2s_channel_enable( rx_channel );
    }
    if( ESP_OK != result )
    {
        release_channel();
    }
    else
    {
        active_input = RECORDER_INPUT_PDM;
        ESP_LOGI( TAG,
                  "PDM configured: I2S0 clk=GPIO%d data=GPIO%d rate=%u bits=%u channels=%u "
                  "slot=LEFT HPF=on expected_pcm=%u B/s",
                  RFSCOUT_BOARD_MIC_CLOCK_PIN,
                  RFSCOUT_BOARD_MIC_DATA_PIN,
                  RFSCOUT_RECORDER_SAMPLE_RATE,
                  RECORDER_BITS_PER_SAMPLE,
                  RECORDER_CHANNELS,
                  (unsigned)RECORDER_EXPECTED_BYTES_PER_SECOND );
        measured_pdm_clock_hz = measure_pdm_clock_hz();
        if( measured_pdm_clock_hz > 0U )
        {
            const uint32_t expected_pdm_hz = pdm_config.clk_cfg.sample_rate_hz * 64U;
            const uint32_t clock_ratio_permille =
                expected_pdm_hz > 0U
                    ? (uint32_t)( (uint64_t)measured_pdm_clock_hz * 1000ULL / expected_pdm_hz )
                    : 0U;
            uint32_t derived_request_hz =
                (uint32_t)( (uint64_t)pdm_config.clk_cfg.sample_rate_hz *
                            RFSCOUT_RECORDER_TARGET_PDM_CLOCK_HZ / measured_pdm_clock_hz );
            if( derived_request_hz > RECORDER_CLOCK_DERIVATION_MAX_REQUEST_HZ )
            {
                derived_request_hz = RECORDER_CLOCK_DERIVATION_MAX_REQUEST_HZ;
            }
            ESP_LOGI( TAG,
                      "PDM calibration: requested_pcm=%u expected_clk=%u measured_clk=%u "
                      "clock_ratio=%u.%u%% target_clk=%u derived_request=%u",
                      (unsigned)pdm_config.clk_cfg.sample_rate_hz,
                      (unsigned)expected_pdm_hz,
                      (unsigned)measured_pdm_clock_hz,
                      (unsigned)( clock_ratio_permille / 10U ),
                      (unsigned)( clock_ratio_permille % 10U ),
                      (unsigned)RFSCOUT_RECORDER_TARGET_PDM_CLOCK_HZ,
                      (unsigned)derived_request_hz );
            if( measured_pdm_clock_hz < RECORDER_MIC_PDM_CLOCK_MIN_HZ ||
                measured_pdm_clock_hz > RECORDER_MIC_PDM_CLOCK_MAX_HZ )
            {
                ESP_LOGW( TAG,
                          "PDM clock outside microphone range: measured=%u valid=%u..%u Hz",
                          (unsigned)measured_pdm_clock_hz,
                          (unsigned)RECORDER_MIC_PDM_CLOCK_MIN_HZ,
                          (unsigned)RECORDER_MIC_PDM_CLOCK_MAX_HZ );
            }
        }
    }
    return result;
}

static esp_err_t open_microphone( void )
{
    if( RFSCOUT_BOARD_HAS_PDM_MIC )
    {
        return open_pdm_microphone();
    }
    if( RFSCOUT_BOARD_HAS_ES7210_MIC )
    {
        const esp_err_t result = es7210_input_open( &rx_channel );
        if( ESP_OK == result )
        {
            active_input = RECORDER_INPUT_ES7210;
        }
        return result;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t recorder_probe_microphone( void )
{
    if( NULL != rx_channel || NULL != record_task || NULL != monitor_task )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = open_microphone();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "microphone probe setup failed: %s", esp_err_to_name( result ) );
        return result;
    }

    if( RECORDER_INPUT_ES7210 == active_input )
    {
        release_channel();
        ESP_LOGI( TAG, "ES7210 microphone detected" );
        return ESP_OK;
    }

    uint32_t edges = 0U;
    (void)gpio_set_pull_mode( RFSCOUT_BOARD_MIC_DATA_PIN, GPIO_PULLDOWN_ONLY );
    vTaskDelay( pdMS_TO_TICKS( RECORDER_MIC_WAKEUP_MS ) );
    edges = measure_pdm_data_edges();
    release_channel();
    (void)gpio_set_pull_mode( RFSCOUT_BOARD_MIC_DATA_PIN, GPIO_FLOATING );

    if( edges < RECORDER_MIC_PROBE_MIN_EDGES )
    {
        ESP_LOGI( TAG,
                  "microphone probe: data_edges=%u/%ums; not detected",
                  (unsigned)edges,
                  (unsigned)RECORDER_MIC_PROBE_MS );
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI( TAG,
              "microphone probe: data_edges=%u/%ums; detected",
              (unsigned)edges,
              (unsigned)RECORDER_MIC_PROBE_MS );
    return ESP_OK;
}

static void make_recording_name( char *a_name, size_t a_capacity )
{
    time_t epoch;
    (void)device_time_now( &epoch );
    struct tm local;
    localtime_r( &epoch, &local );
    (void)strftime( a_name, a_capacity, "%Y%m%d-%H%M%S.wav", &local );
}

static FILE *open_unique_recording( char *a_path, size_t a_path_capacity )
{
    char base_name[ RFSCOUT_RECORDER_NAME_MAX ];
    make_recording_name( base_name, sizeof( base_name ) );

    const size_t base_length = strlen( base_name );
    const size_t stem_length = ( base_length > 4U ) ? base_length - 4U : base_length;
    for( unsigned suffix = 0U; suffix < RECORDER_UNIQUE_SUFFIX_LIMIT; ++suffix )
    {
        if( 0U == suffix )
        {
            snprintf( active_name, sizeof( active_name ), "%s", base_name );
        }
        else
        {
            snprintf( active_name,
                      sizeof( active_name ),
                      "%.*s-%02u.wav",
                      (int)stem_length,
                      base_name,
                      suffix );
        }

        const int path_length =
            snprintf( a_path, a_path_capacity, "%s/%s", RFSCOUT_RECORDER_DIRECTORY, active_name );
        if( ( path_length < 0 ) || ( (size_t)path_length >= a_path_capacity ) )
        {
            return NULL;
        }

        const int descriptor = open( a_path, O_CREAT | O_EXCL | O_RDWR, 0664 );
        if( descriptor >= 0 )
        {
            FILE *file = fdopen( descriptor, "wb+" );
            if( NULL == file )
            {
                close( descriptor );
                (void)remove( a_path );
            }
            return file;
        }
        if( EEXIST != errno )
        {
            return NULL;
        }
    }

    errno = EEXIST;
    return NULL;
}

static bool trim_and_normalize_recording( void )
{
    if( NULL == record_file || 0U == data_bytes || 0U == recording_wav_sample_rate )
    {
        return true;
    }

    const uint32_t start_samples =
        (uint32_t)( (uint64_t)recording_wav_sample_rate * RECORDER_TRIM_START_MS / 1000ULL );
    const uint32_t end_samples =
        (uint32_t)( (uint64_t)recording_wav_sample_rate * RECORDER_TRIM_END_MS / 1000ULL );
    const uint32_t start_bytes = start_samples * sizeof( int16_t );
    const uint32_t end_bytes = end_samples * sizeof( int16_t );
    if( start_bytes + end_bytes >= data_bytes )
    {
        ESP_LOGW( TAG,
                  "trim/filter/normalize: too short bytes=%u start=%u end=%u; leaving untrimmed",
                  (unsigned)data_bytes,
                  (unsigned)start_bytes,
                  (unsigned)end_bytes );
        return true;
    }
    if( 0 != fflush( record_file ) )
        return false;

    const uint32_t retained_bytes = data_bytes - start_bytes - end_bytes;
    int16_t samples[ RECORDER_DMA_READ_SAMPLES ];
    uint64_t count_total = 0U;
    int64_t signed_sum = 0;
    uint32_t remaining = retained_bytes;
    if( 0 != fseek( record_file, RECORDER_WAV_HEADER_SIZE + (long)start_bytes, SEEK_SET ) )
        return false;
    while( remaining > 0U )
    {
        size_t count = remaining / sizeof( int16_t );
        if( count > RECORDER_DMA_READ_SAMPLES )
            count = RECORDER_DMA_READ_SAMPLES;
        const size_t read = fread( samples, sizeof( int16_t ), count, record_file );
        if( 0U == read )
            return false;
        for( size_t i = 0; i < read; ++i )
            signed_sum += samples[ i ];
        count_total += read;
        remaining -= (uint32_t)( read * sizeof( int16_t ) );
    }
    if( 0U == count_total )
        return true;
    const int32_t dc_mean = (int32_t)( signed_sum / (int64_t)count_total );

    uint64_t absolute_sum = 0U;
    uint32_t filtered_peak = 0U;
    rfscout_audio_highpass_t analysis_filter;
    audio_highpass_reset( &analysis_filter );
    remaining = retained_bytes;
    if( 0 != fseek( record_file, RECORDER_WAV_HEADER_SIZE + (long)start_bytes, SEEK_SET ) )
        return false;
    while( remaining > 0U )
    {
        size_t count = remaining / sizeof( int16_t );
        if( count > RECORDER_DMA_READ_SAMPLES )
            count = RECORDER_DMA_READ_SAMPLES;
        const size_t read = fread( samples, sizeof( int16_t ), count, record_file );
        if( 0U == read )
            return false;
        for( size_t i = 0; i < read; ++i )
        {
            const float filtered = audio_highpass_process(
                &analysis_filter, (float)( (int32_t)samples[ i ] - dc_mean ) );
            const float magnitude = fabsf( filtered );
            const uint32_t absolute =
                (uint32_t)( magnitude > (float)UINT32_MAX ? (float)UINT32_MAX : magnitude );
            absolute_sum += absolute;
            if( absolute > filtered_peak )
                filtered_peak = absolute;
        }
        remaining -= (uint32_t)( read * sizeof( int16_t ) );
    }
    const uint32_t average = (uint32_t)( absolute_sum / count_total );
    uint32_t gain_q10 = 1024U;
    if( average >= RECORDER_NORMALIZE_MIN_SIGNAL && average < RECORDER_NORMALIZE_TARGET_AVERAGE )
    {
        gain_q10 = (uint32_t)( (uint64_t)RECORDER_NORMALIZE_TARGET_AVERAGE * 1024ULL / average );
        if( gain_q10 > RECORDER_NORMALIZE_MAX_GAIN_Q10 )
            gain_q10 = RECORDER_NORMALIZE_MAX_GAIN_Q10;
    }

    uint64_t clipped = 0U;
    uint32_t source_offset = RECORDER_WAV_HEADER_SIZE + start_bytes;
    uint32_t destination_offset = RECORDER_WAV_HEADER_SIZE;
    rfscout_audio_highpass_t write_filter;
    audio_highpass_reset( &write_filter );
    remaining = retained_bytes;
    while( remaining > 0U )
    {
        size_t count = remaining / sizeof( int16_t );
        if( count > RECORDER_DMA_READ_SAMPLES )
            count = RECORDER_DMA_READ_SAMPLES;
        if( 0 != fseek( record_file, (long)source_offset, SEEK_SET ) )
            return false;
        const size_t read = fread( samples, sizeof( int16_t ), count, record_file );
        if( 0U == read )
            return false;
        for( size_t i = 0; i < read; ++i )
        {
            const float filtered =
                audio_highpass_process( &write_filter, (float)( (int32_t)samples[ i ] - dc_mean ) );
            int32_t scaled = (int32_t)( filtered * (float)gain_q10 / 1024.0f );
            if( scaled > INT16_MAX )
            {
                scaled = INT16_MAX;
                ++clipped;
            }
            else if( scaled < INT16_MIN )
            {
                scaled = INT16_MIN;
                ++clipped;
            }
            samples[ i ] = (int16_t)scaled;
        }
        if( 0 != fseek( record_file, (long)destination_offset, SEEK_SET ) ||
            read != fwrite( samples, sizeof( int16_t ), read, record_file ) )
            return false;
        const uint32_t bytes = (uint32_t)( read * sizeof( int16_t ) );
        source_offset += bytes;
        destination_offset += bytes;
        remaining -= bytes;
    }
    if( 0 != fflush( record_file ) ||
        0 != ftruncate( fileno( record_file ),
                        (off_t)( RECORDER_WAV_HEADER_SIZE + retained_bytes ) ) )
        return false;
    ESP_LOGI( TAG,
              "trim/filter/normalize: start=%ums end=%ums bytes=%u->%u dc=%ld alpha=%.2f "
              "avg=%u peak=%u gain=%u.%02ux clipped=%llu/%llu",
              RECORDER_TRIM_START_MS,
              RECORDER_TRIM_END_MS,
              (unsigned)data_bytes,
              (unsigned)retained_bytes,
              (long)dc_mean,
              (double)RFSCOUT_AUDIO_HIGHPASS_ALPHA,
              (unsigned)average,
              (unsigned)filtered_peak,
              (unsigned)( gain_q10 / 1024U ),
              (unsigned)( ( gain_q10 % 1024U ) * 100U / 1024U ),
              (unsigned long long)clipped,
              (unsigned long long)count_total );
    data_bytes = retained_bytes;
    return true;
}

static bool finalize_recording( void )
{
    if( NULL == record_file )
    {
        return true;
    }

    bool ok = trim_and_normalize_recording();
    uint8_t header[ RECORDER_WAV_HEADER_SIZE ];
    build_wav_header( header, data_bytes, recording_wav_sample_rate );
    ok = ok && ( 0 == fflush( record_file ) );
    ok = ok && ( 0 == fseek( record_file, 0L, SEEK_SET ) );
    if( ok )
    {
        ok = ( sizeof( header ) == fwrite( header, 1U, sizeof( header ), record_file ) );
    }
    ok = ok && ( 0 == fflush( record_file ) );
    if( 0 != fclose( record_file ) )
    {
        ok = false;
    }
    record_file = NULL;
    if( !ok )
    {
        ESP_LOGE( TAG, "recording finalization failed" );
    }
    return ok;
}

static void record_worker( void *a_argument )
{
    (void)a_argument;
    int16_t dma_samples[ RECORDER_DMA_READ_SAMPLES ];
    bool failed = false;
    unsigned timeout_streak = 0U;

    ESP_LOGI( TAG,
              "microphone wake-up: discarding first %u ms after capture start",
              RECORDER_MIC_WAKEUP_MS );
    const int64_t wakeup_until_us = esp_timer_get_time() + (int64_t)RECORDER_MIC_WAKEUP_MS * 1000LL;
    while( record_running && esp_timer_get_time() < wakeup_until_us )
    {
        size_t discarded = 0U;
        const esp_err_t result = i2s_channel_read(
            rx_channel, dma_samples, sizeof( dma_samples ), &discarded, pdMS_TO_TICKS( 20 ) );
        if( ESP_OK != result && ESP_ERR_TIMEOUT != result )
        {
            ESP_LOGW( TAG, "microphone wake-up read failed: %s", esp_err_to_name( result ) );
            failed = true;
            break;
        }
    }

    recording_started_us = esp_timer_get_time();
    diagnostic_started_us = recording_started_us;
    diagnostic_input_bytes = 0U;
    diagnostic_output_bytes = 0U;
    diagnostic_zero_samples = 0U;
    diagnostic_sample_count = 0U;
    diagnostic_min_sample = INT16_MAX;
    diagnostic_max_sample = INT16_MIN;

    while( record_running && !failed )
    {
        size_t bytes = 0U;
        const esp_err_t result = i2s_channel_read(
            rx_channel, dma_samples, sizeof( dma_samples ), &bytes, pdMS_TO_TICKS( 100 ) );
        if( ESP_ERR_TIMEOUT == result )
        {
            ++timeout_streak;
            if( 10U == timeout_streak )
            {
                ESP_LOGW( TAG, "microphone input timed out 10 consecutive reads" );
            }
            continue;
        }
        if( ESP_OK != result )
        {
            if( record_running )
            {
                ESP_LOGW( TAG, "microphone read failed: %s", esp_err_to_name( result ) );
                failed = true;
            }
            break;
        }
        timeout_streak = 0U;
        diagnostic_input_bytes += (uint32_t)bytes;

        const size_t samples = bytes / sizeof( int16_t );
        if( !diagnostic_initial_snapshot_logged && samples > 0U )
        {
            log_pcm_snapshot( "PCM first", dma_samples, samples );
            diagnostic_initial_snapshot_logged = true;
        }
        uint64_t absolute_sum = 0U;
        int64_t signed_sum = 0;
        uint32_t absolute_peak = 0U;
        for( size_t index = 0U; index < samples; ++index )
        {
            const int16_t sample = dma_samples[ index ];
            const uint32_t absolute = (uint32_t)( sample < 0 ? -(int32_t)sample : sample );
            absolute_sum += absolute;
            signed_sum += sample;
            if( absolute > absolute_peak )
            {
                absolute_peak = absolute;
            }
            if( 0 == sample )
            {
                ++diagnostic_zero_samples;
            }
            if( sample < diagnostic_min_sample )
            {
                diagnostic_min_sample = sample;
            }
            if( sample > diagnostic_max_sample )
            {
                diagnostic_max_sample = sample;
            }
        }
        diagnostic_sample_count += (uint32_t)samples;
        if( !diagnostic_signal_snapshot_logged &&
            absolute_peak >= RECORDER_SIGNAL_SNAPSHOT_THRESHOLD )
        {
            const int32_t chunk_mean =
                samples > 0U ? (int32_t)( signed_sum / (int64_t)samples ) : 0;
            ESP_LOGI( TAG,
                      "PCM signal chunk: samples=%u mean=%ld peak=%u",
                      (unsigned)samples,
                      (long)chunk_mean,
                      (unsigned)absolute_peak );
            log_pcm_snapshot( "PCM signal", dma_samples, samples );
            diagnostic_signal_snapshot_logged = true;
        }

        if( samples > 0U )
        {
            const uint32_t average = (uint32_t)( absolute_sum / samples );
            const uint8_t level = (uint8_t)( ( average * 100U ) / 32767U );
            const uint8_t peak = (uint8_t)( ( absolute_peak * 100U ) / 32767U );
            live_level_percent = (uint8_t)( ( live_level_percent * 3U + level ) / 4U );
            if( peak >= live_peak_percent )
            {
                live_peak_percent = peak;
            }
            else if( live_peak_percent > 0U )
            {
                --live_peak_percent;
            }
        }

        if( ( samples > 0U ) &&
            ( samples != fwrite( dma_samples, sizeof( int16_t ), samples, record_file ) ) )
        {
            ESP_LOGW( TAG, "recording write failed" );
            failed = true;
            break;
        }
        const uint32_t written_bytes = (uint32_t)( samples * sizeof( int16_t ) );
        data_bytes += written_bytes;
        diagnostic_output_bytes += written_bytes;

        const int64_t now_us = esp_timer_get_time();
        if( ( now_us - diagnostic_started_us ) >= RECORDER_DIAGNOSTIC_INTERVAL_US )
        {
            const uint32_t elapsed_ms = (uint32_t)( ( now_us - diagnostic_started_us ) / 1000LL );
            const uint32_t input_rate =
                elapsed_ms > 0U
                    ? (uint32_t)( (uint64_t)diagnostic_input_bytes * 1000ULL / elapsed_ms )
                    : 0U;
            const uint32_t output_rate =
                elapsed_ms > 0U
                    ? (uint32_t)( (uint64_t)diagnostic_output_bytes * 1000ULL / elapsed_ms )
                    : 0U;
            const unsigned zero_percent = diagnostic_sample_count > 0U
                                              ? (unsigned)( (uint64_t)diagnostic_zero_samples *
                                                            100ULL / diagnostic_sample_count )
                                              : 0U;
            ESP_LOGI( TAG,
                      "input diag: elapsed=%ums reads=%uB/s pcm=%uB/s expected=%uB/s samples=%u "
                      "zero=%u%% min=%d max=%d level=%u%% peak=%u%%",
                      (unsigned)elapsed_ms,
                      (unsigned)input_rate,
                      (unsigned)output_rate,
                      (unsigned)RECORDER_EXPECTED_BYTES_PER_SECOND,
                      (unsigned)diagnostic_sample_count,
                      zero_percent,
                      (int)diagnostic_min_sample,
                      (int)diagnostic_max_sample,
                      (unsigned)live_level_percent,
                      (unsigned)live_peak_percent );
            diagnostic_started_us = now_us;
            diagnostic_input_bytes = 0U;
            diagnostic_output_bytes = 0U;
            diagnostic_zero_samples = 0U;
            diagnostic_sample_count = 0U;
            diagnostic_min_sample = INT16_MAX;
            diagnostic_max_sample = INT16_MIN;
        }
    }

    if( RECORDER_INPUT_PDM == active_input && recording_started_us > 0 )
    {
        const int64_t total_us = esp_timer_get_time() - recording_started_us;
        if( total_us > 0 )
        {
            const uint32_t effective_rate = (uint32_t)( (uint64_t)data_bytes * 1000000ULL /
                                                        (uint64_t)total_us / sizeof( int16_t ) );
            if( effective_rate >= RFSCOUT_RECORDER_WAV_RATE_MIN &&
                effective_rate <= RFSCOUT_RECORDER_WAV_RATE_MAX )
            {
                recording_wav_sample_rate = effective_rate;
            }
            const uint32_t pcm_ratio_permille =
                (uint32_t)( (uint64_t)effective_rate * 1000ULL / RFSCOUT_RECORDER_SAMPLE_RATE );
            const uint32_t derived_request_for_16k =
                effective_rate > 0U ? (uint32_t)( (uint64_t)RFSCOUT_RECORDER_SAMPLE_RATE *
                                                  16000ULL / effective_rate )
                                    : 0U;
            ESP_LOGI( TAG,
                      "PDM final: wall=%lldms pcm_bytes=%u effective_pcm=%u samples/s wav_rate=%u",
                      (long long)( total_us / 1000LL ),
                      (unsigned)data_bytes,
                      (unsigned)effective_rate,
                      (unsigned)recording_wav_sample_rate );
            ESP_LOGI( TAG,
                      "PDM calibration result: request=%u measured_clk=%u effective_pcm=%u "
                      "pcm_ratio=%u.%u%% derived_request_for_16k=%u",
                      (unsigned)RFSCOUT_RECORDER_SAMPLE_RATE,
                      (unsigned)measured_pdm_clock_hz,
                      (unsigned)effective_rate,
                      (unsigned)( pcm_ratio_permille / 10U ),
                      (unsigned)( pcm_ratio_permille % 10U ),
                      (unsigned)derived_request_for_16k );
        }
    }

    record_running = false;
    release_channel();
    const bool finalized = finalize_recording();
    record_task = NULL;
    if( failed || !finalized )
    {
        state = RFSCOUT_RECORDER_ERROR;
    }
    else if( storage_is_available() )
    {
        state = RFSCOUT_RECORDER_IDLE;
        (void)recorder_refresh();
        active_name[ 0 ] = '\0';
    }
    else
    {
        state = RFSCOUT_RECORDER_NO_STORAGE;
    }
    if( NULL != record_done )
    {
        (void)xSemaphoreGive( record_done );
    }
    vTaskDelete( NULL );
}

static bool wait_for_record_worker( uint32_t a_timeout_ms )
{
    record_running = false;
    if( NULL == record_task )
    {
        return true;
    }
    if( NULL == record_done ||
        pdTRUE != xSemaphoreTake( record_done, pdMS_TO_TICKS( a_timeout_ms ) ) )
    {
        ESP_LOGE( TAG, "recording worker still busy after %u ms", (unsigned)a_timeout_ms );
        state = RFSCOUT_RECORDER_SAVING;
        return false;
    }
    return true;
}

static void compute_monitor_fft( const float *a_samples )
{
    float re[ RECORDER_MONITOR_FFT_SIZE ];
    float im[ RECORDER_MONITOR_FFT_SIZE ];
    for( size_t i = 0; i < RECORDER_MONITOR_FFT_SIZE; ++i )
    {
        const float window = 0.5f - 0.5f * cosf( 2.0f * (float)M_PI * (float)i /
                                                 (float)( RECORDER_MONITOR_FFT_SIZE - 1U ) );
        re[ i ] = a_samples[ i ] * window;
        im[ i ] = 0.0f;
    }
    for( size_t i = 1U, j = 0U; i < RECORDER_MONITOR_FFT_SIZE; ++i )
    {
        size_t bit = RECORDER_MONITOR_FFT_SIZE >> 1U;
        for( ; j & bit; bit >>= 1U )
            j ^= bit;
        j ^= bit;
        if( i < j )
        {
            float tr = re[ i ];
            re[ i ] = re[ j ];
            re[ j ] = tr;
            tr = im[ i ];
            im[ i ] = im[ j ];
            im[ j ] = tr;
        }
    }
    for( size_t len = 2U; len <= RECORDER_MONITOR_FFT_SIZE; len <<= 1U )
    {
        const float angle = -2.0f * (float)M_PI / (float)len;
        const float wlen_re = cosf( angle ), wlen_im = sinf( angle );
        for( size_t base = 0U; base < RECORDER_MONITOR_FFT_SIZE; base += len )
        {
            float wr = 1.0f, wi = 0.0f;
            for( size_t j = 0U; j < len / 2U; ++j )
            {
                const size_t even = base + j, odd = even + len / 2U;
                const float vr = re[ odd ] * wr - im[ odd ] * wi;
                const float vi = re[ odd ] * wi + im[ odd ] * wr;
                const float ur = re[ even ], ui = im[ even ];
                re[ even ] = ur + vr;
                im[ even ] = ui + vi;
                re[ odd ] = ur - vr;
                im[ odd ] = ui - vi;
                const float next_wr = wr * wlen_re - wi * wlen_im;
                wi = wr * wlen_im + wi * wlen_re;
                wr = next_wr;
            }
        }
    }

    float db_bins[ RFSCOUT_RECORDER_MONITOR_BINS ];
    float sorted[ RFSCOUT_RECORDER_MONITOR_BINS ];
    for( size_t bin = 0U; bin < RFSCOUT_RECORDER_MONITOR_BINS; ++bin )
    {
        const size_t fft_bin = RECORDER_MONITOR_FIRST_FFT_BIN + bin;
        const float mag = sqrtf( re[ fft_bin ] * re[ fft_bin ] + im[ fft_bin ] * im[ fft_bin ] ) /
                          (float)RECORDER_MONITOR_FFT_SIZE;
        db_bins[ bin ] = 20.0f * log10f( mag + 1.0f );
        sorted[ bin ] = db_bins[ bin ];
    }

    for( size_t i = 1U; i < RFSCOUT_RECORDER_MONITOR_BINS; ++i )
    {
        const float value = sorted[ i ];
        size_t j = i;
        while( j > 0U && sorted[ j - 1U ] > value )
        {
            sorted[ j ] = sorted[ j - 1U ];
            --j;
        }
        sorted[ j ] = value;
    }

    const float floor_target = sorted[ RFSCOUT_RECORDER_MONITOR_BINS / 4U ];
    const float peak_target = sorted[ RFSCOUT_RECORDER_MONITOR_BINS - 1U ];
    float ceiling_target = peak_target + RECORDER_MONITOR_HEADROOM_DB;
    if( ceiling_target < floor_target + RECORDER_MONITOR_MIN_RANGE_DB )
    {
        ceiling_target = floor_target + RECORDER_MONITOR_MIN_RANGE_DB;
    }

    if( !monitor_range_initialized )
    {
        monitor_floor_db = floor_target;
        monitor_ceiling_db = ceiling_target;
        monitor_range_initialized = true;
    }
    else
    {
        monitor_floor_db += ( floor_target - monitor_floor_db ) * RECORDER_MONITOR_RANGE_SMOOTH;
        if( ceiling_target > monitor_ceiling_db )
        {
            monitor_ceiling_db = ceiling_target;
        }
        else
        {
            monitor_ceiling_db +=
                ( ceiling_target - monitor_ceiling_db ) * RECORDER_MONITOR_RANGE_SMOOTH;
        }
        if( monitor_ceiling_db < monitor_floor_db + RECORDER_MONITOR_MIN_RANGE_DB )
        {
            monitor_ceiling_db = monitor_floor_db + RECORDER_MONITOR_MIN_RANGE_DB;
        }
    }

    const float span = monitor_ceiling_db - monitor_floor_db;
    const int rounded_span = (int)( span + 0.5f );
    monitor_range_db =
        (uint8_t)( rounded_span < 1 ? 1 : ( rounded_span > 255 ? 255 : rounded_span ) );
    for( size_t bin = 0U; bin < RFSCOUT_RECORDER_MONITOR_BINS; ++bin )
    {
        int level =
            span > 0.0f ? (int)( ( db_bins[ bin ] - monitor_floor_db ) * 100.0f / span ) : 0;
        if( level < 0 )
            level = 0;
        if( level > 100 )
            level = 100;
        monitor_spectrum[ bin ] = (uint8_t)level;
    }
}

static void monitor_process_samples( const int16_t *a_samples, size_t a_count )
{
    for( size_t i = 0U; i < a_count; ++i )
    {
        monitor_fft_frame[ monitor_fft_frame_count++ ] =
            audio_highpass_process( &monitor_highpass, (float)a_samples[ i ] );
        if( RECORDER_MONITOR_FFT_SIZE == monitor_fft_frame_count )
        {
            compute_monitor_fft( monitor_fft_frame );
            monitor_fft_frame_count = 0U;
        }
    }
}

static void monitor_worker( void *a_argument )
{
    (void)a_argument;
    int16_t samples[ RECORDER_DMA_READ_SAMPLES ];
    const int64_t wakeup_until = esp_timer_get_time() + (int64_t)RECORDER_MIC_WAKEUP_MS * 1000LL;
    while( monitor_running && esp_timer_get_time() < wakeup_until )
    {
        size_t discarded = 0U;
        (void)i2s_channel_read(
            rx_channel, samples, sizeof( samples ), &discarded, pdMS_TO_TICKS( 20 ) );
    }

    int64_t rate_started_us = esp_timer_get_time();
    uint32_t rate_bytes = 0U;
    unsigned timeout_streak = 0U;
    while( monitor_running )
    {
        size_t bytes = 0U;
        const esp_err_t result = i2s_channel_read(
            rx_channel, samples, sizeof( samples ), &bytes, pdMS_TO_TICKS( 100 ) );
        if( ESP_ERR_TIMEOUT == result )
        {
            ++timeout_streak;
            if( 10U == timeout_streak || 0U == ( timeout_streak % 100U ) )
            {
                ESP_LOGW( TAG, "spectrum input timed out %u consecutive reads", timeout_streak );
            }
            continue;
        }
        if( ESP_OK != result )
        {
            ESP_LOGW( TAG, "spectrum input read failed: %s", esp_err_to_name( result ) );
            break;
        }
        timeout_streak = 0U;
        rate_bytes += (uint32_t)bytes;
        const int64_t now_us = esp_timer_get_time();
        const int64_t rate_elapsed_us = now_us - rate_started_us;
        if( rate_elapsed_us >= RECORDER_DIAGNOSTIC_INTERVAL_US )
        {
            const uint32_t effective_rate =
                rate_elapsed_us > 0 ? (uint32_t)( (uint64_t)rate_bytes * 1000000ULL /
                                                  (uint64_t)rate_elapsed_us / sizeof( int16_t ) )
                                    : 0U;
            if( effective_rate >= RFSCOUT_RECORDER_WAV_RATE_MIN &&
                effective_rate <= RFSCOUT_RECORDER_WAV_RATE_MAX )
            {
                monitor_sample_rate_hz = effective_rate;
            }
            rate_started_us = now_us;
            rate_bytes = 0U;
        }
        const size_t count = bytes / sizeof( samples[ 0 ] );
        monitor_process_samples( samples, count );
    }
    release_channel();
    monitor_running = false;
    monitor_task = NULL;
    if( RFSCOUT_RECORDER_MONITORING == state )
        state = storage_is_available() ? RFSCOUT_RECORDER_IDLE : RFSCOUT_RECORDER_NO_STORAGE;
    vTaskDelete( NULL );
}

esp_err_t recorder_start_monitor( void )
{
    const rfscout_recorder_state_t current = recorder_state();
    if( ( RFSCOUT_RECORDER_IDLE != current ) && ( RFSCOUT_RECORDER_NO_STORAGE != current ) )
        return ESP_ERR_INVALID_STATE;
    memset( monitor_spectrum, 0, sizeof( monitor_spectrum ) );
    monitor_range_initialized = false;
    monitor_floor_db = 0.0f;
    monitor_ceiling_db = 0.0f;
    monitor_range_db = (uint8_t)RECORDER_MONITOR_MIN_RANGE_DB;
    monitor_sample_rate_hz = RFSCOUT_RECORDER_SAMPLE_RATE;
    audio_highpass_reset( &monitor_highpass );
    monitor_fft_frame_count = 0U;
    esp_err_t result = open_microphone();
    if( ESP_OK != result )
        return result;
    monitor_running = true;
    state = RFSCOUT_RECORDER_MONITORING;
    if( pdPASS != xTaskCreate( monitor_worker,
                               "mic_monitor",
                               RECORDER_MONITOR_TASK_STACK,
                               NULL,
                               RECORDER_MONITOR_TASK_PRIORITY,
                               &monitor_task ) )
    {
        monitor_running = false;
        release_channel();
        state = RFSCOUT_RECORDER_ERROR;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void recorder_stop_monitor( void )
{
    if( RFSCOUT_RECORDER_MONITORING != state && NULL == monitor_task )
        return;
    monitor_running = false;
    for( unsigned i = 0U; i < 20U && NULL != monitor_task; ++i )
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    state = storage_is_available() ? RFSCOUT_RECORDER_IDLE : RFSCOUT_RECORDER_NO_STORAGE;
}

size_t recorder_monitor_bins( uint8_t *a_bins, size_t a_capacity )
{
    if( NULL == a_bins || 0U == a_capacity )
        return 0U;
    const size_t count =
        a_capacity < RFSCOUT_RECORDER_MONITOR_BINS ? a_capacity : RFSCOUT_RECORDER_MONITOR_BINS;
    memcpy( a_bins, monitor_spectrum, count );
    return count;
}

uint32_t recorder_monitor_bin_frequency_hz( size_t a_bin )
{
    if( a_bin >= RFSCOUT_RECORDER_MONITOR_BINS )
    {
        return 0U;
    }
    return (uint32_t)( ( RECORDER_MONITOR_FIRST_FFT_BIN + a_bin ) *
                       (uint64_t)monitor_sample_rate_hz / RECORDER_MONITOR_FFT_SIZE );
}

uint8_t recorder_monitor_range_db( void )
{
    return monitor_range_db;
}

esp_err_t recorder_init( void )
{
    if( NULL == record_done )
    {
        record_done = xSemaphoreCreateBinary();
        if( NULL == record_done )
        {
            state = RFSCOUT_RECORDER_ERROR;
            return ESP_ERR_NO_MEM;
        }
    }
    return recorder_refresh();
}

esp_err_t recorder_refresh( void )
{
    if( ( RFSCOUT_RECORDER_RECORDING == state ) || ( RFSCOUT_RECORDER_SAVING == state ) ||
        ( RFSCOUT_RECORDER_PLAYING == state ) )
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
    while( NULL != ( entry = readdir( directory ) ) )
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
        struct stat file_status;
        if( ( path_length < 0 ) || ( (size_t)path_length >= sizeof( path ) ) ||
            !validate_wav( path, &candidate ) || ( 0 != stat( path, &file_status ) ) )
        {
            continue;
        }
        candidate.modified_time = (int64_t)file_status.st_mtime;

        if( recording_count < RFSCOUT_RECORDER_MAX_FILES )
        {
            recordings[ recording_count++ ] = candidate;
            continue;
        }

        size_t oldest = 0U;
        for( size_t index = 1U; index < recording_count; ++index )
        {
            if( recordings[ index ].modified_time < recordings[ oldest ].modified_time )
            {
                oldest = index;
            }
        }
        if( candidate.modified_time > recordings[ oldest ].modified_time )
        {
            recordings[ oldest ] = candidate;
        }
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

    if( NULL == record_done )
    {
        state = RFSCOUT_RECORDER_ERROR;
        return ESP_ERR_INVALID_STATE;
    }
    (void)xSemaphoreTake( record_done, 0U );

    char path[ 160 ];
    record_file = open_unique_recording( path, sizeof( path ) );
    if( NULL == record_file )
    {
        state = RFSCOUT_RECORDER_ERROR;
        return ESP_FAIL;
    }

    uint8_t header[ RECORDER_WAV_HEADER_SIZE ];
    build_wav_header( header, 0U, RFSCOUT_RECORDER_SAMPLE_RATE );
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
    live_level_percent = 0U;
    live_peak_percent = 0U;
    active_pdm_slot = 1;
    recording_started_us = 0;
    diagnostic_initial_snapshot_logged = false;
    diagnostic_signal_snapshot_logged = false;
    measured_pdm_clock_hz = 0U;
    recording_wav_sample_rate = RFSCOUT_RECORDER_SAMPLE_RATE;
    record_running = true;
    state = RFSCOUT_RECORDER_RECORDING;
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

    ESP_LOGI( TAG, "recording %s", path );
    return ESP_OK;
}

void recorder_request_stop_recording( void )
{
    if( RFSCOUT_RECORDER_RECORDING != state )
    {
        return;
    }

    record_running = false;
    state = RFSCOUT_RECORDER_SAVING;
    ESP_LOGI( TAG, "recording stop requested; saving %s", active_name );
}

void recorder_stop_recording( void )
{
    if( RFSCOUT_RECORDER_RECORDING == state )
    {
        recorder_request_stop_recording();
    }
    if( RFSCOUT_RECORDER_SAVING != state && !record_running && NULL == record_task )
    {
        return;
    }

    (void)wait_for_record_worker( RECORDER_STOP_TIMEOUT_MS );
}

bool recorder_prepare_storage_shutdown( uint32_t a_timeout_ms )
{
    if( RFSCOUT_RECORDER_RECORDING == state || record_running || NULL != record_task )
    {
        if( !wait_for_record_worker( a_timeout_ms ) )
        {
            return false;
        }
        active_name[ 0 ] = '\0';
    }

    if( RFSCOUT_RECORDER_PLAYING == state || audio_output_playback_active() )
    {
        audio_output_stop_playback();
        const TickType_t start = xTaskGetTickCount();
        const TickType_t timeout = pdMS_TO_TICKS( a_timeout_ms );
        while( audio_output_playback_active() )
        {
            if( ( xTaskGetTickCount() - start ) >= timeout )
            {
                ESP_LOGE( TAG, "playback did not stop within %u ms", (unsigned)a_timeout_ms );
                state = RFSCOUT_RECORDER_ERROR;
                return false;
            }
            vTaskDelay( pdMS_TO_TICKS( 10 ) );
        }
    }

    return true;
}

esp_err_t recorder_delete_file( size_t a_index )
{
    if( RFSCOUT_RECORDER_IDLE != state || a_index >= recording_count )
    {
        return ESP_ERR_INVALID_STATE;
    }

    char path[ sizeof( RFSCOUT_RECORDER_DIRECTORY ) + RFSCOUT_RECORDER_NAME_MAX + 1U ];
    const int length = snprintf(
        path, sizeof( path ), "%s/%s", RFSCOUT_RECORDER_DIRECTORY, recordings[ a_index ].name );
    if( length < 0 || (size_t)length >= sizeof( path ) )
    {
        return ESP_ERR_INVALID_SIZE;
    }

    if( 0 != remove( path ) )
    {
        ESP_LOGW( TAG, "delete failed %s errno=%d", path, errno );
        return ESP_FAIL;
    }

    ESP_LOGI( TAG, "deleted recording %s", path );
    return recorder_refresh();
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

    playback_started_us = esp_timer_get_time();
    state = RFSCOUT_RECORDER_PLAYING;
    return ESP_OK;
}

void recorder_stop( void )
{
    if( RFSCOUT_RECORDER_RECORDING == state || RFSCOUT_RECORDER_SAVING == state || record_running ||
        NULL != record_task )
    {
        recorder_stop_recording();
        return;
    }

    if( RFSCOUT_RECORDER_MONITORING == state )
    {
        recorder_stop_monitor();
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
    if( !storage_is_available() && ( RFSCOUT_RECORDER_RECORDING != state ) &&
        ( RFSCOUT_RECORDER_SAVING != state ) && ( RFSCOUT_RECORDER_MONITORING != state ) )
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

uint32_t recorder_recording_elapsed_seconds( void )
{
    if( RFSCOUT_RECORDER_RECORDING != state || recording_started_us <= 0 )
    {
        return 0U;
    }

    const int64_t elapsed_us = esp_timer_get_time() - recording_started_us;
    return elapsed_us > 0 ? (uint32_t)( elapsed_us / 1000000LL ) : 0U;
}

size_t recorder_file_count( void )
{
    return recording_count;
}

uint8_t recorder_level_percent( void )
{
    return live_level_percent;
}

uint8_t recorder_peak_percent( void )
{
    return live_peak_percent;
}

const char *recorder_input_slot_name( void )
{
    if( RECORDER_INPUT_ES7210 == active_input )
    {
        return "MIC1";
    }
    if( active_pdm_slot < 0 )
    {
        return "AUTO";
    }
    return ( 0 == active_pdm_slot ) ? "RIGHT" : "LEFT";
}

void recorder_set_playback_volume( uint8_t a_volume_percent )
{
    audio_output_set_playback_volume( a_volume_percent );
}

uint32_t recorder_playback_elapsed_seconds( void )
{
    if( playback_started_us <= 0 || !audio_output_playback_active() )
    {
        return 0U;
    }
    return (uint32_t)( ( esp_timer_get_time() - playback_started_us ) / 1000000LL );
}

const rfscout_recording_t *recorder_file( size_t a_index )
{
    return ( a_index < recording_count ) ? &recordings[ a_index ] : NULL;
}
