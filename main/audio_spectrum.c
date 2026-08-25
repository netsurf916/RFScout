/**
 * audio_spectrum.c : DMA-fed PDM microphone FFT analyzer
 * Copyright 2026 Daniel Wilson
 * SPDX-License-Identifier: MIT
 */
#include "audio_spectrum.h"
#include "board.h"
#include "driver/i2s_pdm.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#define AUDIO_SAMPLE_RATE 48000U
#define AUDIO_FFT_SIZE 1024U
#define AUDIO_HOP_SIZE ( AUDIO_FFT_SIZE / 2U )
#define AUDIO_DMA_BUFFER_COUNT 8U
#define AUDIO_DMA_BUFFER_LENGTH 256U
#define AUDIO_DMA_READ_SAMPLES 512U
#define AUDIO_TASK_STACK 16384U
#define AUDIO_TASK_PRIORITY 5U
#define AUDIO_EPSILON 1.0e-9f
#define AUDIO_FFT_INTERVAL_US 40000LL
#define AUDIO_WARMUP_FRAMES 3U
#define AUDIO_FFT_NORMALIZATION ( 32768.0f * ( (float)AUDIO_FFT_SIZE / 2.0f ) )
#define AUDIO_DB_MIN (-120.0f)
#define AUDIO_DB_MAX 0.0f
#define AUDIO_DB_HISTOGRAM_BINS 121U
#define AUDIO_AUTO_FLOOR_PERCENT 15U
#define AUDIO_AUTO_CEILING_PERCENT 95U
#define AUDIO_AUTO_FLOOR_MARGIN_DB 4.0f
#define AUDIO_AUTO_CEILING_MARGIN_DB 8.0f
#define AUDIO_AUTO_MIN_SPAN_DB 24.0f
#define AUDIO_PEAK_DECAY_DB 1.25f

static const char *TAG = "audio_spectrum";
static TaskHandle_t task_handle;
static i2s_chan_handle_t rx_channel;
static volatile bool running;
static volatile bool frame_ready;
static volatile uint32_t overruns;
static audio_range_t selected_range = AUDIO_RANGE_AUDIBLE;
static audio_channel_t selected_channel = AUDIO_CHANNEL_LEFT;
static unsigned warmup_frames;
static uint8_t current_bins[ AUDIO_SPECTRUM_BINS ];
static uint8_t peak_bins[ AUDIO_SPECTRUM_BINS ];
static float peak_db[ AUDIO_SPECTRUM_BINS ];
static float scale_floor_db = -80.0f;
static float scale_ceiling_db = 0.0f;
static bool scale_ready;
static float hann_window[ AUDIO_FFT_SIZE ];
static bool hann_ready;
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;

static void range_bounds( audio_range_t a_range, uint32_t *a_low, uint32_t *a_high )
{
    switch( a_range )
    {
        case AUDIO_RANGE_INFRASONIC: *a_low = 1U; *a_high = 20U; break;
        case AUDIO_RANGE_ULTRASONIC: *a_low = 20000U; *a_high = 24000U; break;
        case AUDIO_RANGE_AUDIBLE:
        default: *a_low = 20U; *a_high = 20000U; break;
    }
}

static void reset_display_state( void )
{
    memset( current_bins, 0, sizeof( current_bins ) );
    memset( peak_bins, 0, sizeof( peak_bins ) );
    for( unsigned i = 0U; i < AUDIO_SPECTRUM_BINS; ++i ) peak_db[ i ] = AUDIO_DB_MIN;
    scale_floor_db = -80.0f;
    scale_ceiling_db = 0.0f;
    scale_ready = false;
    frame_ready = false;
    warmup_frames = AUDIO_WARMUP_FRAMES;
}

static void prepare_hann_window( void )
{
    if( hann_ready ) return;
    for( unsigned i = 0U; i < AUDIO_FFT_SIZE; ++i )
    {
        hann_window[ i ] = 0.5f - 0.5f * cosf( 2.0f * (float)M_PI * (float)i /
                                              (float)( AUDIO_FFT_SIZE - 1U ) );
    }
    hann_ready = true;
}

