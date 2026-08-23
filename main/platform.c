/**
    platform.c : RFScout board-level services
    Description: Initializes the LILYGO T-Embed CC1101 Plus shared hardware.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "platform.h"

#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "platform";

esp_err_t platform_init( void )
{
    gpio_config_t outputs =
    {
        .pin_bit_mask = ( 1ULL << RFSCOUT_BOARD_POWER_ENABLE_PIN ) |
                        ( 1ULL << RFSCOUT_BOARD_DISPLAY_CS_PIN ) |
                        ( 1ULL << RFSCOUT_BOARD_CC1101_CS_PIN ) |
                        ( 1ULL << RFSCOUT_BOARD_SD_CS_PIN ) |
                        ( 1ULL << RFSCOUT_BOARD_NRF24_CS_PIN ) |
                        ( 1ULL << RFSCOUT_BOARD_NRF24_CE_PIN ) |
                        ( 1ULL << RFSCOUT_BOARD_CC1101_SWITCH_1_PIN ) |
                        ( 1ULL << RFSCOUT_BOARD_CC1101_SWITCH_0_PIN ),
        .mode = GPIO_MODE_OUTPUT
    };
    ESP_ERROR_CHECK( gpio_config( &outputs ) );

    gpio_set_level( RFSCOUT_BOARD_POWER_ENABLE_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_DISPLAY_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_CC1101_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_SD_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_NRF24_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_NRF24_CE_PIN, 0 );

    gpio_config_t inputs =
    {
        .pin_bit_mask = ( 1ULL << RFSCOUT_BOARD_ENCODER_A_PIN ) |
                        ( 1ULL << RFSCOUT_BOARD_ENCODER_B_PIN ) |
                        ( 1ULL << RFSCOUT_BOARD_ENCODER_KEY_PIN ) |
                        ( 1ULL << RFSCOUT_BOARD_USER_KEY_PIN ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    ESP_ERROR_CHECK( gpio_config( &inputs ) );

    ESP_LOGI( TAG, "T-Embed CC1101 Plus board GPIO initialized" );
    return ESP_OK;
}

void platform_poll( void )
{
}

void platform_set_alert_led( bool a_active )
{
    (void)a_active;
}

void platform_audio_tone( bool a_enabled, uint16_t a_frequency_hz, void *a_context )
{
    (void)a_enabled;
    (void)a_frequency_hz;
    (void)a_context;
}
