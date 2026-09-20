/**
    platform.c : RFScout board-level services
    Description: Implements shared non-input services for the runtime-selected RFScout board profile.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "platform.h"

#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "platform";

static void add_output_pin( uint64_t *a_mask, int a_pin )
{
    if( NULL != a_mask && a_pin >= 0 && a_pin < 64 )
    {
        *a_mask |= 1ULL << (unsigned)a_pin;
    }
}

static void set_level_if_valid( int a_pin, uint32_t a_level )
{
    if( a_pin >= 0 )
    {
        (void)gpio_set_level( (gpio_num_t)a_pin, a_level );
    }
}

esp_err_t platform_init( void )
{
    esp_err_t result = board_profile_detect();
    if( ESP_OK != result )
    {
        ESP_LOGE( TAG, "hardware profile detection failed; board GPIO will remain untouched" );
        return result;
    }

    uint64_t output_mask = 0U;
    add_output_pin( &output_mask, RFSCOUT_BOARD_POWER_ENABLE_PIN );
    add_output_pin( &output_mask, RFSCOUT_BOARD_DISPLAY_CS_PIN );
    add_output_pin( &output_mask, RFSCOUT_BOARD_SD_CS_PIN );
    add_output_pin( &output_mask, RFSCOUT_BOARD_CC1101_CS_PIN );
    add_output_pin( &output_mask, RFSCOUT_BOARD_NRF24_CS_PIN );
    add_output_pin( &output_mask, RFSCOUT_BOARD_NRF24_CE_PIN );
    add_output_pin( &output_mask, RFSCOUT_BOARD_CC1101_SWITCH_1_PIN );
    add_output_pin( &output_mask, RFSCOUT_BOARD_CC1101_SWITCH_0_PIN );

    if( 0U != output_mask )
    {
        gpio_config_t outputs = { .pin_bit_mask = output_mask, .mode = GPIO_MODE_OUTPUT };
        result = gpio_config( &outputs );
        if( ESP_OK != result )
        {
            return result;
        }
    }

    set_level_if_valid( RFSCOUT_BOARD_POWER_ENABLE_PIN, 1U );
    set_level_if_valid( RFSCOUT_BOARD_DISPLAY_CS_PIN, 1U );
    set_level_if_valid( RFSCOUT_BOARD_SD_CS_PIN, 1U );
    set_level_if_valid( RFSCOUT_BOARD_CC1101_CS_PIN, 1U );
    set_level_if_valid( RFSCOUT_BOARD_NRF24_CS_PIN, 1U );
    set_level_if_valid( RFSCOUT_BOARD_NRF24_CE_PIN, 0U );

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
