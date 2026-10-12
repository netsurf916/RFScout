/**
    board.c : RFScout runtime board profile
    Description: Detects supported ESP32-S3 hardware and exposes immutable runtime wiring/capabilities.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "board.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "nvs.h"

#include <stddef.h>

#define BOARD_DETECT_TIMEOUT_MS 20
#define BOARD_HINT_NAMESPACE "hardware"
#define BOARD_HINT_KEY "display"
#define BOARD_HINT_VERSION 1U

static const char *TAG = "board";

static const rfscout_board_profile_t unknown_profile = {
    .id = RFSCOUT_BOARD_UNKNOWN,
    .target_id = "unknown",
    .name = "Unknown hardware",
    .capabilities = 0,
    .power_enable_pin = -1,
    .user_key_pin = -1,
    .spi_mosi_pin = -1,
    .spi_miso_pin = -1,
    .spi_sclk_pin = -1,
    .display_cs_pin = -1,
    .display_dc_pin = -1,
    .display_reset_pin = -1,
    .display_backlight_pin = -1,
    .display_width = 0,
    .display_height = 0,
    .display_native_width = 0,
    .display_native_height = 0,
    .display_x_gap = 0,
    .display_y_gap = 0,
    .display_swap_xy = false,
    .display_mirror_x = false,
    .display_mirror_y = false,
    .display_invert_color = false,
    .backlight_type = RFSCOUT_BACKLIGHT_PWM,
    .sd_cs_pin = -1,
    .cc1101_cs_pin = -1,
    .cc1101_gdo2_pin = -1,
    .cc1101_gdo0_pin = -1,
    .cc1101_switch_1_pin = -1,
    .cc1101_switch_0_pin = -1,
    .nrf24_cs_pin = -1,
    .nrf24_ce_pin = -1,
    .i2c_sda_pin = -1,
    .i2c_scl_pin = -1,
    .encoder_a_pin = -1,
    .encoder_b_pin = -1,
    .encoder_key_pin = -1,
    .speaker_lrclk_pin = -1,
    .speaker_bclk_pin = -1,
    .speaker_data_pin = -1,
    .battery_adc_pin = -1,
    .battery_adc_divider_numerator = 0U,
    .battery_adc_divider_denominator = 0U,
    .pn532_i2c_address = 0,
    .mlx90640_i2c_address = 0,
    .bq27220_i2c_address = 0x55,
    .bq25896_i2c_address = 0x6B,
    .pn532_reset_pin = -1,
    .pn532_irq_pin = -1,
    .ir_enable_pin = -1,
    .ir_rx_pin = -1,
    .mic_data_pin = -1,
    .mic_clock_pin = -1,
    .gps_tx_pin = -1,
    .gps_rx_pin = -1,
};

static const rfscout_board_profile_t tembed_profile = {
    .id = RFSCOUT_BOARD_TEMBED_CC1101_PLUS,
    .target_id = "t-embed-cc1101-plus",
    .name = "LILYGO T-Embed CC1101 Plus",
    .capabilities = RFSCOUT_BOARD_CAP_GPS | RFSCOUT_BOARD_CAP_ROTARY_ENCODER |
                    RFSCOUT_BOARD_CAP_PDM_MIC | RFSCOUT_BOARD_CAP_SPEAKER |
                    RFSCOUT_BOARD_CAP_INTERNAL_TEMPERATURE | RFSCOUT_BOARD_CAP_SCREEN_OFF |
                    RFSCOUT_BOARD_CAP_DEEP_SLEEP | RFSCOUT_BOARD_CAP_POWER_OFF,
    .power_enable_pin = 15,
    .user_key_pin = 6,
    .spi_mosi_pin = 9,
    .spi_miso_pin = 10,
    .spi_sclk_pin = 11,
    .display_cs_pin = 41,
    .display_dc_pin = 16,
    .display_reset_pin = -1,
    .display_backlight_pin = 21,
    .display_width = 320,
    .display_height = 170,
    .display_native_width = 170,
    .display_native_height = 320,
    .display_x_gap = 0,
    .display_y_gap = 35,
    .display_swap_xy = true,
    .display_mirror_x = false,
    .display_mirror_y = true,
    .display_invert_color = true,
    .backlight_type = RFSCOUT_BACKLIGHT_PWM,
    .sd_cs_pin = 13,
    .cc1101_cs_pin = 12,
    .cc1101_gdo2_pin = 38,
    .cc1101_gdo0_pin = 3,
    .cc1101_switch_1_pin = 47,
    .cc1101_switch_0_pin = 48,
    .nrf24_cs_pin = 44,
    .nrf24_ce_pin = 43,
    .i2c_sda_pin = 8,
    .i2c_scl_pin = 18,
    .encoder_a_pin = 4,
    .encoder_b_pin = 5,
    .encoder_key_pin = 0,
    .speaker_lrclk_pin = 40,
    .speaker_bclk_pin = 46,
    .speaker_data_pin = 7,
    .battery_adc_pin = -1,
    .battery_adc_divider_numerator = 0U,
    .battery_adc_divider_denominator = 0U,
    .pn532_i2c_address = 0x24,
    .mlx90640_i2c_address = 0x33,
    .bq27220_i2c_address = 0x55,
    .bq25896_i2c_address = 0x6B,
    .pn532_reset_pin = 45,
    .pn532_irq_pin = 17,
    .ir_enable_pin = 2,
    .ir_rx_pin = 1,
    .mic_data_pin = 42,
    .mic_clock_pin = 39,
    .gps_tx_pin = 43,
    .gps_rx_pin = 44,
};

static const rfscout_board_profile_t si4732_profile = {
    .id = RFSCOUT_BOARD_TEMBED_SI4732,
    .target_id = "t-embed-si4732",
    .name = "LILYGO T-Embed SI4732",
    .capabilities = RFSCOUT_BOARD_CAP_ROTARY_ENCODER | RFSCOUT_BOARD_CAP_SCREEN_OFF |
                    RFSCOUT_BOARD_CAP_INTERNAL_TEMPERATURE,
    .power_enable_pin = 46,
    .user_key_pin = -1,
    .spi_mosi_pin = 11,
    .spi_miso_pin = -1,
    .spi_sclk_pin = 12,
    .display_cs_pin = 10,
    .display_dc_pin = 13,
    .display_reset_pin = 9,
    .display_backlight_pin = 15,
    .display_width = 320,
    .display_height = 170,
    .display_native_width = 170,
    .display_native_height = 320,
    .display_x_gap = 0,
    .display_y_gap = 35,
    .display_swap_xy = true,
    .display_mirror_x = false,
    .display_mirror_y = true,
    .display_invert_color = true,
    .backlight_type = RFSCOUT_BACKLIGHT_PWM,
    .sd_cs_pin = -1,
    .cc1101_cs_pin = -1,
    .cc1101_gdo2_pin = -1,
    .cc1101_gdo0_pin = -1,
    .cc1101_switch_1_pin = -1,
    .cc1101_switch_0_pin = -1,
    .nrf24_cs_pin = -1,
    .nrf24_ce_pin = -1,
    .i2c_sda_pin = 18,
    .i2c_scl_pin = 8,
    .encoder_a_pin = 2,
    .encoder_b_pin = 1,
    .encoder_key_pin = 0,
    .speaker_lrclk_pin = -1,
    .speaker_bclk_pin = -1,
    .speaker_data_pin = -1,
    .battery_adc_pin = 4,
    .battery_adc_divider_numerator = 18U,
    .battery_adc_divider_denominator = 10U,
    .pn532_i2c_address = 0,
    .mlx90640_i2c_address = 0,
    .bq27220_i2c_address = 0,
    .bq25896_i2c_address = 0,
    .pn532_reset_pin = -1,
    .pn532_irq_pin = -1,
    .ir_enable_pin = -1,
    .ir_rx_pin = -1,
    .mic_data_pin = -1,
    .mic_clock_pin = -1,
    .gps_tx_pin = -1,
    .gps_rx_pin = -1,
};

static const rfscout_board_profile_t *active_profile = &unknown_profile;

static bool probe_fingerprint( int a_sda, int a_scl, const uint8_t *a_addresses, size_t a_count )
{
    i2c_master_bus_handle_t bus = NULL;
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = a_sda,
        .scl_io_num = a_scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if( ESP_OK != i2c_new_master_bus( &config, &bus ) )
    {
        return false;
    }

    bool found = false;
    for( size_t index = 0U; index < a_count && !found; ++index )
    {
        found = ESP_OK == i2c_master_probe( bus, a_addresses[ index ], BOARD_DETECT_TIMEOUT_MS );
    }
    (void)i2c_del_master_bus( bus );
    return found;
}

/** @brief Read the last discovered transport, never as proof of attached hardware. */
static uint8_t preferred_profile( void )
{
    nvs_handle_t handle;
    uint8_t candidate = 0U;
    if( ESP_OK == nvs_open( BOARD_HINT_NAMESPACE, NVS_READONLY, &handle ) )
    {
        uint32_t hint = 0U;
        if( ESP_OK == nvs_get_u32( handle, BOARD_HINT_KEY, &hint ) &&
            BOARD_HINT_VERSION == ( hint >> 16 ) )
        {
            candidate = (uint8_t)hint;
        }
        nvs_close( handle );
    }
    return candidate;
}

