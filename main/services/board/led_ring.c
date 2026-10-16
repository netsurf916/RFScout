/**
    led_ring.c : WS2812 ring transport, isolated from application logic
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/
#include "led_ring.h"

#include "board.h"
#include "driver/rmt_tx.h"
#include "esp_rom_sys.h"
#include <string.h>

#define LED_RING_RESOLUTION_HZ 10000000U
#define LED_RING_GPIO 14

static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;
static rfscout_led_color_t frame[ RFSCOUT_LED_RING_COUNT ];
static uint8_t brightness_percent = 20U;
/* RMT may retain the payload until the transaction finishes, including on timeout. */
static uint8_t tx_data[ RFSCOUT_LED_RING_COUNT * 3U ];
static bool tx_pending;

bool led_ring_available( void )
{
    return RFSCOUT_BOARD_TEMBED_CC1101_PLUS == board_profile()->id;
}

esp_err_t led_ring_init( void )
{
    if( NULL != channel )
    {
        return ESP_OK;
    }
    if( !led_ring_available() )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const rmt_tx_channel_config_t config = {
        .gpio_num = LED_RING_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RING_RESOLUTION_HZ,
        .mem_block_symbols = 64U,
        .trans_queue_depth = 1U,
    };
    esp_err_t result = rmt_new_tx_channel( &config, &channel );
    if( ESP_OK != result )
    {
        return result;
    }
    const rmt_bytes_encoder_config_t bytes_config = {
        .bit0 = { .level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 9 },
        .bit1 = { .level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 5 },
        .flags.msb_first = 1,
    };
    result = rmt_new_bytes_encoder( &bytes_config, &encoder );
    if( ESP_OK == result )
    {
        result = rmt_enable( channel );
    }
    if( ESP_OK != result )
    {
        if( NULL != encoder )
        {
            rmt_del_encoder( encoder );
            encoder = NULL;
        }
        rmt_del_channel( channel );
        channel = NULL;
    }
    return result;
}

esp_err_t led_ring_set( unsigned a_index, rfscout_led_color_t a_color )
{
    if( a_index >= RFSCOUT_LED_RING_COUNT )
    {
        return ESP_ERR_INVALID_ARG;
    }
    frame[ a_index ] = a_color;
    return ESP_OK;
}

esp_err_t led_ring_show( void )
{
    if( NULL == channel || NULL == encoder )
    {
        return ESP_ERR_INVALID_STATE;
    }
    if( tx_pending )
    {
        const esp_err_t wait = rmt_tx_wait_all_done( channel, 100U );
        if( ESP_OK != wait )
        {
            return wait;
        }
        tx_pending = false;
    }
    for( unsigned index = 0U; index < RFSCOUT_LED_RING_COUNT; ++index )
    {
        const rfscout_led_color_t c = frame[ index ];
        tx_data[ index * 3U ] = (uint8_t)( ( (unsigned)c.green * brightness_percent ) / 100U );
        tx_data[ index * 3U + 1U ] = (uint8_t)( ( (unsigned)c.red * brightness_percent ) / 100U );
        tx_data[ index * 3U + 2U ] = (uint8_t)( ( (unsigned)c.blue * brightness_percent ) / 100U );
    }
    const rmt_transmit_config_t transmit = { .loop_count = 0 };
    esp_err_t result = rmt_transmit( channel, encoder, tx_data, sizeof( tx_data ), &transmit );
    if( ESP_OK == result )
    {
        tx_pending = true;
        result = rmt_tx_wait_all_done( channel, 100U );
        if( ESP_OK == result )
        {
            tx_pending = false;
            esp_rom_delay_us( 80U );
        }
    }
    return result;
}

esp_err_t led_ring_set_brightness( uint8_t a_percent )
{
    if( a_percent > 100U )
    {
        return ESP_ERR_INVALID_ARG;
    }
    brightness_percent = a_percent;
    return NULL == channel ? ESP_OK : led_ring_show();
}

esp_err_t led_ring_clear( void )
{
    memset( frame, 0, sizeof( frame ) );
    return NULL == channel ? ESP_OK : led_ring_show();
}

esp_err_t led_ring_deinit( void )
{
    if( NULL == channel )
    {
        memset( frame, 0, sizeof( frame ) );
        brightness_percent = 20U;
        return ESP_OK;
    }
    const esp_err_t clear_result = led_ring_clear();
    if( ESP_OK != clear_result )
    {
        /* Keep ownership on failure rather than allowing another app to inherit lit LEDs. */
        return clear_result;
    }
    esp_err_t result = rmt_disable( channel );
    if( ESP_OK != result )
    {
        return result;
    }
    if( NULL != encoder )
    {
        result = rmt_del_encoder( encoder );
        if( ESP_OK != result )
        {
            return result;
        }
        encoder = NULL;
    }
    result = rmt_del_channel( channel );
    if( ESP_OK == result )
    {
        channel = NULL;
        brightness_percent = 20U;
    }
    return result;
}
