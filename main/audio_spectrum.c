/**
 * audio_spectrum.c : DMA-fed PDM microphone FFT analyzer
 * Copyright 2026 Daniel Wilson
 * SPDX-License-Identifier: MIT
 */
#include "audio_spectrum.h"
#include "driver/i2s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#define AUDIO_SAMPLE_RATE 48000U
#define AUDIO_FFT_SIZE 1024U
#define AUDIO_HOP_SIZE ( AUDIO_FFT_SIZE / 2U )
#define AUDIO_MIC_CLK_GPIO 39
#define AUDIO_MIC_DATA_GPIO 42
#define AUDIO_DMA_BUFFER_COUNT 8
#define AUDIO_DMA_BUFFER_LENGTH 256
#define AUDIO_TASK_STACK 6144U
#define AUDIO_TASK_PRIORITY 5U
#define AUDIO_PEAK_DECAY 4U
#define AUDIO_EPSILON 1.0e-9f

static const char *TAG = "audio_spectrum";
static TaskHandle_t task_handle;
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
        while( j & bit ) { j ^= bit; bit >>= 1U; }
        j ^= bit;
        if( i < j )
        {
            float t = a_real[ i ]; a_real[ i ] = a_real[ j ]; a_real[ j ] = t;
            t = a_imag[ i ]; a_imag[ i ] = a_imag[ j ]; a_imag[ j ] = t;
        }
    }
    for( unsigned len = 2U; len <= AUDIO_FFT_SIZE; len <<= 1U )
    {
        const float angle = -2.0f * (float)M_PI / (float)len;
        const float wr0 = cosf( angle ), wi0 = sinf( angle );
        for( unsigned i = 0U; i < AUDIO_FFT_SIZE; i += len )
        {
            float wr = 1.0f, wi = 0.0f;
            for( unsigned k = 0U; k < len / 2U; ++k )
            {
                const unsigned even = i + k, odd = even + len / 2U;
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
    for( unsigned i = 0U; i < AUDIO_FFT_SIZE; ++i ) sum += a_pcm[ i ];
    const float mean = (float)sum / (float)AUDIO_FFT_SIZE;
    for( unsigned i = 0U; i < AUDIO_FFT_SIZE; ++i )
    {
        const float window = 0.5f - 0.5f * cosf( 2.0f * (float)M_PI * (float)i /
                                                (float)( AUDIO_FFT_SIZE - 1U ) );
        real[ i ] = ( (float)a_pcm[ i ] - mean ) * window;
        imag[ i ] = 0.0f;
    }
    fft( real, imag );

    uint32_t low, high;
    range_bounds( selected_range, &low, &high );
    for( unsigned x = 0U; x < AUDIO_SPECTRUM_BINS; ++x )
    {
        const float hz = (float)low + (float)( high - low ) * (float)x /
                         (float)( AUDIO_SPECTRUM_BINS - 1U );
        unsigned bin = (unsigned)lroundf( hz * (float)AUDIO_FFT_SIZE /
                                          (float)AUDIO_SAMPLE_RATE );
        if( bin >= AUDIO_FFT_SIZE / 2U ) bin = AUDIO_FFT_SIZE / 2U - 1U;
        const float mag = sqrtf( real[ bin ] * real[ bin ] + imag[ bin ] * imag[ bin ] );
        float db = 20.0f * log10f( mag / 32768.0f + AUDIO_EPSILON );
        if( db < -70.0f ) db = -70.0f;
        if( db > 10.0f ) db = 10.0f;
        next[ x ] = (uint8_t)( ( db + 70.0f ) * 255.0f / 80.0f );
    }

    portENTER_CRITICAL( &state_lock );
    memcpy( current_bins, next, sizeof( next ) );
    for( unsigned i = 0U; i < AUDIO_SPECTRUM_BINS; ++i )
    {
        if( next[ i ] >= peak_bins[ i ] ) peak_bins[ i ] = next[ i ];
        else peak_bins[ i ] = ( peak_bins[ i ] > AUDIO_PEAK_DECAY ) ?
                              (uint8_t)( peak_bins[ i ] - AUDIO_PEAK_DECAY ) : 0U;
    }
    if( frame_ready ) ++overruns;
    frame_ready = true;
    portEXIT_CRITICAL( &state_lock );
}

static void audio_worker( void *a_arg )
{
    (void)a_arg;
    int16_t pcm[ AUDIO_FFT_SIZE ];
    unsigned fill = 0U;
    while( running )
    {
        size_t bytes = 0U;
        esp_err_t result = i2s_read( I2S_NUM_0, &pcm[ fill ],
                                     ( AUDIO_FFT_SIZE - fill ) * sizeof( int16_t ),
                                     &bytes, pdMS_TO_TICKS( 100 ) );
        if( ESP_OK != result ) continue;
        fill += (unsigned)( bytes / sizeof( int16_t ) );
        if( fill >= AUDIO_FFT_SIZE )
        {
            publish_fft( pcm );
            memmove( pcm, &pcm[ AUDIO_HOP_SIZE ], AUDIO_HOP_SIZE * sizeof( int16_t ) );
            fill = AUDIO_HOP_SIZE;
        }
    }
    vTaskDelete( NULL );
}

esp_err_t audio_spectrum_start( void )
{
    if( running ) return ESP_OK;
    i2s_config_t config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM,
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2,
        .dma_buf_count = AUDIO_DMA_BUFFER_COUNT,
        .dma_buf_len = AUDIO_DMA_BUFFER_LENGTH,
        .use_apll = false
    };
    i2s_pin_config_t pins = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = I2S_PIN_NO_CHANGE,
        .ws_io_num = AUDIO_MIC_CLK_GPIO,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = AUDIO_MIC_DATA_GPIO
    };
    esp_err_t result = i2s_driver_install( I2S_NUM_0, &config, 0, NULL );
    if( ESP_OK != result ) return result;
    result = i2s_set_pin( I2S_NUM_0, &pins );
    if( ESP_OK == result ) result = i2s_set_clk( I2S_NUM_0, AUDIO_SAMPLE_RATE,
                                                 I2S_BITS_PER_SAMPLE_16BIT,
                                                 I2S_CHANNEL_MONO );
    if( ESP_OK != result ) { (void)i2s_driver_uninstall( I2S_NUM_0 ); return result; }
    portENTER_CRITICAL( &state_lock );
    memset( current_bins, 0, sizeof( current_bins ) );
    memset( peak_bins, 0, sizeof( peak_bins ) );
    frame_ready = false;
    overruns = 0U;
    portEXIT_CRITICAL( &state_lock );
    running = true;
    if( pdPASS != xTaskCreate( audio_worker, "audio_fft", AUDIO_TASK_STACK, NULL,
                               AUDIO_TASK_PRIORITY, &task_handle ) )
    {
        running = false;
        (void)i2s_driver_uninstall( I2S_NUM_0 );
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI( TAG, "PDM microphone started at %u Hz", AUDIO_SAMPLE_RATE );
    return ESP_OK;
}

void audio_spectrum_stop( void )
{
    if( !running ) return;
    running = false;
    vTaskDelay( pdMS_TO_TICKS( 120 ) );
    task_handle = NULL;
    (void)i2s_driver_uninstall( I2S_NUM_0 );
}

bool audio_spectrum_is_running( void ) { return running; }

bool audio_spectrum_take_frame( uint8_t *a_current, uint8_t *a_peak, size_t a_count )
{
    if( ( NULL == a_current ) || ( NULL == a_peak ) || ( a_count < AUDIO_SPECTRUM_BINS ) ) return false;
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
    memset( current_bins, 0, sizeof( current_bins ) );
    memset( peak_bins, 0, sizeof( peak_bins ) );
    frame_ready = false;
    portEXIT_CRITICAL( &state_lock );
}

audio_range_t audio_spectrum_range( void ) { return selected_range; }
const char *audio_spectrum_range_name( audio_range_t a_range )
{
    static const char *names[] = { "INFRASONIC", "AUDIBLE", "ULTRASONIC" };
    return ( a_range < AUDIO_RANGE_COUNT ) ? names[ a_range ] : "UNKNOWN";
}
uint32_t audio_spectrum_low_hz( void ) { uint32_t l, h; range_bounds( selected_range, &l, &h ); return l; }
uint32_t audio_spectrum_high_hz( void ) { uint32_t l, h; range_bounds( selected_range, &l, &h ); return h; }
uint32_t audio_spectrum_overruns( void ) { return overruns; }