/** @brief Persist only a changed verified display candidate. */
static void remember_profile( uint8_t a_candidate )
{
    if( preferred_profile() == a_candidate )
    {
        return;
    }
    nvs_handle_t handle;
    if( ESP_OK == nvs_open( BOARD_HINT_NAMESPACE, NVS_READWRITE, &handle ) )
    {
        const uint32_t hint = ( BOARD_HINT_VERSION << 16 ) | a_candidate;
        if( ESP_OK == nvs_set_u32( handle, BOARD_HINT_KEY, hint ) )
        {
            (void)nvs_commit( handle );
        }
        nvs_close( handle );
    }
}

/** @brief Only passive known-safe I2C probes precede board GPIO configuration. */
static bool candidate_present( uint8_t a_candidate )
{
    static const uint8_t TEMBED_SIGNATURES[] = { 0x6B, 0x55 };
    static const uint8_t SI4732_ADDRESSES[] = { 0x6B, 0x55, 0x11, 0x63 };
    switch( a_candidate )
    {
        case RFSCOUT_BOARD_TEMBED_CC1101_PLUS:
            return probe_fingerprint( 8, 18, TEMBED_SIGNATURES, sizeof( TEMBED_SIGNATURES ) );
        case RFSCOUT_BOARD_TEMBED_SI4732:
            return probe_fingerprint( 18, 8, SI4732_ADDRESSES, sizeof( SI4732_ADDRESSES ) );
        default:
            return false;
    }
}

