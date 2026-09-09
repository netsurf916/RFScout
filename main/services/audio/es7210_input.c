/**
    es7210_input.c : RFScout T-Deck ES7210 microphone provider
    Description: Owns the onboard codec control and I2S0 capture transport behind audio_capture.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "es7210_input.h"

#include "audio_capture.h"
#include "board.h"
#include "board_i2c.h"
#include "input.h"

#include "driver/i2s_tdm.h"
#include "esp_codec_dev_defaults.h"
#include "es7210_adc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define ES7210_INPUT_GAIN_DB 6.0f
#define ES7210_INPUT_SAMPLE_RATE 16000U
#define ES7210_INPUT_MIC_SELECTION                                                                 \
    ( ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4 )
#define ES7210_INPUT_I2C_ADDRESS_7BIT ( ES7210_CODEC_DEFAULT_ADDR >> 1U )
#define ES7210_INPUT_TDM_SLOTS 4U
#define ES7210_INPUT_RAW_FRAMES 512U
#define ES7210_INPUT_OUTPUT_SLOT 0U

static const char *TAG = "es7210_input";
static i2s_chan_handle_t rx_channel;
static const audio_codec_ctrl_if_t *ctrl_if;
static const audio_codec_if_t *codec_if;
static bool codec_enabled;
static bool slot_diagnostics_logged;
static int16_t raw_frames[ ES7210_INPUT_RAW_FRAMES * ES7210_INPUT_TDM_SLOTS ];

static void release_codec( void )
{
    codec_enabled = false;
    if( NULL != codec_if )
    {
        /* The Espressif delete helper closes the codec, including ADC disable, before freeing it. */
        (void)audio_codec_delete_codec_if( codec_if );
        codec_if = NULL;
    }
    if( NULL != ctrl_if )
    {
        /* Deleting the control interface closes/removes its device from the shared I2C bus. */
        (void)audio_codec_delete_ctrl_if( ctrl_if );
        ctrl_if = NULL;
    }
}

esp_err_t es7210_input_probe( void )
{
    if( !RFSCOUT_BOARD_HAS_ES7210_MIC )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if( es7210_input_is_open() )
    {
        return ESP_ERR_INVALID_STATE;
    }
    return board_i2c_probe( ES7210_INPUT_I2C_ADDRESS_7BIT );
}

void es7210_input_close( void )
{
    release_codec();
    if( NULL != rx_channel )
    {
        (void)i2s_channel_disable( rx_channel );
        (void)i2s_del_channel( rx_channel );
        rx_channel = NULL;
    }
    input_primary_select_resume();
}

