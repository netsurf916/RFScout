/**
    platform.c : RFScout board-level services
    Description: Implements shared services for the selected RFScout board target.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "platform.h"
#include "board.h"
#include "event.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define ENCODER_BUTTON_DEBOUNCE_US 30000LL
#define ENCODER_BUTTON_LONG_PRESS_US 800000LL
#define ENCODER_BUTTON_LOCK_HOLD_US 2000000LL
#define ENCODER_LATCH_STATE_0 0U
#define ENCODER_LATCH_STATE_3 3U
#define ENCODER_TRANSITIONS_PER_DETENT 2

static const char *TAG = "platform";
static volatile uint8_t encoder_state;
static volatile int encoder_accumulator;
static volatile int encoder_delta;
static portMUX_TYPE encoder_lock = portMUX_INITIALIZER_UNLOCKED;
static bool encoder_button_raw;
static bool encoder_button_stable;
static bool encoder_button_event;
static bool encoder_button_long_event;
static bool encoder_button_lock_fired;
static bool input_locked;
static int64_t encoder_button_pressed_us;
static int64_t encoder_button_change_us;

static void IRAM_ATTR encoder_isr( void *a_argument )
{
    (void)a_argument;
    static const int8_t transitions[ 16 ] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
    const uint8_t current = (uint8_t)( ( gpio_get_level( RFSCOUT_BOARD_ENCODER_A_PIN ) << 1 ) |
                                       gpio_get_level( RFSCOUT_BOARD_ENCODER_B_PIN ) );
    portENTER_CRITICAL_ISR( &encoder_lock );
    encoder_accumulator += transitions[ ( ( encoder_state << 2 ) | current ) & 0x0FU ];
    encoder_state = current;
    if( ENCODER_LATCH_STATE_0 == current || ENCODER_LATCH_STATE_3 == current )
    {
        if( encoder_accumulator >= ENCODER_TRANSITIONS_PER_DETENT )
        {
            ++encoder_delta;
            encoder_accumulator = 0;
        }
        else if( encoder_accumulator <= -ENCODER_TRANSITIONS_PER_DETENT )
        {
            --encoder_delta;
            encoder_accumulator = 0;
        }
    }
    portEXIT_CRITICAL_ISR( &encoder_lock );
}

static void clear_encoder_delta( void )
{
    portENTER_CRITICAL( &encoder_lock );
    encoder_delta = 0;
    encoder_accumulator = 0;
    portEXIT_CRITICAL( &encoder_lock );
}

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
    ESP_ERROR_CHECK( gpio_config( &outputs ) );
    gpio_set_level( RFSCOUT_BOARD_POWER_ENABLE_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_DISPLAY_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_CC1101_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_SD_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_NRF24_CS_PIN, 1 );
    gpio_set_level( RFSCOUT_BOARD_NRF24_CE_PIN, 0 );

    gpio_config_t inputs = { .pin_bit_mask = ( 1ULL << RFSCOUT_BOARD_ENCODER_A_PIN ) |
                                             ( 1ULL << RFSCOUT_BOARD_ENCODER_B_PIN ) |
                                             ( 1ULL << RFSCOUT_BOARD_ENCODER_KEY_PIN ) |
                                             ( 1ULL << RFSCOUT_BOARD_USER_KEY_PIN ),
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_ENABLE,
                             .intr_type = GPIO_INTR_DISABLE };
    ESP_ERROR_CHECK( gpio_config( &inputs ) );
    encoder_state = (uint8_t)( ( gpio_get_level( RFSCOUT_BOARD_ENCODER_A_PIN ) << 1 ) |
                               gpio_get_level( RFSCOUT_BOARD_ENCODER_B_PIN ) );
    encoder_accumulator = 0;
    encoder_delta = 0;
    encoder_button_raw = 0 == gpio_get_level( RFSCOUT_BOARD_ENCODER_KEY_PIN );
    encoder_button_stable = encoder_button_raw;
    encoder_button_event = false;
    encoder_button_long_event = false;
    encoder_button_lock_fired = false;
    input_locked = false;
    encoder_button_pressed_us = 0;
    encoder_button_change_us = esp_timer_get_time();

    ESP_ERROR_CHECK( gpio_install_isr_service( 0 ) );
    ESP_ERROR_CHECK( gpio_set_intr_type( RFSCOUT_BOARD_ENCODER_A_PIN, GPIO_INTR_ANYEDGE ) );
    ESP_ERROR_CHECK( gpio_set_intr_type( RFSCOUT_BOARD_ENCODER_B_PIN, GPIO_INTR_ANYEDGE ) );
    ESP_ERROR_CHECK( gpio_isr_handler_add( RFSCOUT_BOARD_ENCODER_A_PIN, encoder_isr, NULL ) );
    ESP_ERROR_CHECK( gpio_isr_handler_add( RFSCOUT_BOARD_ENCODER_B_PIN, encoder_isr, NULL ) );
    ESP_LOGI( TAG, "%s board GPIO initialized", RFSCOUT_BOARD_NAME );
    return ESP_OK;
}

void platform_poll( void )
{
    const bool pressed = 0 == gpio_get_level( RFSCOUT_BOARD_ENCODER_KEY_PIN );
    const int64_t now_us = esp_timer_get_time();
    if( pressed != encoder_button_raw )
    {
        encoder_button_raw = pressed;
        encoder_button_change_us = now_us;
    }
    else if( pressed != encoder_button_stable &&
             now_us - encoder_button_change_us >= ENCODER_BUTTON_DEBOUNCE_US )
    {
        encoder_button_stable = pressed;
        if( pressed )
        {
            encoder_button_pressed_us = now_us;
            encoder_button_lock_fired = false;
        }
        else
        {
            const int64_t held_us = now_us - encoder_button_pressed_us;
            if( !encoder_button_lock_fired && !input_locked )
            {
                if( held_us >= ENCODER_BUTTON_LONG_PRESS_US )
                    encoder_button_long_event = true;
                else
                    encoder_button_event = true;
            }
            encoder_button_pressed_us = 0;
        }
    }

    if( encoder_button_stable && !encoder_button_lock_fired && encoder_button_pressed_us > 0 &&
        now_us - encoder_button_pressed_us >= ENCODER_BUTTON_LOCK_HOLD_US )
    {
        input_locked = !input_locked;
        encoder_button_lock_fired = true;
        encoder_button_event = false;
        encoder_button_long_event = false;
        clear_encoder_delta();
        ESP_LOGI( TAG, "user input %s", input_locked ? "locked" : "unlocked" );
        /* Emit only after the semantic transition is committed; the hold itself remains silent. */
        rfscout_event_emit( input_locked ? RFSCOUT_EVENT_INPUT_LOCKED
                                         : RFSCOUT_EVENT_INPUT_UNLOCKED,
                            RFSCOUT_EVENT_SOURCE_SYSTEM,
                            input_locked ? 1 : 0 );
    }
}