static void fft( float *a_real, float *a_imag )
{
    unsigned j = 0U;
    for( unsigned i = 1U; i < AUDIO_FFT_SIZE; ++i )
    {
        unsigned bit = AUDIO_FFT_SIZE >> 1U;
        while( j & bit )
        {
            j ^= bit;
            bit >>= 1U;
        }
        j ^= bit;
        if( i < j )
        {
            float temp = a_real[ i ];
            a_real[ i ] = a_real[ j ];
            a_real[ j ] = temp;
            temp = a_imag[ i ];
            a_imag[ i ] = a_imag[ j ];
            a_imag[ j ] = temp;
        }
    }

    for( unsigned length = 2U; length <= AUDIO_FFT_SIZE; length <<= 1U )
    {
        const float angle = -2.0f * (float)M_PI / (float)length;
        const float wr0 = cosf( angle );
        const float wi0 = sinf( angle );
        for( unsigned i = 0U; i < AUDIO_FFT_SIZE; i += length )
        {
            float wr = 1.0f;
            float wi = 0.0f;
            for( unsigned k = 0U; k < length / 2U; ++k )
            {
                const unsigned even = i + k;
                const unsigned odd = even + length / 2U;
                const float tr = wr * a_real[ odd ] - wi * a_imag[ odd ];
                const float ti = wr * a_imag[ odd ] + wi * a_real[ odd ];
                a_real[ odd ] = a_real[ even ] - tr;
                a_imag[ odd ] = a_imag[ even ] - ti;
                a_real[ even ] += tr;
                a_imag[ even ] += ti;
                const float next_wr = wr * wr0 - wi * wi0;
                wi = wr * wi0 + wi * wr0;
                wr = next_wr;
            }
        }
    }
}

static float percentile_db( const unsigned *a_histogram, unsigned a_percent )
{
    const unsigned target =
        ( AUDIO_SPECTRUM_BINS * a_percent + 99U ) / 100U;
    unsigned cumulative = 0U;
    for( unsigned i = 0U; i < AUDIO_DB_HISTOGRAM_BINS; ++i )
    {
        cumulative += a_histogram[ i ];
        if( cumulative >= target ) return AUDIO_DB_MIN + (float)i;
    }
    return AUDIO_DB_MAX;
}

static uint8_t scale_db( float a_db, float a_floor, float a_ceiling )
{
    if( a_db <= a_floor ) return 0U;
    if( a_db >= a_ceiling ) return 255U;
    return (uint8_t)( ( a_db - a_floor ) * 255.0f / ( a_ceiling - a_floor ) );
}

