/**
    audio_output.c : RFScout asynchronous onboard-speaker output
    Description: Drives the dedicated I2S speaker pins from a nonblocking command queue.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "audio_output.h"

#include "board.h"
#include "morse.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define AUDIO_OUTPUT_SAMPLE_RATE 12800U
#define AUDIO_OUTPUT_MAX_AMPLITUDE 7000.0f
#define AUDIO_OUTPUT_CHUNK_FRAMES 160U
#define AUDIO_OUTPUT_QUEUE_LENGTH 6U
#define AUDIO_OUTPUT_TASK_STACK 4096U
#define AUDIO_OUTPUT_TASK_PRIORITY 3U
#define AUDIO_OUTPUT_DMA_DESC_NUM 6U
#define AUDIO_OUTPUT_DMA_FRAME_NUM 240U
#define AUDIO_OUTPUT_TRAILING_SILENCE_MS 20U
#define AUDIO_OUTPUT_MAX_VOLUME_PERCENT 100U
#define AUDIO_OUTPUT_WAV_PATH_MAX 160U
#define AUDIO_OUTPUT_WAV_HEADER_SIZE 44U

typedef enum
{
    AUDIO_OUTPUT_COMMAND_TONE = 0,
    AUDIO_OUTPUT_COMMAND_MORSE,
    AUDIO_OUTPUT_COMMAND_WAV,
} audio_output_command_type_t;

typedef struct
{
    audio_output_command_type_t type;
    uint16_t frequency_hz;
    uint32_t duration_ms;
    char character;
    uint8_t volume_percent;
    uint32_t generation;
    char path[ AUDIO_OUTPUT_WAV_PATH_MAX ];
} audio_output_command_t;

static const char *TAG = "audio_output";
static i2s_chan_handle_t tx_channel;
static QueueHandle_t command_queue;
static TaskHandle_t output_task;
static bool initialized;
static volatile bool playback_active;
static volatile bool playback_worker_active;
static volatile uint32_t playback_generation;
static volatile uint8_t playback_volume_percent = 50U;

static uint16_t read_u16_le( const uint8_t *a_source )
{
    return (uint16_t)a_source[ 0 ] | ( (uint16_t)a_source[ 1 ] << 8U );
}

static uint32_t read_u32_le( const uint8_t *a_source )
{
    return (uint32_t)a_source[ 0 ] | ( (uint32_t)a_source[ 1 ] << 8U ) |
           ( (uint32_t)a_source[ 2 ] << 16U ) | ( (uint32_t)a_source[ 3 ] << 24U );
}

/** @brief Write one tone or silence segment to the speaker channel. */
static bool write_segment( bool a_tone,
                           uint16_t a_frequency_hz,
                           uint32_t a_duration_ms,
                           uint8_t a_volume_percent,
                           float *a_phase )
{
    int16_t samples[ AUDIO_OUTPUT_CHUNK_FRAMES * 2U ];
    uint32_t remaining_frames =
        (uint32_t)( ( (uint64_t)AUDIO_OUTPUT_SAMPLE_RATE * a_duration_ms + 999U ) / 1000U );
    const float phase_step =
        2.0f * (float)M_PI * (float)a_frequency_hz / (float)AUDIO_OUTPUT_SAMPLE_RATE;
    const float amplitude =
        AUDIO_OUTPUT_MAX_AMPLITUDE * (float)a_volume_percent / AUDIO_OUTPUT_MAX_VOLUME_PERCENT;

    while( remaining_frames > 0U )
    {
        const uint32_t frames = ( remaining_frames < AUDIO_OUTPUT_CHUNK_FRAMES )
                                    ? remaining_frames
                                    : AUDIO_OUTPUT_CHUNK_FRAMES;
        for( uint32_t frame = 0U; frame < frames; ++frame )
        {
            int16_t sample = 0;
            if( a_tone && ( a_volume_percent > 0U ) )
            {
                sample = (int16_t)lroundf( sinf( *a_phase ) * amplitude );
                *a_phase += phase_step;
                if( *a_phase >= ( 2.0f * (float)M_PI ) )
                {
                    *a_phase -= 2.0f * (float)M_PI;
                }
            }

            samples[ frame * 2U ] = sample;
            samples[ frame * 2U + 1U ] = sample;
        }

        size_t bytes_written = 0U;
        const size_t bytes_requested = frames * 2U * sizeof( samples[ 0 ] );
        const esp_err_t result = i2s_channel_write(
            tx_channel, samples, bytes_requested, &bytes_written, portMAX_DELAY );
        if( ( ESP_OK != result ) || ( bytes_written != bytes_requested ) )
        {
            ESP_LOGW( TAG,
                      "speaker write failed: %s bytes=%u/%u",
                      esp_err_to_name( result ),
                      (unsigned)bytes_written,
                      (unsigned)bytes_requested );
            return false;
        }
        remaining_frames -= frames;
    }
    return true;
}

