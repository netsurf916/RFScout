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

#include "driver/i2s_std.h"
#include "esp_codec_dev_defaults.h"
#include "es7210_adc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define ES7210_INPUT_GAIN_DB 6.0f
#define ES7210_INPUT_MIC_SELECTION ( ES7210_SEL_MIC1 )
#define ES7210_INPUT_I2C_ADDRESS_7BIT ( ES7210_CODEC_DEFAULT_ADDR >> 1U )

static const char *TAG = "es7210_input";
static i2s_chan_handle_t rx_channel;
static const audio_codec_ctrl_if_t *ctrl_if;
static const audio_codec_if_t *codec_if;
static bool codec_enabled;

static void release_codec( void )
{
    if( NULL != codec_if )
    {
        if( NULL != codec_if->close )
        {
            /* The ES7210 close operation disables an enabled codec before marking it closed. */
            (void)codec_if->close( codec_if );
        }
        codec_enabled = false;
        (void)audio_codec_delete_codec_if( codec_if );
        codec_if = NULL;
    }
    if( NULL != ctrl_if )
    {
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

    /* The T-Deck populates one ES7210 microphone. Keep the codec in its ordinary Philips-I2S
       slave mode and let the ESP32-S3 own the wire clocks/DMA. MIC1 is normalized to a mono LEFT
       software stream here so callers never need to know the physical stereo frame layout. */
    i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG( RFSCOUT_AUDIO_CAPTURE_SAMPLE_RATE ),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG( I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO ),
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
    i2s_config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    result = i2s_channel_init_std_mode( rx_channel, &i2s_config );
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

    es7210_codec_cfg_t codec_config = {
        .ctrl_if = ctrl_if,
        .master_mode = false,
        .mic_selected = ES7210_INPUT_MIC_SELECTION,
        .mclk_src = ES7210_MCLK_FROM_PAD,
        .mclk_div = I2S_MCLK_MULTIPLE_256,
    };
    /* es7210_codec_new() opens the codec before returning; RFScout owns only subsequent format,
       gain, enable, and close transitions. */
    codec_if = es7210_codec_new( &codec_config );
    if( NULL == codec_if || NULL == codec_if->set_fs || NULL == codec_if->set_mic_gain ||
        NULL == codec_if->enable || NULL == codec_if->close )
    {
        es7210_input_close();
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* ES7210 register formatting remains stereo-I2S even though RFScout exposes one selected slot
       to software. The provider, not esp_codec_dev's I2S data wrapper, owns the transport format. */
    esp_codec_dev_sample_info_t sample_config = {
        .bits_per_sample = RFSCOUT_AUDIO_CAPTURE_BITS_PER_SAMPLE,
        .channel = 2,
        .channel_mask = 0,
        .sample_rate = RFSCOUT_AUDIO_CAPTURE_SAMPLE_RATE,
    };
    if( ESP_CODEC_DEV_OK != codec_if->set_fs( codec_if, &sample_config ) ||
        ESP_CODEC_DEV_OK != codec_if->set_mic_gain( codec_if, ES7210_INPUT_GAIN_DB ) )
    {
        es7210_input_close();
        return ESP_FAIL;
    }

    /* LILYGO documents GPIO0/trackball-center as unavailable while microphone capture is active.
       Suspend that physical source before clocks/ADC are started; all cleanup paths restore it. */
    input_primary_select_suspend();
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

    ESP_LOGI( TAG,
              "ES7210 configured: I2S0 Philips MIC1/LEFT mclk=GPIO%d bclk=GPIO%d lrclk=GPIO%d "
              "data=GPIO%d rate=%u",
              RFSCOUT_BOARD_ES7210_MCLK_PIN,
              RFSCOUT_BOARD_ES7210_SCLK_PIN,
              RFSCOUT_BOARD_ES7210_LRCLK_PIN,
              RFSCOUT_BOARD_ES7210_DATA_PIN,
              (unsigned)RFSCOUT_AUDIO_CAPTURE_SAMPLE_RATE );
    return ESP_OK;
}

esp_err_t
es7210_input_read( void *a_buffer, size_t a_capacity, size_t *a_bytes_read, uint32_t a_timeout_ms )
{
    if( NULL == a_buffer || NULL == a_bytes_read || 0U == a_capacity )
    {
        return ESP_ERR_INVALID_ARG;
    }
    if( NULL == rx_channel || !codec_enabled )
    {
        *a_bytes_read = 0U;
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_read(
        rx_channel, a_buffer, a_capacity, a_bytes_read, pdMS_TO_TICKS( a_timeout_ms ) );
}

bool es7210_input_is_open( void )
{
    return NULL != rx_channel || NULL != codec_if || NULL != ctrl_if;
}
