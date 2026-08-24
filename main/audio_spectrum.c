/**
 * audio_spectrum.c : DMA-fed PDM microphone FFT analyzer
 * Copyright 2026 Daniel Wilson
 * SPDX-License-Identifier: MIT
 */
#include "audio_spectrum.h"
#include "board.h"
#include "driver/gpio.h"
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
/* pcm[] + FFT real/imag arrays require >10 KiB at the deepest call point. */
#define AUDIO_TASK_STACK 14336U
#define AUDIO_TASK_PRIORITY 5U
#define AUDIO_PEAK_DECAY 4U
#define AUDIO_EPSILON 1.0e-9f
#define AUDIO_DIAGNOSTIC_INTERVAL_US 1000000LL

static const char *TAG = "audio_spectrum";
static TaskHandle_t task_handle;
static i2s_chan_handle_t rx_channel;
static volatile bool running;
static volatile bool frame_ready;
static volatile uint32_t overruns;
static audio_range_t selected_range = AUDIO_RANGE_AUDIBLE;
static uint8_t current_bins[ AUDIO_SPECTRUM_BINS ];
static uint8_t peak_bins[ AUDIO_SPECTRUM_BINS ];
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

static void publish_fft( const int16_t *a_pcm )
{
    float real[ AUDIO_FFT_SIZE ];
    float imag[ AUDIO_FFT_SIZE ];
    uint8_t next[ AUDIO_SPECTRUM_BINS ];
    int64_t sum = 0;

    for( unsigned i = 0U; i < AUDIO_FFT_SIZE; ++i )
    {
        sum += a_pcm[ i ];
    }
    const float mean = (float)sum / (float)AUDIO_FFT_SIZE;

    for( unsigned i = 0U; i < AUDIO_FFT_SIZE; ++i )
    {
        const float window = 0.5f - 0.5f * cosf( 2.0f * (float)M_PI * (float)i /
                                                (float)( AUDIO_FFT_SIZE - 1U ) );
        real[ i ] = ( (float)a_pcm[ i ] - mean ) * window;
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
        if( bin >= AUDIO_FFT_SIZE / 2U )
        {
            bin = AUDIO_FFT_SIZE / 2U - 1U;
        }
        const float magnitude = sqrtf( real[ bin ] * real[ bin ] +
                                       imag[ bin ] * imag[ bin ] );
        float db = 20.0f * log10f( magnitude / 32768.0f + AUDIO_EPSILON );
        if( db < -70.0f ) db = -70.0f;
        if( db > 10.0f ) db = 10.0f;
        next[ x ] = (uint8_t)( ( db + 70.0f ) * 255.0f / 80.0f );
    }

    portENTER_CRITICAL( &state_lock );
    memcpy( current_bins, next, sizeof( next ) );
    for( unsigned i = 0U; i < AUDIO_SPECTRUM_BINS; ++i )
    {
        if( next[ i ] >= peak_bins[ i ] )
        {
            peak_bins[ i ] = next[ i ];
        }
        else
        {
            peak_bins[ i ] = ( peak_bins[ i ] > AUDIO_PEAK_DECAY ) ?
                             (uint8_t)( peak_bins[ i ] - AUDIO_PEAK_DECAY ) : 0U;
        }
    }
    if( frame_ready )
    {
        ++overruns;
    }
    frame_ready = true;
    portEXIT_CRITICAL( &state_lock );
}