esp_err_t board_profile_detect( void )
{
    if( RFSCOUT_BOARD_UNKNOWN != active_profile->id )
    {
        return ESP_OK;
    }

    const uint8_t preferred = preferred_profile();
    const uint8_t order[] = {
        preferred, RFSCOUT_BOARD_TEMBED_CC1101_PLUS, RFSCOUT_BOARD_TEMBED_SI4732 };
    ESP_LOGI( TAG, "POST: begin passive discovery cached=%u", (unsigned)preferred );
    for( size_t index = 0U; index < sizeof( order ); ++index )
    {
        const uint8_t candidate = order[ index ];
        if( index > 0U && candidate == preferred )
        {
            continue;
        }
        if( candidate != RFSCOUT_BOARD_TEMBED_CC1101_PLUS &&
            candidate != RFSCOUT_BOARD_TEMBED_SI4732 )
        {
            continue;
        }
        const bool found = candidate_present( candidate );
        ESP_LOGI( TAG, "POST: candidate=%u responsive=%u", (unsigned)candidate, found ? 1U : 0U );
        if( !found )
        {
            continue;
        }
        active_profile =
            ( RFSCOUT_BOARD_TEMBED_SI4732 == candidate ) ? &si4732_profile : &tembed_profile;
        remember_profile( candidate );
        ESP_LOGI(
            TAG, "POST: selected transport=%s (verify providers on demand)", active_profile->name );
        return ESP_OK;
    }
    ESP_LOGW( TAG, "POST: no known transport verified; GPIO-safe diagnostic mode" );
    return ESP_OK;
}

const rfscout_board_profile_t *board_profile( void )
{
    return active_profile;
}

bool board_profile_selected( void )
{
    return RFSCOUT_BOARD_UNKNOWN != active_profile->id;
}

bool board_profile_has( rfscout_board_capability_t a_capability )
{
    return board_profile_selected() &&
           0U != ( active_profile->capabilities & (uint32_t)a_capability );
}
