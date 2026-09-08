/**
    audio_capture.c : RFScout board-neutral audio capture service
    Description: Selects the legal runtime provider and owns capture transport lifecycle and reads.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "audio_capture.h"

#include "board.h"
#include "es7210_input.h"

#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AUDIO_CAPTURE_DMA_BUFFER_COUNT 8U
#define AUDIO_CAPTURE_DMA_BUFFER_LENGTH 256U
#define AUDIO_CAPTURE_MIC_WAKEUP_MS 20U
#define AUDIO_CAPTURE_CLOCK_MEASURE_MS 10U
#define AUDIO_CAPTURE_PDM_PROBE_MS 5U
#define AUDIO_CAPTURE_PDM_PROBE_MIN_EDGES 32U
#define AUDIO_CAPTURE_TARGET_PDM_CLOCK_HZ 2048000U
#define AUDIO_CAPTURE_PDM_CLOCK_MIN_HZ 1000000U
#define AUDIO_CAPTURE_PDM_CLOCK_MAX_HZ 3250000U
#define AUDIO_CAPTURE_CLOCK_DERIVATION_MAX_REQUEST_HZ 96000U

static const char *TAG = "audio_capture";
static i2s_chan_handle_t pdm_channel;
static rfscout_audio_capture_provider_t active_provider;
static uint32_t measured_pdm_clock_hz;

static uint32_t measure_pdm_clock_hz( void )
{
    pcnt_unit_handle_t unit = NULL;
    pcnt_channel_handle_t channel = NULL;
    const pcnt_unit_config_t unit_config = { .low_limit = -1, .high_limit = 30000 };
    esp_err_t result = pcnt_new_unit( &unit_config, &unit );
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "PDM clock measurement unavailable: %s", esp_err_to_name( result ) );
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
        vTaskDelay( pdMS_TO_TICKS( AUDIO_CAPTURE_CLOCK_MEASURE_MS ) );
        (void)pcnt_unit_stop( unit );
        result = pcnt_unit_get_count( unit, &count );
    }

    uint32_t measured_hz = 0U;
    if( ESP_OK == result )
    {
        measured_hz =
            (uint32_t)( (uint64_t)( count > 0 ? count : 0 ) * 1000ULL /
                        AUDIO_CAPTURE_CLOCK_MEASURE_MS );
        measured_pdm_clock_hz = measured_hz;
        ESP_LOGI( TAG,
                  "PDM clock measured: gpio=%d edges=%d window=%ums frequency=%u Hz",
                  RFSCOUT_BOARD_MIC_CLOCK_PIN,
                  count,
                  AUDIO_CAPTURE_CLOCK_MEASURE_MS,
                  (unsigned)measured_hz );
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
    return measured_hz;
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
        const TickType_t requested_ticks = pdMS_TO_TICKS( AUDIO_CAPTURE_PDM_PROBE_MS );
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

static void close_pdm( void )
{
    if( NULL != pdm_channel )
    {
        (void)i2s_channel_disable( pdm_channel );
        (void)i2s_del_channel( pdm_channel );
        pdm_channel = NULL;
    }
}

static esp_err_t open_pdm( void )
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG( I2S_NUM_0, I2S_ROLE_MASTER );
    channel_config.dma_desc_num = AUDIO_CAPTURE_DMA_BUFFER_COUNT;
    channel_config.dma_frame_num = AUDIO_CAPTURE_DMA_BUFFER_LENGTH;

    esp_err_t result = i2s_new_channel( &channel_config, NULL, &pdm_channel );
    if( ESP_OK != result )
    {
        pdm_channel = NULL;
        return result;
    }

    i2s_pdm_rx_config_t pdm_config = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG( RFSCOUT_AUDIO_CAPTURE_SAMPLE_RATE ),
        .slot_cfg =
            I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG( I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO ),
        .gpio_cfg = { .clk = (gpio_num_t)RFSCOUT_BOARD_MIC_CLOCK_PIN,
                      .din = (gpio_num_t)RFSCOUT_BOARD_MIC_DATA_PIN,
                      .invert_flags = { .clk_inv = false } },
    };

#if CONFIG_IDF_TARGET_ESP32S3
    pdm_config.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    pdm_config.slot_cfg.slot_mask = I2S_PDM_SLOT_LEFT;
#endif
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
    pdm_config.slot_cfg.hp_en = true;
#endif

    result = i2s_channel_init_pdm_rx_mode( pdm_channel, &pdm_config );
    if( ESP_OK == result )
    {
        result = i2s_channel_enable( pdm_channel );
    }
    if( ESP_OK != result )
    {
        close_pdm();
        return result;
    }

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
                        AUDIO_CAPTURE_TARGET_PDM_CLOCK_HZ / measured_pdm_clock_hz );
        if( derived_request_hz > AUDIO_CAPTURE_CLOCK_DERIVATION_MAX_REQUEST_HZ )
        {
            derived_request_hz = AUDIO_CAPTURE_CLOCK_DERIVATION_MAX_REQUEST_HZ;
        }
        ESP_LOGI( TAG,
                  "PDM configured: rate=%u clk=%u ratio=%u.%u%% derived_request=%u",
                  (unsigned)pdm_config.clk_cfg.sample_rate_hz,
                  (unsigned)measured_pdm_clock_hz,
                  (unsigned)( clock_ratio_permille / 10U ),
                  (unsigned)( clock_ratio_permille % 10U ),
                  (unsigned)derived_request_hz );
        if( measured_pdm_clock_hz < AUDIO_CAPTURE_PDM_CLOCK_MIN_HZ ||
            measured_pdm_clock_hz > AUDIO_CAPTURE_PDM_CLOCK_MAX_HZ )
        {
            ESP_LOGW( TAG,
                      "PDM clock outside microphone range: measured=%u valid=%u..%u Hz",
                      (unsigned)measured_pdm_clock_hz,
                      AUDIO_CAPTURE_PDM_CLOCK_MIN_HZ,
                      AUDIO_CAPTURE_PDM_CLOCK_MAX_HZ );
        }
    }
    return ESP_OK;
}

bool audio_capture_supported( void )
{
    return RFSCOUT_BOARD_HAS_PDM_MIC || RFSCOUT_BOARD_HAS_ES7210_MIC;
}

esp_err_t audio_capture_probe( void )
{
    if( audio_capture_is_open() )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( RFSCOUT_BOARD_HAS_ES7210_MIC )
    {
        return es7210_input_probe();
    }
    if( !RFSCOUT_BOARD_HAS_PDM_MIC )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t result = open_pdm();
    if( ESP_OK != result )
    {
        return result;
    }
    (void)gpio_set_pull_mode( RFSCOUT_BOARD_MIC_DATA_PIN, GPIO_PULLDOWN_ONLY );
    vTaskDelay( pdMS_TO_TICKS( AUDIO_CAPTURE_MIC_WAKEUP_MS ) );
    const uint32_t edges = measure_pdm_data_edges();
    audio_capture_close();
    (void)gpio_set_pull_mode( RFSCOUT_BOARD_MIC_DATA_PIN, GPIO_FLOATING );
    if( edges < AUDIO_CAPTURE_PDM_PROBE_MIN_EDGES )
    {
        ESP_LOGI( TAG,
                  "PDM probe: data_edges=%u/%ums; not detected",
                  (unsigned)edges,
                  AUDIO_CAPTURE_PDM_PROBE_MS );
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI( TAG,
              "PDM probe: data_edges=%u/%ums; detected",
              (unsigned)edges,
              AUDIO_CAPTURE_PDM_PROBE_MS );
    return ESP_OK;
}

esp_err_t audio_capture_open( rfscout_audio_capture_format_t *a_format )
{
    if( audio_capture_is_open() )
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result;
    if( RFSCOUT_BOARD_HAS_PDM_MIC )
    {
        result = open_pdm();
        if( ESP_OK == result )
        {
            active_provider = RFSCOUT_AUDIO_CAPTURE_PROVIDER_PDM;
        }
    }
    else if( RFSCOUT_BOARD_HAS_ES7210_MIC )
    {
        result = es7210_input_open();
        if( ESP_OK == result )
        {
            active_provider = RFSCOUT_AUDIO_CAPTURE_PROVIDER_ES7210;
        }
    }
    else
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if( ESP_OK != result )
    {
        active_provider = RFSCOUT_AUDIO_CAPTURE_PROVIDER_NONE;
        return result;
    }
    if( NULL != a_format )
    {
        a_format->sample_rate_hz = RFSCOUT_AUDIO_CAPTURE_SAMPLE_RATE;
        a_format->channels = RFSCOUT_AUDIO_CAPTURE_CHANNELS;
        a_format->bits_per_sample = RFSCOUT_AUDIO_CAPTURE_BITS_PER_SAMPLE;
    }
    return ESP_OK;
}

esp_err_t audio_capture_read( void *a_buffer,
                              size_t a_capacity,
                              size_t *a_bytes_read,
                              uint32_t a_timeout_ms )
{
    if( NULL == a_buffer || NULL == a_bytes_read || 0U == a_capacity )
    {
        return ESP_ERR_INVALID_ARG;
    }
    *a_bytes_read = 0U;
    if( RFSCOUT_AUDIO_CAPTURE_PROVIDER_PDM == active_provider )
    {
        return i2s_channel_read(
            pdm_channel, a_buffer, a_capacity, a_bytes_read, pdMS_TO_TICKS( a_timeout_ms ) );
    }
    if( RFSCOUT_AUDIO_CAPTURE_PROVIDER_ES7210 == active_provider )
    {
        return es7210_input_read( a_buffer, a_capacity, a_bytes_read, a_timeout_ms );
    }
    return ESP_ERR_INVALID_STATE;
}

void audio_capture_close( void )
{
    if( RFSCOUT_AUDIO_CAPTURE_PROVIDER_ES7210 == active_provider )
    {
        es7210_input_close();
    }
    else if( RFSCOUT_AUDIO_CAPTURE_PROVIDER_PDM == active_provider )
    {
        close_pdm();
    }
    active_provider = RFSCOUT_AUDIO_CAPTURE_PROVIDER_NONE;
}

bool audio_capture_is_open( void )
{
    return RFSCOUT_AUDIO_CAPTURE_PROVIDER_NONE != active_provider || NULL != pdm_channel ||
           es7210_input_is_open();
}

rfscout_audio_capture_provider_t audio_capture_provider( void )
{
    return active_provider;
}

const char *audio_capture_input_name( void )
{
    if( RFSCOUT_AUDIO_CAPTURE_PROVIDER_ES7210 == active_provider )
    {
        return "MIC1";
    }
    if( RFSCOUT_AUDIO_CAPTURE_PROVIDER_PDM == active_provider )
    {
        return "LEFT";
    }
    return "AUTO";
}

uint32_t audio_capture_pdm_clock_hz( void )
{
    return RFSCOUT_AUDIO_CAPTURE_PROVIDER_PDM == active_provider ? measured_pdm_clock_hz : 0U;
}