bool platform_input_get( rfscout_input_event_t *a_event )
{
    if( NULL == a_event )
        return false;
    a_event->type = RFSCOUT_INPUT_NONE;
    a_event->value = 0;
    if( platform_encoder_button_long_pressed() )
    {
        a_event->type = RFSCOUT_INPUT_LONG_SELECT;
        return true;
    }
    if( platform_encoder_button_pressed() )
    {
        a_event->type = RFSCOUT_INPUT_SELECT;
        return true;
    }
    const int delta = platform_encoder_delta();
    if( 0 != delta )
    {
        a_event->type = RFSCOUT_INPUT_NAVIGATION;
        a_event->value = delta;
        return true;
    }
    return false;
}

int platform_encoder_delta( void )
{
    portENTER_CRITICAL( &encoder_lock );
    const int delta = input_locked ? 0 : encoder_delta;
    encoder_delta = 0;
    portEXIT_CRITICAL( &encoder_lock );
    return delta;
}

bool platform_encoder_button_pressed( void )
{
    const bool event = !input_locked && encoder_button_event;
    encoder_button_event = false;
    return event;
}
bool platform_encoder_button_long_pressed( void )
{
    const bool event = !input_locked && encoder_button_long_event;
    encoder_button_long_event = false;
    return event;
}
bool platform_input_locked( void )
{
    return input_locked;
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