static void publish_fft( const int16_t *a_pcm )
{
    float real[ AUDIO_FFT_SIZE ];
    float imag[ AUDIO_FFT_SIZE ];
    float db_bins[ AUDIO_SPECTRUM_BINS ];
    unsigned histogram[ AUDIO_DB_HISTOGRAM_BINS ] = { 0U };
    int64_t sum = 0;

    for( unsigned i = 0U; i < AUDIO_FFT_SIZE; ++i ) sum += a_pcm[ i ];
    const float mean = (float)sum / (float)AUDIO_FFT_SIZE;

    for( unsigned i = 0U; i < AUDIO_FFT_SIZE; ++i )
    {
        real[ i ] = ( (float)a_pcm[ i ] - mean ) * hann_window[ i ];
        imag[ i ] = 0.0f;
    }
    fft( real, imag );

    uint32_t low;
    uint32_t high;
    range_bounds( selected_range, &low, &high );
    for( unsigned x = 0U; x < AUDIO_SPECTRUM_BINS; ++x )
    {
        const float hz = (float)low + (float)( high - low ) * (float)x /
                         (float)( AUDIO_SPECTRUM_BINS - 1U );
        unsigned bin = (unsigned)lroundf( hz * (float)AUDIO_FFT_SIZE /
                                          (float)AUDIO_SAMPLE_RATE );
        if( bin >= AUDIO_FFT_SIZE / 2U ) bin = AUDIO_FFT_SIZE / 2U - 1U;
        const float magnitude = sqrtf( real[ bin ] * real[ bin ] +
                                       imag[ bin ] * imag[ bin ] );
        float db = 20.0f * log10f( magnitude / AUDIO_FFT_NORMALIZATION + AUDIO_EPSILON );
        if( db < AUDIO_DB_MIN ) db = AUDIO_DB_MIN;
        if( db > AUDIO_DB_MAX ) db = AUDIO_DB_MAX;
        db_bins[ x ] = db;
        unsigned bucket = (unsigned)( db - AUDIO_DB_MIN );
        if( bucket >= AUDIO_DB_HISTOGRAM_BINS ) bucket = AUDIO_DB_HISTOGRAM_BINS - 1U;
        ++histogram[ bucket ];
    }

    float target_floor = percentile_db( histogram, AUDIO_AUTO_FLOOR_PERCENT ) -
                         AUDIO_AUTO_FLOOR_MARGIN_DB;
    float target_ceiling = percentile_db( histogram, AUDIO_AUTO_CEILING_PERCENT ) +
                           AUDIO_AUTO_CEILING_MARGIN_DB;
    if( target_floor < AUDIO_DB_MIN ) target_floor = AUDIO_DB_MIN;
    if( target_ceiling > AUDIO_DB_MAX ) target_ceiling = AUDIO_DB_MAX;
    if( ( target_ceiling - target_floor ) < AUDIO_AUTO_MIN_SPAN_DB )
    {
        const float center = ( target_ceiling + target_floor ) * 0.5f;
        target_floor = center - AUDIO_AUTO_MIN_SPAN_DB * 0.5f;
        target_ceiling = center + AUDIO_AUTO_MIN_SPAN_DB * 0.5f;
        if( target_floor < AUDIO_DB_MIN )
        {
            target_ceiling += AUDIO_DB_MIN - target_floor;
            target_floor = AUDIO_DB_MIN;
        }
        if( target_ceiling > AUDIO_DB_MAX )
        {
            target_floor -= target_ceiling - AUDIO_DB_MAX;
            target_ceiling = AUDIO_DB_MAX;
        }
    }

    portENTER_CRITICAL( &state_lock );
    if( warmup_frames > 0U )
    {
        --warmup_frames;
        memset( current_bins, 0, sizeof( current_bins ) );
        memset( peak_bins, 0, sizeof( peak_bins ) );
        frame_ready = false;
        portEXIT_CRITICAL( &state_lock );
        return;
    }

    if( !scale_ready )
    {
        scale_floor_db = target_floor;
        scale_ceiling_db = target_ceiling;
        scale_ready = true;
    }
    else
    {
        /* Fast attack when the signal gets louder, slow release so the graph
           does not visibly pump as room noise changes. */
        const float floor_alpha = ( target_floor < scale_floor_db ) ? 0.30f : 0.08f;
        const float ceiling_alpha = ( target_ceiling > scale_ceiling_db ) ? 0.35f : 0.08f;
        scale_floor_db += ( target_floor - scale_floor_db ) * floor_alpha;
        scale_ceiling_db += ( target_ceiling - scale_ceiling_db ) * ceiling_alpha;
    }

    for( unsigned i = 0U; i < AUDIO_SPECTRUM_BINS; ++i )
    {
        if( db_bins[ i ] >= peak_db[ i ] ) peak_db[ i ] = db_bins[ i ];
        else
        {
            peak_db[ i ] -= AUDIO_PEAK_DECAY_DB;
            if( peak_db[ i ] < AUDIO_DB_MIN ) peak_db[ i ] = AUDIO_DB_MIN;
        }
        current_bins[ i ] = scale_db( db_bins[ i ], scale_floor_db, scale_ceiling_db );
        peak_bins[ i ] = scale_db( peak_db[ i ], scale_floor_db, scale_ceiling_db );
    }

    if( frame_ready ) ++overruns;
    frame_ready = true;
    portEXIT_CRITICAL( &state_lock );
}

