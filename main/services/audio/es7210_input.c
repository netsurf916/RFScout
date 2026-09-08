/**
    es7210_input.c : RFScout T-Deck ES7210 microphone provider
    Description: Configures the onboard ES7210 through the shared I2C bus and I2S0.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "es7210_input.h"

#include "board.h"
#include "board_i2c.h"
#include "input.h"
#include "recorder.h"

#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es7210_adc.h"
#include "esp_log.h"

#define ES7210_INPUT_GAIN_DB 6.0f
#define ES7210_INPUT_MIC_SELECTION ( ES7210_SEL_MIC1 )
#define ES7210_INPUT_TDM_SLOTS 4U

static const char *TAG = "es7210_input";
static i2s_chan_handle_t rx_channel;
static const audio_codec_data_if_t *data_if;
static const audio_codec_ctrl_if_t *ctrl_if;
static const audio_codec_if_t *codec_if;
static esp_codec_dev_handle_t codec_dev;

static void release_codec( void )
{
    if( NULL != codec_dev )
    {
        (void)esp_codec_dev_close( codec_dev );
        esp_codec_dev_delete( codec_dev );
        codec_dev = NULL;
    }
    if( NULL != codec_if )
    {
        (void)audio_codec_delete_codec_if( codec_if );
        codec_if = NULL;
    }
    if( NULL != ctrl_if )
    {
        (void)audio_codec_delete_ctrl_if( ctrl_if );
        ctrl_if = NULL;
    }
    if( NULL != data_if )
    {
        (void)audio_codec_delete_data_if( data_if );
        data_if = NULL;
    }
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

esp_err_t es7210_input_open( i2s_chan_handle_t *a_channel )
{
    if( NULL == a_channel )
    {
        return ESP_ERR_INVALID_ARG;
    }
    *a_channel = NULL;
    if( !RFSCOUT_BOARD_HAS_ES7210_MIC || NULL != rx_channel || NULL != codec_dev )
    {
        return ESP_ERR_INVALID_STATE;
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

    /* ES7210 emits Philips-format TDM frames even when only one microphone is enabled. Keep the
       physical four-slot frame so BCLK/LRCK match the codec, select only slot 0 (MIC1), and ask
       I2S RX to compact masked slots out of DMA. Recorder therefore receives a contiguous mono
       int16_t stream rather than MIC1 samples interleaved with three unused physical slot words. */
    i2s_tdm_config_t i2s_config = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG( RFSCOUT_RECORDER_SAMPLE_RATE ),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_TDM_SLOT0 ),
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
    i2s_config.slot_cfg.skip_mask = true;
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

    audio_codec_i2s_cfg_t data_config = {
        .port = I2S_NUM_0,
        .rx_handle = rx_channel,
        .tx_handle = NULL,
    };
    data_if = audio_codec_new_i2s_data( &data_config );
    if( NULL == data_if )
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
    codec_if = es7210_codec_new( &codec_config );
    if( NULL == codec_if )
    {
        es7210_input_close();
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    codec_dev = esp_codec_dev_new( &device_config );
    if( NULL == codec_dev )
    {
        es7210_input_close();
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t sample_config = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = ES7210_INPUT_MIC_SELECTION,
        .sample_rate = RFSCOUT_RECORDER_SAMPLE_RATE,
    };

    /* LILYGO documents GPIO0/trackball-center as unavailable while microphone capture is active.
       Stop observing that GPIO before codec activation can begin driving the capture interface;
       every failure path below closes the provider and restores Select. */
    input_primary_select_suspend();
    if( ESP_CODEC_DEV_OK != esp_codec_dev_open( codec_dev, &sample_config ) ||
        ESP_CODEC_DEV_OK != esp_codec_dev_set_in_gain( codec_dev, ES7210_INPUT_GAIN_DB ) )
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

    *a_channel = rx_channel;
    ESP_LOGI( TAG,
              "ES7210 configured: I2S0 TDM4 MIC1 mclk=GPIO%d bclk=GPIO%d lrclk=GPIO%d data=GPIO%d "
              "rate=%u",
              RFSCOUT_BOARD_ES7210_MCLK_PIN,
              RFSCOUT_BOARD_ES7210_SCLK_PIN,
              RFSCOUT_BOARD_ES7210_LRCLK_PIN,
              RFSCOUT_BOARD_ES7210_DATA_PIN,
              (unsigned)RFSCOUT_RECORDER_SAMPLE_RATE );
    return ESP_OK;
}
