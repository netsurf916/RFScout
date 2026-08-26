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

#define AUDIO_OUTPUT_SAMPLE_RATE 16000U
#define AUDIO_OUTPUT_AMPLITUDE 7000.0f
#define AUDIO_OUTPUT_CHUNK_FRAMES 160U
#define AUDIO_OUTPUT_QUEUE_LENGTH 6U
#define AUDIO_OUTPUT_TASK_STACK 4096U
#define AUDIO_OUTPUT_TASK_PRIORITY 3U
#define AUDIO_OUTPUT_DMA_DESC_NUM 6U
#define AUDIO_OUTPUT_DMA_FRAME_NUM 240U
#define AUDIO_OUTPUT_TRAILING_SILENCE_MS 20U

typedef enum
{
    AUDIO_OUTPUT_COMMAND_TONE = 0,
    AUDIO_OUTPUT_COMMAND_MORSE,
} audio_output_command_type_t;

typedef struct
{
    audio_output_command_type_t type;
    uint16_t frequency_hz;
    uint32_t duration_ms;
    char character;
} audio_output_command_t;

static const char *TAG = "audio_output";
static i2s_chan_handle_t tx_channel;
static QueueHandle_t command_queue;
static TaskHandle_t output_task;
static bool initialized;

/** @brief Write one tone or silence segment to the speaker channel. */
static bool
write_segment( bool a_tone, uint16_t a_frequency_hz, uint32_t a_duration_ms, float *a_phase )
{
    int16_t samples[ AUDIO_OUTPUT_CHUNK_FRAMES * 2U ];
    uint32_t remaining_frames =
        (uint32_t)( ( (uint64_t)AUDIO_OUTPUT_SAMPLE_RATE * a_duration_ms + 999U ) / 1000U );
    const float phase_step =
        2.0f * (float)M_PI * (float)a_frequency_hz / (float)AUDIO_OUTPUT_SAMPLE_RATE;

    while( remaining_frames > 0U )
    {
        const uint32_t frames = ( remaining_frames < AUDIO_OUTPUT_CHUNK_FRAMES )
                                    ? remaining_frames
                                    : AUDIO_OUTPUT_CHUNK_FRAMES;
        for( uint32_t frame = 0U; frame < frames; ++frame )
        {
            int16_t sample = 0;
            if( a_tone )
            {
                sample = (int16_t)lroundf( sinf( *a_phase ) * AUDIO_OUTPUT_AMPLITUDE );
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

/** @brief Play one queued tone command on the worker task. */
static void play_tone( const audio_output_command_t *a_command )
{
    float phase = 0.0f;
    if( ESP_OK != i2s_channel_enable( tx_channel ) )
    {
        ESP_LOGW( TAG, "could not enable speaker channel" );
        return;
    }

    (void)write_segment( true, a_command->frequency_hz, a_command->duration_ms, &phase );
    (void)write_segment( false, a_command->frequency_hz, AUDIO_OUTPUT_TRAILING_SILENCE_MS, &phase );
    (void)i2s_channel_disable( tx_channel );
}

/** @brief Play one queued single-character Morse command on the worker task. */
static void play_morse( const audio_output_command_t *a_command )
{
    const char *pattern = morse_pattern( a_command->character );
    if( NULL == pattern )
    {
        return;
    }

    float phase = 0.0f;
    if( ESP_OK != i2s_channel_enable( tx_channel ) )
    {
        ESP_LOGW( TAG, "could not enable speaker channel" );
        return;
    }

    for( const char *symbol = pattern; '\0' != *symbol; ++symbol )
    {
        const uint32_t tone_units = ( '-' == *symbol ) ? 3U : 1U;
        if( !write_segment(
                true, a_command->frequency_hz, tone_units * a_command->duration_ms, &phase ) )
        {
            break;
        }
        if( '\0' != symbol[ 1 ] )
        {
            if( !write_segment( false, a_command->frequency_hz, a_command->duration_ms, &phase ) )
            {
                break;
            }
        }
    }

    (void)write_segment( false, a_command->frequency_hz, 3U * a_command->duration_ms, &phase );
    (void)i2s_channel_disable( tx_channel );
}

/** @brief Drain queued speaker commands without blocking RF or UI callers. */
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

bool audio_output_play_tone( uint16_t a_frequency_hz, uint32_t a_duration_ms )
{
    if( !initialized || ( 0U == a_frequency_hz ) ||
        ( a_frequency_hz >= ( AUDIO_OUTPUT_SAMPLE_RATE / 2U ) ) || ( 0U == a_duration_ms ) )
    {
        return false;
    }

    const audio_output_command_t command = { .type = AUDIO_OUTPUT_COMMAND_TONE,
                                             .frequency_hz = a_frequency_hz,
                                             .duration_ms = a_duration_ms,
                                             .character = '\0' };
    return pdTRUE == xQueueSend( command_queue, &command, 0U );
}

bool audio_output_play_morse( char a_character, uint16_t a_frequency_hz, uint32_t a_unit_ms )
{
    if( !initialized || ( NULL == morse_pattern( a_character ) ) || ( 0U == a_frequency_hz ) ||
        ( a_frequency_hz >= ( AUDIO_OUTPUT_SAMPLE_RATE / 2U ) ) || ( 0U == a_unit_ms ) )
    {
        return false;
    }

    const audio_output_command_t command = { .type = AUDIO_OUTPUT_COMMAND_MORSE,
                                             .frequency_hz = a_frequency_hz,
                                             .duration_ms = a_unit_ms,
                                             .character = a_character };
    return pdTRUE == xQueueSend( command_queue, &command, 0U );
}