static void append_pcm_sample( int16_t *a_pcm,
                               unsigned *a_fill,
                               int16_t a_sample,
                               int64_t *a_last_fft_us )
{
    a_pcm[ *a_fill ] = a_sample;
    ++( *a_fill );
    if( *a_fill < AUDIO_FFT_SIZE ) return;

    const int64_t now_us = esp_timer_get_time();
    if( ( now_us - *a_last_fft_us ) >= AUDIO_FFT_INTERVAL_US )
    {
        publish_fft( a_pcm );
        *a_last_fft_us = now_us;
        taskYIELD();
    }

    memmove( a_pcm,
             &a_pcm[ AUDIO_HOP_SIZE ],
             AUDIO_HOP_SIZE * sizeof( int16_t ) );
    *a_fill = AUDIO_HOP_SIZE;
}

static void audio_worker( void *a_arg )
{
    (void)a_arg;
    int16_t pcm[ AUDIO_FFT_SIZE ];
    int16_t dma_samples[ AUDIO_DMA_READ_SAMPLES ];
    unsigned fill = 0U;
    int64_t last_fft_us = esp_timer_get_time() - AUDIO_FFT_INTERVAL_US;
    unsigned timeout_streak = 0U;

    while( running )
    {
        size_t bytes = 0U;
        const esp_err_t result = i2s_channel_read( rx_channel,
                                                   dma_samples,
                                                   sizeof( dma_samples ),
                                                   &bytes,
                                                   pdMS_TO_TICKS( 100 ) );

        if( ESP_ERR_TIMEOUT == result )
        {
            ++timeout_streak;
            if( 10U == timeout_streak ) ESP_LOGW( TAG, "PDM input timed out repeatedly" );
            continue;
        }
        if( ESP_OK != result )
        {
            if( running ) ESP_LOGW( TAG, "PDM read failed: %s", esp_err_to_name( result ) );
            continue;
        }

        timeout_streak = 0U;
        const unsigned samples_read = (unsigned)( bytes / sizeof( int16_t ) );
        const unsigned slot = ( AUDIO_CHANNEL_RIGHT == selected_channel ) ? 1U : 0U;
        for( unsigned i = slot; i < samples_read; i += 2U )
        {
            append_pcm_sample( pcm, &fill, dma_samples[ i ], &last_fft_us );
        }
    }

    task_handle = NULL;
    vTaskDelete( NULL );
}

static void release_channel( void )
{
    if( NULL == rx_channel ) return;
    (void)i2s_channel_disable( rx_channel );
    (void)i2s_del_channel( rx_channel );
    rx_channel = NULL;
}

esp_err_t audio_spectrum_start( void )
{
    if( running ) return ESP_OK;

    prepare_hann_window();

    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG( I2S_NUM_0, I2S_ROLE_MASTER );
    channel_config.dma_desc_num = AUDIO_DMA_BUFFER_COUNT;
    channel_config.dma_frame_num = AUDIO_DMA_BUFFER_LENGTH;

    esp_err_t result = i2s_new_channel( &channel_config, NULL, &rx_channel );
    if( ESP_OK != result )
    {
        rx_channel = NULL;
        return result;
    }

    i2s_pdm_rx_config_t pdm_config =
    {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG( AUDIO_SAMPLE_RATE ),
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO ),
        .gpio_cfg =
        {
            .clk = (gpio_num_t)RFSCOUT_BOARD_MIC_CLOCK_PIN,
            .din = (gpio_num_t)RFSCOUT_BOARD_MIC_DATA_PIN,
            .invert_flags = { .clk_inv = false }
        }
    };

#if CONFIG_IDF_TARGET_ESP32S3
    pdm_config.slot_cfg.slot_mode = I2S_SLOT_MODE_STEREO;
    pdm_config.slot_cfg.slot_mask = I2S_PDM_LINE_SLOT_ALL;
#endif

#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
    pdm_config.slot_cfg.hp_en = false;