static void audio_worker( void *a_arg )
{
    (void)a_arg;
    int16_t pcm[ AUDIO_FFT_SIZE ];
    unsigned fill = 0U;
    int64_t last_diagnostic_us = esp_timer_get_time();
    int16_t diagnostic_peak = 0;
    uint64_t diagnostic_sum_sq = 0U;
    uint32_t diagnostic_samples = 0U;

    while( running )
    {
        size_t bytes = 0U;
        const esp_err_t result =
            i2s_channel_read( rx_channel,
                              &pcm[ fill ],
                              ( AUDIO_FFT_SIZE - fill ) * sizeof( int16_t ),
                              &bytes,
                              pdMS_TO_TICKS( 100 ) );
        if( ESP_ERR_TIMEOUT == result )
        {
            continue;
        }
        if( ESP_OK != result )
        {
            if( running )
            {
                ESP_LOGW( TAG, "PDM read failed: %s", esp_err_to_name( result ) );
            }
            continue;
        }

        const unsigned samples_read = (unsigned)( bytes / sizeof( int16_t ) );
        for( unsigned i = 0U; i < samples_read; ++i )
        {
            const int32_t sample = pcm[ fill + i ];
            const int32_t absolute = sample < 0 ? -sample : sample;
            if( absolute > diagnostic_peak ) diagnostic_peak = (int16_t)absolute;
            diagnostic_sum_sq += (uint64_t)( sample * sample );
        }
        diagnostic_samples += samples_read;

        fill += samples_read;
        if( fill >= AUDIO_FFT_SIZE )
        {
            publish_fft( pcm );
            memmove( pcm,
                     &pcm[ AUDIO_HOP_SIZE ],
                     AUDIO_HOP_SIZE * sizeof( int16_t ) );
            fill = AUDIO_HOP_SIZE;
        }

        const int64_t now_us = esp_timer_get_time();
        if( ( now_us - last_diagnostic_us ) >= AUDIO_DIAGNOSTIC_INTERVAL_US )
        {
            const uint32_t rms = diagnostic_samples > 0U ?
                (uint32_t)sqrt( (double)diagnostic_sum_sq / diagnostic_samples ) : 0U;
            ESP_LOGI( TAG,
                      "mic PCM samples=%lu rms=%lu peak=%d stack_free=%u",
                      (unsigned long)diagnostic_samples,
                      (unsigned long)rms,
                      (int)diagnostic_peak,
                      (unsigned)uxTaskGetStackHighWaterMark( NULL ) );
            diagnostic_peak = 0;
            diagnostic_sum_sq = 0U;
            diagnostic_samples = 0U;
            last_diagnostic_us = now_us;
        }
    }

    task_handle = NULL;
    vTaskDelete( NULL );
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

esp_err_t audio_spectrum_start( void )
{
    if( running )
    {
        return ESP_OK;
    }

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
            .invert_flags =
            {
                .clk_inv = false
            }
        }
    };

#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
    /* The default helper enables a ~35.5 Hz high-pass filter on newer S3 I2S
       hardware. RFScout needs the raw low-frequency content for Infrasonic
       mode, so explicitly disable that hardware filter. */
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
        return result;
    }

    portENTER_CRITICAL( &state_lock );
    memset( current_bins, 0, sizeof( current_bins ) );
    memset( peak_bins, 0, sizeof( peak_bins ) );
    frame_ready = false;
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
              "PDM microphone started: I2S0 clk=GPIO%d data=GPIO%d PCM=%u Hz DMA=%ux%u",
              RFSCOUT_BOARD_MIC_CLOCK_PIN,
              RFSCOUT_BOARD_MIC_DATA_PIN,
              AUDIO_SAMPLE_RATE,
              AUDIO_DMA_BUFFER_COUNT,
              AUDIO_DMA_BUFFER_LENGTH );
    return ESP_OK;
}

void audio_spectrum_stop( void )
{
    if( !running && ( NULL == rx_channel ) )
    {
        return;
    }

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
        ( a_count < AUDIO_SPECTRUM_BINS ) )
    {
        return false;
    }

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
    if( a_range >= AUDIO_RANGE_COUNT )
    {
        return;
    }

    portENTER_CRITICAL( &state_lock );
    selected_range = a_range;
    memset( current_bins, 0, sizeof( current_bins ) );
    memset( peak_bins, 0, sizeof( peak_bins ) );
    frame_ready = false;
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

uint32_t audio_spectrum_overruns( void )
{
    return overruns;
}
