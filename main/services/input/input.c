/**
    input.c : RFScout semantic input service
    Description: Selects the runtime board input provider and normalizes controls into semantic events.
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
#include <stdint.h>

#define INPUT_BUTTON_DEBOUNCE_US 30000LL
#define INPUT_BUTTON_LONG_PRESS_US 800000LL
#define INPUT_BUTTON_BACK_HOLD_US 1500000LL
#define INPUT_BUTTON_LOCK_HOLD_US 2000000LL
#define ENCODER_LATCH_STATE_0 0U
#define ENCODER_LATCH_STATE_3 3U
#define ENCODER_TRANSITIONS_PER_DETENT 2

static const char *TAG = "input";
static volatile uint8_t encoder_state;
static volatile int navigation_accumulator;
static volatile int navigation_delta;
static portMUX_TYPE navigation_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE button_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool button_down;
static volatile int64_t button_pressed_us;
static volatile int64_t button_pending_duration_us;
static volatile uint32_t button_press_sequence;
static volatile uint32_t button_pending_sequence;
static uint32_t button_seen_sequence;
static uint32_t button_lock_sequence;
static bool button_event;
static bool button_long_event;
static bool button_back_event;
static bool button_lock_fired;
static bool locked;
static int primary_button_pin = -1;
static rfscout_input_provider_t provider;

static void IRAM_ATTR encoder_isr( void *a_argument )
{
    (void)a_argument;
    static const int8_t transitions[ 16 ] = { 0, -1, 1, 0, 1, 0, 0, -1,
                                             -1, 0, 0, 1, 0, 1, -1, 0 };

    const uint8_t current = (uint8_t)( ( gpio_get_level( RFSCOUT_BOARD_ENCODER_A_PIN ) << 1 ) |
                                       gpio_get_level( RFSCOUT_BOARD_ENCODER_B_PIN ) );

    portENTER_CRITICAL_ISR( &navigation_lock );
    navigation_accumulator += transitions[ ( ( encoder_state << 2 ) | current ) & 0x0FU ];
    encoder_state = current;
    if( ( ENCODER_LATCH_STATE_0 == current ) || ( ENCODER_LATCH_STATE_3 == current ) )
    {
        if( navigation_accumulator >= ENCODER_TRANSITIONS_PER_DETENT )
        {
            ++navigation_delta;
            navigation_accumulator = 0;
        }
        else if( navigation_accumulator <= -ENCODER_TRANSITIONS_PER_DETENT )
        {
            --navigation_delta;
            navigation_accumulator = 0;
        }
    }
    portEXIT_CRITICAL_ISR( &navigation_lock );
}

static void IRAM_ATTR trackball_isr( void *a_argument )
{
    const int direction = (int)(intptr_t)a_argument;
    portENTER_CRITICAL_ISR( &navigation_lock );
    navigation_delta += direction;
    portEXIT_CRITICAL_ISR( &navigation_lock );
}

static void IRAM_ATTR primary_button_isr( void *a_argument )
{
    (void)a_argument;
    const bool pressed = ( primary_button_pin >= 0 ) && ( 0 == gpio_get_level( primary_button_pin ) );
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL_ISR( &button_lock );
    if( pressed )
    {
        if( !button_down )
        {
            button_down = true;
            button_pressed_us = now_us;
            ++button_press_sequence;
        }
    }
    else if( button_down )
    {
        const int64_t duration_us = now_us - button_pressed_us;
        button_down = false;
        button_pressed_us = 0;
        if( duration_us > button_pending_duration_us )
        {
            button_pending_duration_us = duration_us;
            button_pending_sequence = button_press_sequence;
        }
    }
    portEXIT_CRITICAL_ISR( &button_lock );
}

static void clear_navigation_delta( void )
{
    portENTER_CRITICAL( &navigation_lock );
    navigation_delta = 0;
    navigation_accumulator = 0;
    portEXIT_CRITICAL( &navigation_lock );
}

static int consume_navigation_delta( void )
{
    portENTER_CRITICAL( &navigation_lock );
    const int delta = locked ? 0 : navigation_delta;
    navigation_delta = 0;
    portEXIT_CRITICAL( &navigation_lock );
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
    button_lock_fired = true;
    button_lock_sequence = a_press_sequence;
    button_event = false;
    button_long_event = false;
    button_back_event = false;
    clear_navigation_delta();
    ESP_LOGI( TAG, "user input %s", locked ? "locked" : "unlocked" );
    rfscout_event_emit( locked ? RFSCOUT_EVENT_INPUT_LOCKED : RFSCOUT_EVENT_INPUT_UNLOCKED,
                        RFSCOUT_EVENT_SOURCE_SYSTEM,
                        locked ? 1 : 0 );
}

static void initialize_button_state( void )
{
    button_down = ( primary_button_pin >= 0 ) && ( 0 == gpio_get_level( primary_button_pin ) );
    button_pressed_us = button_down ? esp_timer_get_time() : 0;
    button_pending_duration_us = 0;
    button_press_sequence = button_down ? 1U : 0U;
    button_pending_sequence = 0U;
    button_seen_sequence = button_press_sequence;
    button_lock_sequence = 0U;
    button_event = false;
    button_long_event = false;
    button_back_event = false;
    button_lock_fired = false;
    rfscout_session_state_t restored_session;
    locked = session_state_restore( &restored_session ) && restored_session.input_locked;
}

static esp_err_t install_rotary_provider( void )
{
    primary_button_pin = RFSCOUT_BOARD_ENCODER_KEY_PIN;
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
    navigation_accumulator = 0;
    navigation_delta = 0;
    initialize_button_state();

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
                        RFSCOUT_BOARD_ENCODER_KEY_PIN, primary_button_isr, NULL ) ) )
    {
        return result;
    }
    provider = RFSCOUT_INPUT_PROVIDER_ROTARY_BUTTONS;
    return ESP_OK;
}

static esp_err_t install_trackball_provider( void )
{
    primary_button_pin = RFSCOUT_BOARD_TRACKBALL_SELECT_PIN;
    const uint64_t mask = ( 1ULL << RFSCOUT_BOARD_TRACKBALL_UP_PIN ) |
                          ( 1ULL << RFSCOUT_BOARD_TRACKBALL_DOWN_PIN ) |
                          ( 1ULL << RFSCOUT_BOARD_TRACKBALL_LEFT_PIN ) |
                          ( 1ULL << RFSCOUT_BOARD_TRACKBALL_RIGHT_PIN ) |
                          ( 1ULL << RFSCOUT_BOARD_TRACKBALL_SELECT_PIN );
    gpio_config_t inputs = { .pin_bit_mask = mask,
                             .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_ENABLE,
                             .intr_type = GPIO_INTR_DISABLE };
    esp_err_t result = gpio_config( &inputs );
    if( ESP_OK != result )
    {
        return result;
    }

    navigation_accumulator = 0;
    navigation_delta = 0;
    initialize_button_state();

    result = gpio_install_isr_service( 0 );
    if( ESP_OK != result )
    {
        return result;
    }
    const int pins[] = { RFSCOUT_BOARD_TRACKBALL_UP_PIN,
                         RFSCOUT_BOARD_TRACKBALL_LEFT_PIN,
                         RFSCOUT_BOARD_TRACKBALL_DOWN_PIN,
                         RFSCOUT_BOARD_TRACKBALL_RIGHT_PIN };
    const int directions[] = { -1, -1, 1, 1 };
    for( size_t index = 0U; index < sizeof( pins ) / sizeof( pins[ 0 ] ); ++index )
    {
        if( ESP_OK != ( result = gpio_set_intr_type( pins[ index ], GPIO_INTR_NEGEDGE ) ) ||
            ESP_OK != ( result = gpio_isr_handler_add(
                            pins[ index ], trackball_isr, (void *)(intptr_t)directions[ index ] ) ) )
        {
            return result;
        }
    }
    if( ESP_OK !=
            ( result = gpio_set_intr_type( RFSCOUT_BOARD_TRACKBALL_SELECT_PIN, GPIO_INTR_ANYEDGE ) ) ||
        ESP_OK != ( result = gpio_isr_handler_add(
                        RFSCOUT_BOARD_TRACKBALL_SELECT_PIN, primary_button_isr, NULL ) ) )
    {
        return result;
    }
    provider = RFSCOUT_INPUT_PROVIDER_TRACKBALL_BUTTONS;
    return ESP_OK;
}

esp_err_t input_init( void )
{
    provider = RFSCOUT_INPUT_PROVIDER_NONE;
    esp_err_t result = ESP_ERR_NOT_SUPPORTED;
    if( RFSCOUT_BOARD_HAS_ROTARY_ENCODER )
    {
        result = install_rotary_provider();
    }
    else if( RFSCOUT_BOARD_HAS_TRACKBALL )
    {
        result = install_trackball_provider();
    }
    if( ESP_OK != result )
    {
        return result;
    }

    ESP_LOGI( TAG,
              "%s input provider selected",
              RFSCOUT_INPUT_PROVIDER_ROTARY_BUTTONS == provider ? "rotary/button"
                                                                : "trackball/button" );
    rfscout_event_emit(
        RFSCOUT_EVENT_INPUT_PROVIDER_SELECTED, RFSCOUT_EVENT_SOURCE_SYSTEM, (int32_t)provider );
    return ESP_OK;
}

void input_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    bool current_button_down;
    int64_t pressed_us;
    int64_t completed_duration_us;
    uint32_t press_sequence;
    uint32_t completed_sequence;

    portENTER_CRITICAL( &button_lock );
    current_button_down = button_down;
    pressed_us = button_pressed_us;
    completed_duration_us = button_pending_duration_us;
    completed_sequence = button_pending_sequence;
    button_pending_duration_us = 0;
    button_pending_sequence = 0U;
    press_sequence = button_press_sequence;
    portEXIT_CRITICAL( &button_lock );

    if( press_sequence != button_seen_sequence )
    {
        button_seen_sequence = press_sequence;
        button_lock_fired = ( press_sequence == button_lock_sequence );
    }

    if( current_button_down && !button_lock_fired && ( pressed_us > 0 ) &&
        ( now_us - pressed_us ) >= INPUT_BUTTON_LOCK_HOLD_US )
    {
        toggle_input_lock( press_sequence );
    }

    if( completed_duration_us < INPUT_BUTTON_DEBOUNCE_US ||
        completed_sequence == button_lock_sequence )
    {
        return;
    }
    if( completed_duration_us >= INPUT_BUTTON_LOCK_HOLD_US )
    {
        toggle_input_lock( completed_sequence );
        return;
    }
    if( locked )
    {
        return;
    }

    if( completed_duration_us >= INPUT_BUTTON_BACK_HOLD_US )
    {
        button_back_event = true;
    }
    else if( completed_duration_us >= INPUT_BUTTON_LONG_PRESS_US )
    {
        button_long_event = true;
    }
    else
    {
        button_event = true;
    }
}

bool input_activity_pending( void )
{
    bool movement;
    bool button_activity;

    portENTER_CRITICAL( &navigation_lock );
    movement = ( 0 != navigation_delta );
    portEXIT_CRITICAL( &navigation_lock );

    portENTER_CRITICAL( &button_lock );
    button_activity = button_down || ( button_pending_duration_us > 0 );
    portEXIT_CRITICAL( &button_lock );

    const bool user_key = RFSCOUT_BOARD_USER_KEY_PIN >= 0 &&
                          0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN );
    return movement || button_activity || button_event || button_long_event || button_back_event ||
           user_key;
}

void input_consume_wake( void )
{
    clear_navigation_delta();
    button_event = false;
    button_long_event = false;
    button_back_event = false;

    uint32_t press_sequence;
    bool current_button_down;
    portENTER_CRITICAL( &button_lock );
    button_pending_duration_us = 0;
    button_pending_sequence = 0U;
    press_sequence = button_press_sequence;
    current_button_down = button_down;
    portEXIT_CRITICAL( &button_lock );

    button_seen_sequence = press_sequence;
    button_lock_sequence = press_sequence;
    button_lock_fired = current_button_down;
}

bool input_get( rfscout_input_event_t *a_event )
{
    if( NULL == a_event )
    {
        return false;
    }

    a_event->type = RFSCOUT_INPUT_NONE;
    a_event->value = 0;
    if( button_back_event )
    {
        button_back_event = false;
        a_event->type = RFSCOUT_INPUT_BACK;
        return true;
    }
    if( consume_button_event( &button_long_event ) )
    {
        a_event->type = RFSCOUT_INPUT_LONG_SELECT;
        return true;
    }
    if( consume_button_event( &button_event ) )
    {
        a_event->type = RFSCOUT_INPUT_SELECT;
        return true;
    }

    const int delta = consume_navigation_delta();
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
