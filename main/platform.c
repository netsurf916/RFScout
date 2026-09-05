/**
    platform.c : RFScout board-level services
    Description: Implements shared non-input services for the selected RFScout board target.
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
    gpio_config_t outputs = {
        .pin_bit_mask =
            ( 1ULL << RFSCOUT_BOARD_POWER_ENABLE_PIN ) | ( 1ULL << RFSCOUT_BOARD_DISPLAY_CS_PIN ) |
            ( 1ULL << RFSCOUT_BOARD_CC1101_CS_PIN ) | ( 1ULL << RFSCOUT_BOARD_SD_CS_PIN ) |
            ( 1ULL << RFSCOUT_BOARD_NRF24_CS_PIN ) | ( 1ULL << RFSCOUT_BOARD_NRF24_CE_PIN ) |
            ( 1ULL << RFSCOUT_BOARD_CC1101_SWITCH_1_PIN ) |
            ( 1ULL << RFSCOUT_BOARD_CC1101_SWITCH_0_PIN ),
        .mode = GPIO_MODE_OUTPUT };
    esp_err_t result = gpio_config( &outputs );
    if( ESP_OK != result )
    {
        return result;
    }

    gpio_set_level( RFSCOUT_BOARD_POWER_ENABLE_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_DISPLAY_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_CC1101_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_SD_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_NRF24_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_NRF24_CE_PIN, 0 );

    ESP_LOGI( TAG, "%s shared board GPIO initialized", RFSCOUT_BOARD_NAME );
    return ESP_OK;
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