#endif

    result = i2s_channel_init_pdm_rx_mode( rx_channel, &pdm_config );
    if( ESP_OK == result ) result = i2s_channel_enable( rx_channel );
    if( ESP_OK != result )
    {
        release_channel();
        return result;
    }

    portENTER_CRITICAL( &state_lock );
    reset_display_state();
    overruns = 0U;
    portEXIT_CRITICAL( &state_lock );

    running = true;
    if( pdPASS != xTaskCreate( audio_worker,
                               "audio_fft",
                               AUDIO_TASK_STACK,
                               NULL,
                               AUDIO_TASK_PRIORITY,
                               &task_handle ) )
    {
        running = false;
        release_channel();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI( TAG,
              "PDM microphone started: I2S0 clk=GPIO%d data=GPIO%d PCM=%u Hz channel=%s",
              RFSCOUT_BOARD_MIC_CLOCK_PIN,
              RFSCOUT_BOARD_MIC_DATA_PIN,
              AUDIO_SAMPLE_RATE,
              audio_spectrum_channel_name( selected_channel ) );
    return ESP_OK;
}

void audio_spectrum_stop( void )
{
    if( !running && ( NULL == rx_channel ) ) return;
    running = false;
    for( unsigned attempt = 0U;
         ( NULL != task_handle ) && ( attempt < 20U );
         ++attempt )
    {
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }
    release_channel();
}

bool audio_spectrum_is_running( void )
{
    return running;
}

bool audio_spectrum_take_frame( uint8_t *a_current,
                                uint8_t *a_peak,
                                size_t a_count )
{
    if( ( NULL == a_current ) || ( NULL == a_peak ) ||
        ( a_count < AUDIO_SPECTRUM_BINS ) ) return false;

    bool ready;
    portENTER_CRITICAL( &state_lock );
    ready = frame_ready;
    if( ready )
    {
        memcpy( a_current, current_bins, sizeof( current_bins ) );
        memcpy( a_peak, peak_bins, sizeof( peak_bins ) );
        frame_ready = false;
    }
    portEXIT_CRITICAL( &state_lock );
    return ready;
}

void audio_spectrum_set_range( audio_range_t a_range )
{
    if( a_range >= AUDIO_RANGE_COUNT ) return;
    portENTER_CRITICAL( &state_lock );
    selected_range = a_range;
    reset_display_state();
    portEXIT_CRITICAL( &state_lock );
}

audio_range_t audio_spectrum_range( void )
{
    return selected_range;
}

const char *audio_spectrum_range_name( audio_range_t a_range )
{
    static const char *names[] = { "INFRASONIC", "AUDIBLE", "ULTRASONIC" };
    return ( a_range < AUDIO_RANGE_COUNT ) ? names[ a_range ] : "UNKNOWN";
}

void audio_spectrum_set_channel( audio_channel_t a_channel )
{
    if( a_channel >= AUDIO_CHANNEL_COUNT ) return;
    portENTER_CRITICAL( &state_lock );
    selected_channel = a_channel;
    reset_display_state();
    portEXIT_CRITICAL( &state_lock );
}

audio_channel_t audio_spectrum_channel( void )
{
    return selected_channel;
}

const char *audio_spectrum_channel_name( audio_channel_t a_channel )
{
    static const char *names[] = { "LEFT", "RIGHT" };
    return ( a_channel < AUDIO_CHANNEL_COUNT ) ? names[ a_channel ] : "UNKNOWN";
}

uint32_t audio_spectrum_low_hz( void )
{
    uint32_t low;
    uint32_t high;
    range_bounds( selected_range, &low, &high );
    (void)high;
    return low;
}

uint32_t audio_spectrum_high_hz( void )
{
    uint32_t low;
    uint32_t high;
    range_bounds( selected_range, &low, &high );
    (void)low;
    return high;
}

int audio_spectrum_scale_floor_db( void )
{
    return (int)lroundf( scale_floor_db );
}

int audio_spectrum_scale_ceiling_db( void )
{
    return (int)lroundf( scale_ceiling_db );
}

uint32_t audio_spectrum_overruns( void )
{
    return overruns;
}