static uint32_t dma_drain_silence_ms( void )
{
    const uint32_t drain_frames = ( AUDIO_OUTPUT_DMA_DESC_NUM + 1U ) * AUDIO_OUTPUT_DMA_FRAME_NUM;
    return ( drain_frames * 1000U + AUDIO_OUTPUT_SAMPLE_RATE - 1U ) / AUDIO_OUTPUT_SAMPLE_RATE;
}

static bool enable_channel( void )
{
    if( ESP_OK != i2s_channel_enable( tx_channel ) )
    {
        ESP_LOGW( TAG, "could not enable speaker channel" );
        return false;
    }
    return true;
}

static void finish_channel( void )
{
    float phase = 0.0f;
    const uint32_t drain_ms = dma_drain_silence_ms();
    (void)write_segment( false, 1000U, drain_ms, 0U, &phase );
    (void)i2s_channel_disable( tx_channel );
}

static void play_tone( const audio_output_command_t *a_command )
{
    float phase = 0.0f;
    if( !enable_channel() )
    {
        return;
    }

    const uint32_t drain_ms = dma_drain_silence_ms();
    const uint32_t trailing_ms = ( drain_ms > AUDIO_OUTPUT_TRAILING_SILENCE_MS )
                                     ? drain_ms
                                     : AUDIO_OUTPUT_TRAILING_SILENCE_MS;
    (void)write_segment(
        true, a_command->frequency_hz, a_command->duration_ms, a_command->volume_percent, &phase );
    (void)write_segment(
        false, a_command->frequency_hz, trailing_ms, a_command->volume_percent, &phase );
    (void)i2s_channel_disable( tx_channel );
}

static void play_morse( const audio_output_command_t *a_command )
{
    const char *pattern = morse_pattern( a_command->character );
    if( NULL == pattern || !enable_channel() )
    {
        return;
    }

    float phase = 0.0f;
    for( const char *symbol = pattern; '\0' != *symbol; ++symbol )
    {
        const uint32_t tone_units = ( '-' == *symbol ) ? 3U : 1U;
        if( !write_segment( true,
                            a_command->frequency_hz,
                            tone_units * a_command->duration_ms,
                            a_command->volume_percent,
                            &phase ) )
        {
            break;
        }
        if( '\0' != symbol[ 1 ] && !write_segment( false,
                                                   a_command->frequency_hz,
                                                   a_command->duration_ms,
                                                   a_command->volume_percent,
                                                   &phase ) )
        {
            break;
        }
    }

    const uint32_t character_gap_ms = 3U * a_command->duration_ms;
    const uint32_t drain_ms = dma_drain_silence_ms();
    const uint32_t trailing_ms = ( character_gap_ms > drain_ms ) ? character_gap_ms : drain_ms;
    (void)write_segment(
        false, a_command->frequency_hz, trailing_ms, a_command->volume_percent, &phase );
    (void)i2s_channel_disable( tx_channel );
}

static bool wav_supported( const uint8_t a_header[ AUDIO_OUTPUT_WAV_HEADER_SIZE ] )
{
    return 0 == memcmp( &a_header[ 0 ], "RIFF", 4U ) && 0 == memcmp( &a_header[ 8 ], "WAVE", 4U ) &&
           0 == memcmp( &a_header[ 12 ], "fmt ", 4U ) &&
           0 == memcmp( &a_header[ 36 ], "data", 4U ) && 1U == read_u16_le( &a_header[ 20 ] ) &&
           1U == read_u16_le( &a_header[ 22 ] ) &&
           AUDIO_OUTPUT_SAMPLE_RATE == read_u32_le( &a_header[ 24 ] ) &&
           16U == read_u16_le( &a_header[ 34 ] );
}

static void finish_wav_command( uint32_t a_generation )
{
    playback_worker_active = false;
    if( a_generation == playback_generation )
    {
        playback_active = false;
    }
}

