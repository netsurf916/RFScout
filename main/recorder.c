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
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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
#define RECORDER_SIGNAL_SNAPSHOT_THRESHOLD 4000U
#define RECORDER_EXPECTED_BYTES_PER_SECOND                                                         \
    ( RFSCOUT_RECORDER_SAMPLE_RATE * RECORDER_CHANNELS * ( RECORDER_BITS_PER_SAMPLE / 8U ) )

static const char *TAG = "recorder";
static i2s_chan_handle_t rx_channel;
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
    write_u32_le( &a_header[ 24 ], RFSCOUT_RECORDER_WAV_SAMPLE_RATE );
    write_u32_le( &a_header[ 28 ],
                  RFSCOUT_RECORDER_WAV_SAMPLE_RATE * RECORDER_CHANNELS * RECORDER_BITS_PER_SAMPLE /
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
        RFSCOUT_RECORDER_WAV_SAMPLE_RATE != read_u32_le( &header[ 24 ] ) ||
        RECORDER_BITS_PER_SAMPLE != read_u16_le( &header[ 34 ] ) )
    {
        return false;
    }

    const uint32_t bytes = read_u32_le( &header[ 40 ] );
    if( NULL != a_recording )
    {
        a_recording->data_bytes = bytes;
        a_recording->duration_seconds =
            bytes / ( RFSCOUT_RECORDER_WAV_SAMPLE_RATE * RECORDER_CHANNELS *
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

static void log_pdm_clock_measurement( void )
{
    pcnt_unit_handle_t unit = NULL;
    pcnt_channel_handle_t channel = NULL;
    const pcnt_unit_config_t unit_config = { .low_limit = -1, .high_limit = 30000 };
    esp_err_t result = pcnt_new_unit( &unit_config, &unit );
    if( ESP_OK != result )
    {
        ESP_LOGW(
            TAG, "PDM clock measurement unavailable: pcnt unit: %s", esp_err_to_name( result ) );
        return;
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
        ESP_LOGI( TAG,
                  "PDM configured: I2S0 clk=GPIO%d data=GPIO%d rate=%u bits=%u channels=%u "
                  "slot=LEFT HPF=on expected_pcm=%u B/s",
                  RFSCOUT_BOARD_MIC_CLOCK_PIN,
                  RFSCOUT_BOARD_MIC_DATA_PIN,
                  RFSCOUT_RECORDER_SAMPLE_RATE,
                  RECORDER_BITS_PER_SAMPLE,
                  RECORDER_CHANNELS,
                  (unsigned)RECORDER_EXPECTED_BYTES_PER_SECOND );
        log_pdm_clock_measurement();
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
    bool failed = false;
    unsigned timeout_streak = 0U;

    ESP_LOGI( TAG,
              "microphone wake-up: discarding first %u ms after PDM clock start",
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
                ESP_LOGW( TAG, "PDM input timed out 10 consecutive reads" );
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
                      "PDM diag: elapsed=%ums reads=%uB/s pcm=%uB/s expected=%uB/s samples=%u "
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

    if( recording_started_us > 0 )
    {
        const int64_t total_us = esp_timer_get_time() - recording_started_us;
        if( total_us > 0 )
        {
            const uint32_t effective_rate = (uint32_t)( (uint64_t)data_bytes * 1000000ULL /
                                                        (uint64_t)total_us / sizeof( int16_t ) );
            ESP_LOGI( TAG,
                      "PDM final: wall=%lldms pcm_bytes=%u effective_pcm=%u samples/s wav_rate=%u",
                      (long long)( total_us / 1000LL ),
                      (unsigned)data_bytes,
                      (unsigned)effective_rate,
                      (unsigned)RFSCOUT_RECORDER_WAV_SAMPLE_RATE );
        }
    }

    record_running = false;
    release_channel();
    finalize_recording();
    if( failed )
    {
        state = RFSCOUT_RECORDER_ERROR;
    }
    record_task = NULL;
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
        ESP_LOGE( TAG, "recording worker did not stop within %u ms", (unsigned)a_timeout_ms );
        state = RFSCOUT_RECORDER_ERROR;
        return false;
    }
    return true;
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
        if( ( path_length < 0 ) || ( (size_t)path_length >= sizeof( path ) ) ||
            !validate_wav( path, &candidate ) )
        {
            continue;
        }

        if( recording_count < RFSCOUT_RECORDER_MAX_FILES )
        {
            recordings[ recording_count++ ] = candidate;
            continue;
        }

        size_t oldest = 0U;
        for( size_t index = 1U; index < recording_count; ++index )
        {
            if( strcmp( recordings[ index ].name, recordings[ oldest ].name ) < 0 )
            {
                oldest = index;
            }
        }
        if( strcmp( candidate.name, recordings[ oldest ].name ) > 0 )
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
    live_level_percent = 0U;
    live_peak_percent = 0U;
    active_pdm_slot = 1;
    recording_started_us = 0;
    diagnostic_initial_snapshot_logged = false;
    diagnostic_signal_snapshot_logged = false;
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
    if( RFSCOUT_RECORDER_RECORDING != state && !record_running && NULL == record_task )
    {
        return;
    }

    if( !wait_for_record_worker( RECORDER_STOP_TIMEOUT_MS ) )
    {
        return;
    }

    active_name[ 0 ] = '\0';
    if( RFSCOUT_RECORDER_ERROR == state )
    {
        return;
    }

    state = storage_is_available() ? RFSCOUT_RECORDER_IDLE : RFSCOUT_RECORDER_NO_STORAGE;
    if( storage_is_available() )
    {
        (void)recorder_refresh();
    }
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