esp_err_t es7210_input_open( void )
{
    if( !RFSCOUT_BOARD_HAS_ES7210_MIC || es7210_input_is_open() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t probe_result = es7210_input_probe();
    if( ESP_OK != probe_result )
    {
        return probe_result;
    }

    i2c_master_bus_handle_t i2c_bus = board_i2c_handle();
    if( NULL == i2c_bus )
    {
        return ESP_FAIL;
    }

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG( I2S_NUM_0, I2S_ROLE_MASTER );
    esp_err_t result = i2s_new_channel( &channel_config, NULL, &rx_channel );
    if( ESP_OK != result )
    {
        rx_channel = NULL;
        return result;
    }

    /* LILYGO's T-Deck microphone path uses a fixed four-slot ES7210 TDM frame. Keep all physical
       slots in DMA and compact the populated MIC1/slot 0 into RFScout's mono software contract in
       es7210_input_read(). This preserves the codec's wire framing instead of asking IDF to present
       the device as ordinary two-slot I2S. */
    i2s_tdm_config_t i2s_config = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG( ES7210_INPUT_SAMPLE_RATE ),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG( I2S_DATA_BIT_WIDTH_16BIT,
                                                         I2S_SLOT_MODE_STEREO,
                                                         I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                                                             I2S_TDM_SLOT2 | I2S_TDM_SLOT3 ),
        .gpio_cfg =
            {
                .mclk = (gpio_num_t)RFSCOUT_BOARD_ES7210_MCLK_PIN,
                .bclk = (gpio_num_t)RFSCOUT_BOARD_ES7210_SCLK_PIN,
                .ws = (gpio_num_t)RFSCOUT_BOARD_ES7210_LRCLK_PIN,
                .dout = I2S_GPIO_UNUSED,
                .din = (gpio_num_t)RFSCOUT_BOARD_ES7210_DATA_PIN,
                .invert_flags = { 0 },
            },
    };
    i2s_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    i2s_config.slot_cfg.total_slot = ES7210_INPUT_TDM_SLOTS;
    i2s_config.slot_cfg.skip_mask = false;
    result = i2s_channel_init_tdm_mode( rx_channel, &i2s_config );
    if( ESP_OK != result )
    {
        es7210_input_close();
        return result;
    }

    audio_codec_i2c_cfg_t i2c_config = {
        .port = I2C_NUM_0,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    ctrl_if = audio_codec_new_i2c_ctrl( &i2c_config );
    if( NULL == ctrl_if )
    {
        es7210_input_close();
        return ESP_FAIL;
    }

    /* LILYGO documents GPIO0/trackball-center as unavailable while microphone capture is active.
       es7210_codec_new() immediately opens/configures the selected microphone path, so claim this
       conflict before codec creation rather than waiting for I2S/ADC enable. Every failure path
       below closes the provider and restores Select. */
    input_primary_select_suspend();

    es7210_codec_cfg_t codec_config = {
        .ctrl_if = ctrl_if,
        .master_mode = false,
        .mic_selected = ES7210_INPUT_MIC_SELECTION,
        .mclk_src = ES7210_MCLK_FROM_PAD,
        .mclk_div = I2S_MCLK_MULTIPLE_256,
    };
    /* Selecting all four ADC channels keeps the ES7210 in a stable four-slot TDM layout. RFScout
       currently publishes MIC1/slot 0 as mono; the one-time slot diagnostics make alternate board
       wiring immediately visible during hardware validation. */
    codec_if = es7210_codec_new( &codec_config );
    if( NULL == codec_if || NULL == codec_if->set_fs || NULL == codec_if->set_mic_gain ||
        NULL == codec_if->enable || NULL == codec_if->close )
    {
        es7210_input_close();
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_codec_dev_sample_info_t sample_config = {
        .bits_per_sample = RFSCOUT_AUDIO_CAPTURE_BITS_PER_SAMPLE,
        .channel = ES7210_INPUT_TDM_SLOTS,
        .channel_mask = 0,
        .sample_rate = ES7210_INPUT_SAMPLE_RATE,
    };
    if( ESP_CODEC_DEV_OK != codec_if->set_fs( codec_if, &sample_config ) ||
        ESP_CODEC_DEV_OK != codec_if->set_mic_gain( codec_if, ES7210_INPUT_GAIN_DB ) )
    {
        es7210_input_close();
        return ESP_FAIL;
    }

    result = i2s_channel_enable( rx_channel );
    if( ESP_OK != result )
    {
        es7210_input_close();
        return result;
    }
    if( ESP_CODEC_DEV_OK != codec_if->enable( codec_if, true ) )
    {
        es7210_input_close();
        return ESP_FAIL;
    }
    codec_enabled = true;
    slot_diagnostics_logged = false;

    ESP_LOGI( TAG,
              "ES7210 configured: I2S0 TDM4 MIC1/slot0 mclk=GPIO%d bclk=GPIO%d lrclk=GPIO%d "
              "data=GPIO%d rate=%u",
              RFSCOUT_BOARD_ES7210_MCLK_PIN,
              RFSCOUT_BOARD_ES7210_SCLK_PIN,
              RFSCOUT_BOARD_ES7210_LRCLK_PIN,
              RFSCOUT_BOARD_ES7210_DATA_PIN,
              (unsigned)ES7210_INPUT_SAMPLE_RATE );
    return ESP_OK;
}

esp_err_t
es7210_input_read( void *a_buffer, size_t a_capacity, size_t *a_bytes_read, uint32_t a_timeout_ms )
{
    if( NULL == a_buffer || NULL == a_bytes_read || 0U == a_capacity )
    {
        return ESP_ERR_INVALID_ARG;
    }
    *a_bytes_read = 0U;
    if( NULL == rx_channel || !codec_enabled )
    {
        return ESP_ERR_INVALID_STATE;
    }

    int16_t *output = (int16_t *)a_buffer;
    const size_t requested_samples = a_capacity / sizeof( int16_t );
    size_t output_samples = 0U;
    int32_t slot_peaks[ ES7210_INPUT_TDM_SLOTS ] = { 0 };

    while( output_samples < requested_samples )
    {
        size_t frames = requested_samples - output_samples;
        if( frames > ES7210_INPUT_RAW_FRAMES )
        {
            frames = ES7210_INPUT_RAW_FRAMES;
        }
        const size_t raw_capacity = frames * ES7210_INPUT_TDM_SLOTS * sizeof( raw_frames[ 0 ] );
        size_t raw_bytes = 0U;
        /* i2s_channel_read() takes milliseconds, matching the audio_capture service contract. */
        const esp_err_t result =
            i2s_channel_read( rx_channel, raw_frames, raw_capacity, &raw_bytes, a_timeout_ms );
        if( ESP_OK != result )
        {
            return result;
        }

        const size_t raw_samples = raw_bytes / sizeof( raw_frames[ 0 ] );
        const size_t complete_frames = raw_samples / ES7210_INPUT_TDM_SLOTS;
        if( 0U == complete_frames )
        {
            break;
        }

        for( size_t frame = 0U; frame < complete_frames; ++frame )
        {
            const size_t base = frame * ES7210_INPUT_TDM_SLOTS;
            output[ output_samples++ ] = raw_frames[ base + ES7210_INPUT_OUTPUT_SLOT ];
            if( !slot_diagnostics_logged )
            {
                for( size_t slot = 0U; slot < ES7210_INPUT_TDM_SLOTS; ++slot )
                {
                    int32_t magnitude = raw_frames[ base + slot ];
                    if( magnitude < 0 )
                    {
                        magnitude = -magnitude;
                    }
                    if( magnitude > slot_peaks[ slot ] )
                    {
                        slot_peaks[ slot ] = magnitude;
                    }
                }
            }
        }

        if( raw_bytes < raw_capacity )
        {
            break;
        }
    }

    *a_bytes_read = output_samples * sizeof( int16_t );
    if( !slot_diagnostics_logged && output_samples > 0U )
    {
        ESP_LOGI( TAG,
                  "TDM slot peaks: slot0=%ld slot1=%ld slot2=%ld slot3=%ld",
                  (long)slot_peaks[ 0 ],
                  (long)slot_peaks[ 1 ],
                  (long)slot_peaks[ 2 ],
                  (long)slot_peaks[ 3 ] );
        slot_diagnostics_logged = true;
    }
    return ESP_OK;
}

bool es7210_input_is_open( void )
{
    return NULL != rx_channel || NULL != codec_if || NULL != ctrl_if;
}