static void play_wav( const audio_output_command_t *a_command )
{
    if( a_command->generation != playback_generation )
    {
        return;
    }

    playback_worker_active = true;
    playback_volume_percent = a_command->volume_percent;
    FILE *file = fopen( a_command->path, "rb" );
    if( NULL == file )
    {
        ESP_LOGW( TAG, "could not open WAV %s", a_command->path );
        finish_wav_command( a_command->generation );
        return;
    }

    uint8_t header[ AUDIO_OUTPUT_WAV_HEADER_SIZE ];
    if( sizeof( header ) != fread( header, 1U, sizeof( header ), file ) ||
        !wav_supported( header ) )
    {
        ESP_LOGW( TAG, "unsupported WAV %s", a_command->path );
        fclose( file );
        finish_wav_command( a_command->generation );
        return;
    }

    if( !enable_channel() )
    {
        fclose( file );
        finish_wav_command( a_command->generation );
        return;
    }

    uint32_t remaining = read_u32_le( &header[ 40 ] );
    int16_t mono[ AUDIO_OUTPUT_CHUNK_FRAMES ];
    int16_t stereo[ AUDIO_OUTPUT_CHUNK_FRAMES * 2U ];

    while( remaining > 0U && a_command->generation == playback_generation )
    {
        size_t frames = remaining / sizeof( int16_t );
        if( frames > AUDIO_OUTPUT_CHUNK_FRAMES )
        {
            frames = AUDIO_OUTPUT_CHUNK_FRAMES;
        }

        const size_t read = fread( mono, sizeof( int16_t ), frames, file );
        if( 0U == read )
        {
            break;
        }

        for( size_t frame = 0U; frame < read; ++frame )
        {
            int32_t sample =
                (int32_t)mono[ frame ] * playback_volume_percent / AUDIO_OUTPUT_MAX_VOLUME_PERCENT;
            if( sample > INT16_MAX )
            {
                sample = INT16_MAX;
            }
            if( sample < INT16_MIN )
            {
                sample = INT16_MIN;
            }
            stereo[ frame * 2U ] = (int16_t)sample;
            stereo[ frame * 2U + 1U ] = (int16_t)sample;
        }

        const size_t requested = read * 2U * sizeof( int16_t );
        size_t written = 0U;
        if( ESP_OK != i2s_channel_write( tx_channel, stereo, requested, &written, portMAX_DELAY ) ||
            written != requested )
        {
            break;
        }
        remaining -= (uint32_t)( read * sizeof( int16_t ) );
    }

    fclose( file );
    finish_channel();
    finish_wav_command( a_command->generation );
}

static void output_worker( void *a_argument )
{
    (void)a_argument;
    audio_output_command_t command;

    while( true )
    {
        if( pdTRUE != xQueueReceive( command_queue, &command, portMAX_DELAY ) )
        {
            continue;
        }

        if( AUDIO_OUTPUT_COMMAND_MORSE == command.type )
        {
            play_morse( &command );
        }
        else if( AUDIO_OUTPUT_COMMAND_WAV == command.type )
        {
            play_wav( &command );
        }
        else
        {
            play_tone( &command );
        }
    }
}

esp_err_t audio_output_init( void )
{
    if( initialized )
    {
        return ESP_OK;
    }

    command_queue = xQueueCreate( AUDIO_OUTPUT_QUEUE_LENGTH, sizeof( audio_output_command_t ) );
    if( NULL == command_queue )
    {
        return ESP_ERR_NO_MEM;
    }

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG( I2S_NUM_1, I2S_ROLE_MASTER );
    channel_config.dma_desc_num = AUDIO_OUTPUT_DMA_DESC_NUM;
    channel_config.dma_frame_num = AUDIO_OUTPUT_DMA_FRAME_NUM;

    esp_err_t result = i2s_new_channel( &channel_config, &tx_channel, NULL );
    if( ESP_OK != result )
    {
        tx_channel = NULL;
        vQueueDelete( command_queue );
        command_queue = NULL;
        return result;
    }

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG( AUDIO_OUTPUT_SAMPLE_RATE ),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG( I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO ),
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED,
                      .bclk = (gpio_num_t)RFSCOUT_BOARD_SPEAKER_BCLK_PIN,
                      .ws = (gpio_num_t)RFSCOUT_BOARD_SPEAKER_LRCLK_PIN,
                      .dout = (gpio_num_t)RFSCOUT_BOARD_SPEAKER_DATA_PIN,
                      .din = I2S_GPIO_UNUSED,
                      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } } };

    result = i2s_channel_init_std_mode( tx_channel, &standard_config );
    if( ESP_OK != result )
    {
        (void)i2s_del_channel( tx_channel );
        tx_channel = NULL;
        vQueueDelete( command_queue );
        command_queue = NULL;
        return result;
    }

    if( pdPASS != xTaskCreate( output_worker,
                               "audio_output",
                               AUDIO_OUTPUT_TASK_STACK,
                               NULL,
                               AUDIO_OUTPUT_TASK_PRIORITY,
                               &output_task ) )
    {
        (void)i2s_del_channel( tx_channel );
        tx_channel = NULL;
        vQueueDelete( command_queue );
        command_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    initialized = true;
    ESP_LOGI( TAG,
              "speaker ready: I2S1 bclk=GPIO%d lrclk=GPIO%d data=GPIO%d",
              RFSCOUT_BOARD_SPEAKER_BCLK_PIN,
              RFSCOUT_BOARD_SPEAKER_LRCLK_PIN,
              RFSCOUT_BOARD_SPEAKER_DATA_PIN );
    return ESP_OK;
}

