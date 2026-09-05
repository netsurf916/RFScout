/**
    input.c : RFScout semantic input service
    Description: Implements the current T-Embed rotary/button provider behind board-neutral input.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "input.h"

#include "board.h"
#include "event.h"
#include "session_state.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include <stddef.h>

#define ENCODER_BUTTON_DEBOUNCE_US 30000LL
#define ENCODER_BUTTON_LONG_PRESS_US 800000LL
#define ENCODER_BUTTON_BACK_HOLD_US 1500000LL
#define ENCODER_BUTTON_LOCK_HOLD_US 2000000LL
#define ENCODER_LATCH_STATE_0 0U
#define ENCODER_LATCH_STATE_3 3U
#define ENCODER_TRANSITIONS_PER_DETENT 2

static const char *TAG = "input";
static volatile uint8_t encoder_state;
static volatile int encoder_accumulator;
static volatile int encoder_delta;
static portMUX_TYPE encoder_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE encoder_button_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool encoder_button_down;
static volatile int64_t encoder_button_pressed_us;
static volatile int64_t encoder_button_pending_duration_us;
static volatile uint32_t encoder_button_press_sequence;
static volatile uint32_t encoder_button_pending_sequence;
static uint32_t encoder_button_seen_sequence;
static uint32_t encoder_button_lock_sequence;
static bool encoder_button_event;
static bool encoder_button_long_event;
static bool encoder_button_back_event;
static bool encoder_button_lock_fired;
static bool locked;
static rfscout_input_provider_t provider;

static void IRAM_ATTR encoder_isr( void *a_argument )
{
    (void)a_argument;
    static const int8_t transitions[ 16 ] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };

    const uint8_t current = (uint8_t)( ( gpio_get_level( RFSCOUT_BOARD_ENCODER_A_PIN ) << 1 ) |
                                       gpio_get_level( RFSCOUT_BOARD_ENCODER_B_PIN ) );

    portENTER_CRITICAL_ISR( &encoder_lock );
    encoder_accumulator += transitions[ ( ( encoder_state << 2 ) | current ) & 0x0FU ];
    encoder_state = current;
    if( ( ENCODER_LATCH_STATE_0 == current ) || ( ENCODER_LATCH_STATE_3 == current ) )
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

static void IRAM_ATTR encoder_button_isr( void *a_argument )
{
    (void)a_argument;
    const bool pressed = ( 0 == gpio_get_level( RFSCOUT_BOARD_ENCODER_KEY_PIN ) );
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL_ISR( &encoder_button_lock );
    if( pressed )
    {
        if( !encoder_button_down )
        {
            encoder_button_down = true;
            encoder_button_pressed_us = now_us;
            ++encoder_button_press_sequence;
        }
    }
    else if( encoder_button_down )
    {
        const int64_t duration_us = now_us - encoder_button_pressed_us;
        encoder_button_down = false;
        encoder_button_pressed_us = 0;
        if( duration_us > encoder_button_pending_duration_us )
        {
            encoder_button_pending_duration_us = duration_us;
            encoder_button_pending_sequence = encoder_button_press_sequence;
        }
    }
    portEXIT_CRITICAL_ISR( &encoder_button_lock );
}

static void clear_encoder_delta( void )
{
    portENTER_CRITICAL( &encoder_lock );
    encoder_delta = 0;
    encoder_accumulator = 0;
    portEXIT_CRITICAL( &encoder_lock );
}

static int consume_encoder_delta( void )
{
    portENTER_CRITICAL( &encoder_lock );
    const int delta = locked ? 0 : encoder_delta;
    encoder_delta = 0;
    portEXIT_CRITICAL( &encoder_lock );
    return delta;
}

static bool consume_button_event( bool *a_event )
{
    const bool event = !locked && *a_event;
    *a_event = false;
    return event;
}

static void toggle_input_lock( uint32_t a_press_sequence )
{
    locked = !locked;
    encoder_button_lock_fired = true;
    encoder_button_lock_sequence = a_press_sequence;
    encoder_button_event = false;
    encoder_button_long_event = false;
    encoder_button_back_event = false;
    clear_encoder_delta();
    ESP_LOGI( TAG, "user input %s", locked ? "locked" : "unlocked" );
    rfscout_event_emit( locked ? RFSCOUT_EVENT_INPUT_LOCKED : RFSCOUT_EVENT_INPUT_UNLOCKED,
                        RFSCOUT_EVENT_SOURCE_SYSTEM,
                        locked ? 1 : 0 );
}

esp_err_t input_init( void )
{
#if !RFSCOUT_BOARD_HAS_ROTARY_ENCODER
    provider = RFSCOUT_INPUT_PROVIDER_NONE;
    return ESP_ERR_NOT_SUPPORTED;
#else
    gpio_config_t inputs = { .pin_bit_mask = ( 1ULL << RFSCOUT_BOARD_ENCODER_A_PIN ) |
                                             ( 1ULL << RFSCOUT_BOARD_ENCODER_B_PIN ) |
                                             ( 1ULL << RFSCOUT_BOARD_ENCODER_KEY_PIN ) |
                                             ( 1ULL << RFSCOUT_BOARD_USER_KEY_PIN ),
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_ENABLE,
                             .intr_type = GPIO_INTR_DISABLE };
    esp_err_t result = gpio_config( &inputs );
    if( ESP_OK != result )
    {
        return result;
    }

    encoder_state = (uint8_t)( ( gpio_get_level( RFSCOUT_BOARD_ENCODER_A_PIN ) << 1 ) |
                               gpio_get_level( RFSCOUT_BOARD_ENCODER_B_PIN ) );
    encoder_accumulator = 0;
    encoder_delta = 0;
    encoder_button_down = ( 0 == gpio_get_level( RFSCOUT_BOARD_ENCODER_KEY_PIN ) );
    encoder_button_pressed_us = encoder_button_down ? esp_timer_get_time() : 0;
    encoder_button_pending_duration_us = 0;
    encoder_button_press_sequence = encoder_button_down ? 1U : 0U;
    encoder_button_pending_sequence = 0U;
    encoder_button_seen_sequence = encoder_button_press_sequence;
    encoder_button_lock_sequence = 0U;
    encoder_button_event = false;
    encoder_button_long_event = false;
    encoder_button_back_event = false;
    encoder_button_lock_fired = false;
    rfscout_session_state_t restored_session;
    locked = session_state_restore( &restored_session ) && restored_session.input_locked;

    result = gpio_install_isr_service( 0 );
    if( ESP_OK != result )
    {
        return result;
    }
    if( ESP_OK !=
            ( result = gpio_set_intr_type( RFSCOUT_BOARD_ENCODER_A_PIN, GPIO_INTR_ANYEDGE ) ) ||
        ESP_OK !=
            ( result = gpio_set_intr_type( RFSCOUT_BOARD_ENCODER_B_PIN, GPIO_INTR_ANYEDGE ) ) ||
        ESP_OK !=
            ( result = gpio_set_intr_type( RFSCOUT_BOARD_ENCODER_KEY_PIN, GPIO_INTR_ANYEDGE ) ) ||
        ESP_OK !=
            ( result = gpio_isr_handler_add( RFSCOUT_BOARD_ENCODER_A_PIN, encoder_isr, NULL ) ) ||
        ESP_OK !=
            ( result = gpio_isr_handler_add( RFSCOUT_BOARD_ENCODER_B_PIN, encoder_isr, NULL ) ) ||
        ESP_OK != ( result = gpio_isr_handler_add(
                        RFSCOUT_BOARD_ENCODER_KEY_PIN, encoder_button_isr, NULL ) ) )
    {
        return result;
    }

    provider = RFSCOUT_INPUT_PROVIDER_ROTARY_BUTTONS;
    ESP_LOGI( TAG, "rotary/button input provider selected" );
    rfscout_event_emit(
        RFSCOUT_EVENT_INPUT_PROVIDER_SELECTED, RFSCOUT_EVENT_SOURCE_SYSTEM, (int32_t)provider );
    return ESP_OK;
#endif
}

void input_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    bool button_down;
    int64_t pressed_us;
    int64_t completed_duration_us;
    uint32_t press_sequence;
    uint32_t completed_sequence;

    portENTER_CRITICAL( &encoder_button_lock );
    button_down = encoder_button_down;
    pressed_us = encoder_button_pressed_us;
    completed_duration_us = encoder_button_pending_duration_us;
    completed_sequence = encoder_button_pending_sequence;
    encoder_button_pending_duration_us = 0;
    encoder_button_pending_sequence = 0U;
    press_sequence = encoder_button_press_sequence;
    portEXIT_CRITICAL( &encoder_button_lock );

    if( press_sequence != encoder_button_seen_sequence )
    {
        encoder_button_seen_sequence = press_sequence;
        encoder_button_lock_fired = ( press_sequence == encoder_button_lock_sequence );
    }

    if( button_down && !encoder_button_lock_fired && ( pressed_us > 0 ) &&
        ( now_us - pressed_us ) >= ENCODER_BUTTON_LOCK_HOLD_US )
    {
        toggle_input_lock( press_sequence );
    }

    if( completed_duration_us < ENCODER_BUTTON_DEBOUNCE_US ||
        completed_sequence == encoder_button_lock_sequence )
    {
        return;
    }
    if( completed_duration_us >= ENCODER_BUTTON_LOCK_HOLD_US )
    {
        toggle_input_lock( completed_sequence );
        return;
    }
    if( locked )
    {
        return;
    }

    if( completed_duration_us >= ENCODER_BUTTON_BACK_HOLD_US )
    {
        encoder_button_back_event = true;
    }
    else if( completed_duration_us >= ENCODER_BUTTON_LONG_PRESS_US )
    {
        encoder_button_long_event = true;
    }
    else
    {
        encoder_button_event = true;
    }
}

bool input_activity_pending( void )
{
    bool movement;
    bool button_activity;

    portENTER_CRITICAL( &encoder_lock );
    movement = ( 0 != encoder_delta );
    portEXIT_CRITICAL( &encoder_lock );

    portENTER_CRITICAL( &encoder_button_lock );
    button_activity = encoder_button_down || ( encoder_button_pending_duration_us > 0 );
    portEXIT_CRITICAL( &encoder_button_lock );

    return movement || button_activity || encoder_button_event || encoder_button_long_event ||
           encoder_button_back_event || ( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) );
}

void input_consume_wake( void )
{
    clear_encoder_delta();
    encoder_button_event = false;
    encoder_button_long_event = false;
    encoder_button_back_event = false;

    uint32_t press_sequence;
    bool button_down;
    portENTER_CRITICAL( &encoder_button_lock );
    encoder_button_pending_duration_us = 0;
    encoder_button_pending_sequence = 0U;
    press_sequence = encoder_button_press_sequence;
    button_down = encoder_button_down;
    portEXIT_CRITICAL( &encoder_button_lock );

    encoder_button_seen_sequence = press_sequence;
    encoder_button_lock_sequence = press_sequence;
    encoder_button_lock_fired = button_down;
}

bool input_get( rfscout_input_event_t *a_event )
{
    if( NULL == a_event )
    {
        return false;
    }

    a_event->type = RFSCOUT_INPUT_NONE;
    a_event->value = 0;
    if( encoder_button_back_event )
    {
        encoder_button_back_event = false;
        a_event->type = RFSCOUT_INPUT_BACK;
        return true;
    }
    if( consume_button_event( &encoder_button_long_event ) )
    {
        a_event->type = RFSCOUT_INPUT_LONG_SELECT;
        return true;
    }
    if( consume_button_event( &encoder_button_event ) )
    {
        a_event->type = RFSCOUT_INPUT_SELECT;
        return true;
    }

    const int delta = consume_encoder_delta();
    if( 0 != delta )
    {
        a_event->type = RFSCOUT_INPUT_NAVIGATION;
        a_event->value = delta;
        return true;
    }
    return false;
}

bool input_locked( void )
{
    return locked;
}

rfscout_input_provider_t input_provider( void )
{
    return provider;
}
