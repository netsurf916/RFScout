/**
    board.c : RFScout runtime board profile selection
    Description: Selects one immutable hardware profile using an early passive I2C fingerprint.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "board.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

#define BOARD_DETECT_TIMEOUT_MS 20

static const char *TAG = "board";

static const rfscout_board_profile_t unknown_profile = {
    .id = RFSCOUT_BOARD_UNKNOWN,
    .name = "Unknown ESP32-S3 hardware",
    .power_enable_pin = -1,
    .user_key_pin = -1,
    .spi_mosi_pin = -1,
    .spi_miso_pin = -1,
    .spi_sclk_pin = -1,
    .display_cs_pin = -1,
    .display_dc_pin = -1,
    .display_reset_pin = -1,
    .display_backlight_pin = -1,
    .backlight_type = RFSCOUT_BACKLIGHT_PWM,
    .sd_cs_pin = -1,
    .cc1101_cs_pin = -1,
    .cc1101_gdo2_pin = -1,
    .cc1101_gdo0_pin = -1,
    .cc1101_switch_1_pin = -1,
    .cc1101_switch_0_pin = -1,
    .nrf24_cs_pin = -1,
    .nrf24_ce_pin = -1,
    .sx1262_cs_pin = -1,
    .sx1262_busy_pin = -1,
    .sx1262_reset_pin = -1,
    .sx1262_dio1_pin = -1,
    .i2c_sda_pin = -1,
    .i2c_scl_pin = -1,
    .keyboard_irq_pin = -1,
    .touch_irq_pin = -1,
    .encoder_a_pin = -1,
    .encoder_b_pin = -1,
    .encoder_key_pin = -1,
    .trackball_up_pin = -1,
    .trackball_down_pin = -1,
    .trackball_left_pin = -1,
    .trackball_right_pin = -1,
    .trackball_select_pin = -1,
    .es7210_mclk_pin = -1,
    .es7210_lrclk_pin = -1,
    .es7210_sclk_pin = -1,
    .es7210_data_pin = -1,
    .speaker_lrclk_pin = -1,
    .speaker_bclk_pin = -1,
    .speaker_data_pin = -1,
    .battery_type = RFSCOUT_BATTERY_NONE,
    .battery_adc_pin = -1,
    .battery_divider_numerator = 0,
    .battery_divider_denominator = 0,
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
    .name = "LILYGO T-Embed CC1101 Plus",
    .capabilities = RFSCOUT_BOARD_CAP_CC1101 | RFSCOUT_BOARD_CAP_NRF24 |
                    RFSCOUT_BOARD_CAP_ROTARY_ENCODER | RFSCOUT_BOARD_CAP_PDM_MIC |
                    RFSCOUT_BOARD_CAP_SPEAKER | RFSCOUT_BOARD_CAP_SD |
                    RFSCOUT_BOARD_CAP_SCREEN_OFF | RFSCOUT_BOARD_CAP_DEEP_SLEEP |
                    RFSCOUT_BOARD_CAP_POWER_OFF | RFSCOUT_BOARD_CAP_INTERNAL_TEMPERATURE,
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
    .sx1262_cs_pin = -1,
    .sx1262_busy_pin = -1,
    .sx1262_reset_pin = -1,
    .sx1262_dio1_pin = -1,
    .i2c_sda_pin = 8,
    .i2c_scl_pin = 18,
    .keyboard_i2c_address = 0,
    .keyboard_irq_pin = -1,
    .touch_irq_pin = -1,
    .encoder_a_pin = 4,
    .encoder_b_pin = 5,
    .encoder_key_pin = 0,
    .trackball_up_pin = -1,
    .trackball_down_pin = -1,
    .trackball_left_pin = -1,
    .trackball_right_pin = -1,
    .trackball_select_pin = -1,
    .es7210_mclk_pin = -1,
    .es7210_lrclk_pin = -1,
    .es7210_sclk_pin = -1,
    .es7210_data_pin = -1,
    .speaker_lrclk_pin = 40,
    .speaker_bclk_pin = 46,
    .speaker_data_pin = 7,
    .battery_type = RFSCOUT_BATTERY_I2C_GAUGE_CHARGER,
    .battery_adc_pin = -1,
    .battery_divider_numerator = 0,
    .battery_divider_denominator = 0,
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
    .gps_tx_pin = -1,
    .gps_rx_pin = -1,
};

static const rfscout_board_profile_t tdeck_plus_profile = {
    .id = RFSCOUT_BOARD_TDECK_PLUS,
    .name = "LILYGO T-Deck Plus",
    .capabilities = RFSCOUT_BOARD_CAP_GPS | RFSCOUT_BOARD_CAP_TRACKBALL | RFSCOUT_BOARD_CAP_TOUCH |
                    RFSCOUT_BOARD_CAP_SPEAKER | RFSCOUT_BOARD_CAP_SD |
                    RFSCOUT_BOARD_CAP_INTERNAL_TEMPERATURE | RFSCOUT_BOARD_CAP_GLOBAL_TABS,
    .power_enable_pin = 10,
    .user_key_pin = -1,
    .spi_mosi_pin = 41,
    .spi_miso_pin = 38,
    .spi_sclk_pin = 40,
    .display_cs_pin = 12,
    .display_dc_pin = 11,
    .display_reset_pin = -1,
    .display_backlight_pin = 42,
    .display_width = 320,
    .display_height = 240,
    .display_native_width = 240,
    .display_native_height = 320,
    .display_x_gap = 0,
    .display_y_gap = 0,
    .display_swap_xy = true,
    .display_mirror_x = true,
    .display_mirror_y = false,
    .display_invert_color = true,
    .backlight_type = RFSCOUT_BACKLIGHT_PULSE_16_STEP,
    .sd_cs_pin = 39,
    .cc1101_cs_pin = -1,
    .cc1101_gdo2_pin = -1,
    .cc1101_gdo0_pin = -1,
    .cc1101_switch_1_pin = -1,
    .cc1101_switch_0_pin = -1,
    .nrf24_cs_pin = -1,
    .nrf24_ce_pin = -1,
    .sx1262_cs_pin = 9,
    .sx1262_busy_pin = 13,
    .sx1262_reset_pin = 17,
    .sx1262_dio1_pin = 45,
    .i2c_sda_pin = 18,
    .i2c_scl_pin = 8,
    .keyboard_i2c_address = 0x55,
    .keyboard_irq_pin = 46,
    .touch_irq_pin = 16,
    .encoder_a_pin = -1,
    .encoder_b_pin = -1,
    .encoder_key_pin = -1,
    .trackball_up_pin = 3,
    .trackball_down_pin = 15,
    .trackball_left_pin = 1,
    .trackball_right_pin = 2,
    .trackball_select_pin = 0,
    .es7210_mclk_pin = 48,
    .es7210_lrclk_pin = 21,
    .es7210_sclk_pin = 47,
    .es7210_data_pin = 14,
    .speaker_lrclk_pin = 5,
    .speaker_bclk_pin = 7,
    .speaker_data_pin = 6,
    .battery_type = RFSCOUT_BATTERY_ADC_VOLTAGE_ONLY,
    .battery_adc_pin = 4,
    .battery_divider_numerator = 2,
    .battery_divider_denominator = 1,
    .pn532_i2c_address = 0x24,
    .mlx90640_i2c_address = 0x33,
    .bq27220_i2c_address = 0,
    .bq25896_i2c_address = 0,
    .pn532_reset_pin = -1,
    .pn532_irq_pin = -1,
    .ir_enable_pin = -1,
    .ir_rx_pin = -1,
    .mic_data_pin = -1,
    .mic_clock_pin = -1,
    .gps_tx_pin = 44,
    .gps_rx_pin = 43,
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

esp_err_t board_profile_detect( void )
{
    if( RFSCOUT_BOARD_UNKNOWN != active_profile->id )
    {
        return ESP_OK;
    }

    static const uint8_t TDECK_SIGNATURES[] = { 0x5D, 0x14 };
    static const uint8_t TEMBED_SIGNATURES[] = { 0x6B, 0x55 };

    const bool tdeck = probe_fingerprint( 18, 8, TDECK_SIGNATURES, sizeof( TDECK_SIGNATURES ) );
    const bool tembed = probe_fingerprint( 8, 18, TEMBED_SIGNATURES, sizeof( TEMBED_SIGNATURES ) );

    if( tdeck && !tembed )
    {
        active_profile = &tdeck_plus_profile;
    }
    else if( tembed && !tdeck )
    {
        active_profile = &tembed_profile;
    }
    else
    {
        ESP_LOGE( TAG,
                  "hardware profile detection failed or ambiguous (T-Deck=%d T-Embed=%d)",
                  tdeck,
                  tembed );
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI( TAG, "selected hardware profile: %s", active_profile->name );
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
    return board_profile_selected() && 0U != ( active_profile->capabilities & (uint32_t)a_capability );
}