bool audio_output_play_tone( uint16_t a_frequency_hz,
                             uint32_t a_duration_ms,
                             uint8_t a_volume_percent )
{
    if( !initialized || ( 0U == a_frequency_hz ) ||
        ( a_frequency_hz >= ( AUDIO_OUTPUT_SAMPLE_RATE / 2U ) ) || ( 0U == a_duration_ms ) ||
        ( a_volume_percent > AUDIO_OUTPUT_MAX_VOLUME_PERCENT ) )
    {
        return false;
    }

    audio_output_command_t command = { .type = AUDIO_OUTPUT_COMMAND_TONE,
                                       .frequency_hz = a_frequency_hz,
                                       .duration_ms = a_duration_ms,
                                       .character = '\0',
                                       .volume_percent = a_volume_percent };
    return pdTRUE == xQueueSend( command_queue, &command, 0U );
}

bool audio_output_play_morse( char a_character,
                              uint16_t a_frequency_hz,
                              uint32_t a_unit_ms,
                              uint8_t a_volume_percent )
{
    if( !initialized || ( NULL == morse_pattern( a_character ) ) || ( 0U == a_frequency_hz ) ||
        ( a_frequency_hz >= ( AUDIO_OUTPUT_SAMPLE_RATE / 2U ) ) || ( 0U == a_unit_ms ) ||
        ( a_volume_percent > AUDIO_OUTPUT_MAX_VOLUME_PERCENT ) )
    {
        return false;
    }

    audio_output_command_t command = { .type = AUDIO_OUTPUT_COMMAND_MORSE,
                                       .frequency_hz = a_frequency_hz,
                                       .duration_ms = a_unit_ms,
                                       .character = a_character,
                                       .volume_percent = a_volume_percent };
    return pdTRUE == xQueueSend( command_queue, &command, 0U );
}

bool audio_output_play_wav( const char *a_path, uint8_t a_volume_percent )
{
    if( !initialized || NULL == a_path || '\0' == a_path[ 0 ] ||
        strlen( a_path ) >= AUDIO_OUTPUT_WAV_PATH_MAX ||
        a_volume_percent > AUDIO_OUTPUT_MAX_VOLUME_PERCENT )
    {
        return false;
    }

    const uint32_t generation = ++playback_generation;
    audio_output_command_t command = { .type = AUDIO_OUTPUT_COMMAND_WAV,
                                       .volume_percent = a_volume_percent,
                                       .generation = generation };
    snprintf( command.path, sizeof( command.path ), "%s", a_path );
    playback_active = true;
    if( pdTRUE != xQueueSend( command_queue, &command, 0U ) )
    {
        if( generation == playback_generation )
        {
            playback_active = false;
        }
        return false;
    }
    return true;
}

void audio_output_set_playback_volume( uint8_t a_volume_percent )
{
    if( a_volume_percent > AUDIO_OUTPUT_MAX_VOLUME_PERCENT )
    {
        a_volume_percent = AUDIO_OUTPUT_MAX_VOLUME_PERCENT;
    }
    playback_volume_percent = a_volume_percent;
}

bool audio_output_playback_active( void )
{
    return playback_active || playback_worker_active;
}

void audio_output_stop_playback( void )
{
    ++playback_generation;
    playback_active = false;
}

void audio_output_cancel_pending( void )
{
    if( initialized && ( NULL != command_queue ) )
    {
        (void)xQueueReset( command_queue );
    }
}
