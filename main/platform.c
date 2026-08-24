/**
    platform.c : RFScout board-level services
    Description: Initializes the LILYGO T-Embed CC1101 Plus shared hardware.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "platform.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "platform";
static volatile uint8_t encoder_state;
static volatile int encoder_accumulator;
static volatile int encoder_delta;
static portMUX_TYPE encoder_lock = portMUX_INITIALIZER_UNLOCKED;

/** @brief Decode one quadrature transition from either encoder GPIO edge. */
static void IRAM_ATTR encoder_isr( void *a_argument )
{
    (void)a_argument;
    static const int8_t transitions[ 16 ] =
    {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };

    const uint8_t current =
        (uint8_t)( ( gpio_get_level( RFSCOUT_BOARD_ENCODER_A_PIN ) << 1 ) |
                   gpio_get_level( RFSCOUT_BOARD_ENCODER_B_PIN ) );

    portENTER_CRITICAL_ISR( &encoder_lock );
    encoder_accumulator += transitions[ ( ( encoder_state << 2 ) | current ) & 0x0FU ];
    encoder_state = current;

    /* The T-Embed encoder produces a complete four-transition quadrature
       sequence per physical detent. Keeping the partial accumulator instead
       of zeroing it on noise makes direction changes and slow turns reliable. */
    while( encoder_accumulator >= 4 )
    {
        ++encoder_delta;
        encoder_accumulator -= 4;
    }
    while( encoder_accumulator <= -4 )
    {
        --encoder_delta;
        encoder_accumulator += 4;
    }
    portEXIT_CRITICAL_ISR( &encoder_lock );
}

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
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK( gpio_config( &inputs ) );

    encoder_state =
        (uint8_t)( ( gpio_get_level( RFSCOUT_BOARD_ENCODER_A_PIN ) << 1 ) |
                   gpio_get_level( RFSCOUT_BOARD_ENCODER_B_PIN ) );
    encoder_accumulator = 0;
    encoder_delta = 0;

    ESP_ERROR_CHECK( gpio_install_isr_service( 0 ) );
    ESP_ERROR_CHECK( gpio_set_intr_type( RFSCOUT_BOARD_ENCODER_A_PIN,
                                         GPIO_INTR_ANYEDGE ) );
    ESP_ERROR_CHECK( gpio_set_intr_type( RFSCOUT_BOARD_ENCODER_B_PIN,
                                         GPIO_INTR_ANYEDGE ) );
    ESP_ERROR_CHECK( gpio_isr_handler_add( RFSCOUT_BOARD_ENCODER_A_PIN,
                                           encoder_isr,
                                           NULL ) );
    ESP_ERROR_CHECK( gpio_isr_handler_add( RFSCOUT_BOARD_ENCODER_B_PIN,
                                           encoder_isr,
                                           NULL ) );

    ESP_LOGI( TAG, "T-Embed CC1101 Plus board GPIO initialized" );
    return ESP_OK;
}

void platform_poll( void )
{
}

int platform_encoder_delta( void )
{
    portENTER_CRITICAL( &encoder_lock );
    const int delta = encoder_delta;
    encoder_delta = 0;
    portEXIT_CRITICAL( &encoder_lock );
    return delta;
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
