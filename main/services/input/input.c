/**
    input.c : RFScout semantic input service
    Description: Selects the runtime board input provider and normalizes controls into semantic events.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "input.h"

#include "board.h"
#include "event.h"
#include "log_writer.h"
#include "rotary_decoder.h"
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

/* Investigation PR #141: enable only on diagnostic builds. Set to 0 for release. */
#ifndef RFSCOUT_INPUT_TRACE_ENABLED
#define RFSCOUT_INPUT_TRACE_ENABLED 1
#endif

#define INPUT_TRACE_CAPACITY 128U
#define INPUT_TRACE_POLL_BURST 16U

static const char *TAG = "input";
static rotary_decoder_t navigation_decoder;
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
static bool button_long_press_event;
static bool button_global_hold_event;
static bool button_lock_fired;
static bool locked;
static int primary_button_pin = -1;
static rfscout_input_provider_t provider;

#if RFSCOUT_INPUT_TRACE_ENABLED
typedef struct
{
    int64_t edge_us;
    uint32_t invalid;
    int32_t pending;
    uint8_t ab;
    uint8_t anchor;
    uint8_t intermediate;
    int8_t step;
    bool synchronized;
} input_edge_trace_t;

static input_edge_trace_t edge_trace[ INPUT_TRACE_CAPACITY ];
static uint16_t edge_head;
static uint16_t edge_tail;
static uint32_t edge_drops;
static uint32_t edge_record_seq;

/* Called only with navigation_lock held, including from the GPIO ISR.
   No allocation, SD access, printf or ESP_LOG in the interrupt. */
static void IRAM_ATTR record_encoder_edge( uint8_t a_ab, int32_t a_before )
{
    const uint16_t next = (uint16_t)( ( edge_head + 1U ) % INPUT_TRACE_CAPACITY );
    if( next == edge_tail )
    {
        ++edge_drops;
        return;
    }
    input_edge_trace_t *item = &edge_trace[ edge_head ];
    item->edge_us = esp_timer_get_time();
    item->ab = a_ab;
    item->step = (int8_t)( navigation_decoder.pending_delta > a_before
                               ? 1
                               : ( navigation_decoder.pending_delta < a_before ? -1 : 0 ) );
    item->pending = navigation_decoder.pending_delta;
    item->invalid = navigation_decoder.invalid_transitions;
    item->anchor = navigation_decoder.anchor_state;
    item->intermediate = navigation_decoder.intermediate_state;
    item->synchronized = navigation_decoder.synchronized;
    edge_head = next;
}
#endif

void input_trace_note( const char *a_stage, int32_t a_value, int32_t a_aux )
{
#if RFSCOUT_INPUT_TRACE_ENABLED
    if( NULL == a_stage )
    {
        return;
    }
    int32_t pending;
    uint32_t invalid;
    uint8_t ab;
    portENTER_CRITICAL( &navigation_lock );
    pending = navigation_decoder.pending_delta;
    invalid = navigation_decoder.invalid_transitions;
    ab = navigation_decoder.last_state;
    portEXIT_CRITICAL( &navigation_lock );
    rfscout_log_record_t record;
    if( log_begin( &record, "input_trace" ) && log_str( &record, "stage", a_stage ) &&
        log_int( &record, "mono_us", esp_timer_get_time() ) &&
        log_int( &record, "value", a_value ) && log_int( &record, "aux", a_aux ) &&
        log_uint( &record, "ab", ab ) && log_int( &record, "pending", pending ) &&
        log_uint( &record, "invalid", invalid ) && log_bool( &record, "input_locked", locked ) )
    {
        (void)log_end( &record );
    }
#else
    (void)a_stage;
    (void)a_value;
    (void)a_aux;
#endif
}

static void input_trace_drain( void )
{
#if RFSCOUT_INPUT_TRACE_ENABLED
    for( unsigned count = 0U; count < INPUT_TRACE_POLL_BURST; ++count )
    {
        input_edge_trace_t item;
        uint32_t drops = 0U;
        bool has_item = false;
        uint32_t sequence = 0U;
        portENTER_CRITICAL( &navigation_lock );
        if( edge_tail != edge_head )
        {
            item = edge_trace[ edge_tail ];
            edge_tail = (uint16_t)( ( edge_tail + 1U ) % INPUT_TRACE_CAPACITY );
            sequence = ++edge_record_seq;
            has_item = true;
        }
        else if( edge_drops > 0U )
        {
            drops = edge_drops;
            edge_drops = 0U;
        }
        portEXIT_CRITICAL( &navigation_lock );
        if( !has_item )
        {
            if( drops > 0U )
            {
                input_trace_note( "edge_overflow", (int32_t)drops, 0 );
            }
            break;
        }
        rfscout_log_record_t record;
        if( log_begin( &record, "rotary_edge" ) && log_int( &record, "mono_us", item.edge_us ) &&
            log_uint( &record, "seq", sequence ) && log_uint( &record, "ab", item.ab ) &&
            log_uint( &record, "anchor", item.anchor ) &&
            log_uint( &record, "intermediate", item.intermediate ) &&
            log_int( &record, "step", item.step ) && log_int( &record, "pending", item.pending ) &&
            log_uint( &record, "invalid", item.invalid ) &&
            log_bool( &record, "synced", item.synchronized ) )
        {
            (void)log_end( &record );
        }
    }
#endif
}

static void IRAM_ATTR encoder_isr( void *a_argument )
{
    (void)a_argument;
    const uint8_t current = (uint8_t)( ( gpio_get_level( RFSCOUT_BOARD_ENCODER_A_PIN ) << 1 ) |
                                       gpio_get_level( RFSCOUT_BOARD_ENCODER_B_PIN ) );

    portENTER_CRITICAL_ISR( &navigation_lock );
#if RFSCOUT_INPUT_TRACE_ENABLED
    const int32_t before = navigation_decoder.pending_delta;
#endif
    rotary_decoder_sample( &navigation_decoder, current );
#if RFSCOUT_INPUT_TRACE_ENABLED
    record_encoder_edge( current, before );
#endif
    portEXIT_CRITICAL_ISR( &navigation_lock );
}

static void IRAM_ATTR primary_button_isr( void *a_argument )
{
    (void)a_argument;
    const bool pressed =
        ( primary_button_pin >= 0 ) && ( 0 == gpio_get_level( primary_button_pin ) );
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
    rotary_decoder_clear_pending( &navigation_decoder );
    portEXIT_CRITICAL( &navigation_lock );
}

static int consume_navigation_delta( void )
{
    portENTER_CRITICAL( &navigation_lock );
    const int32_t raw_delta = navigation_decoder.pending_delta;
    const int delta = locked ? 0 : rotary_decoder_take_direction( &navigation_decoder );
    const bool was_locked = locked;
    if( was_locked )
    {
        rotary_decoder_clear_pending( &navigation_decoder );
    }
    portEXIT_CRITICAL( &navigation_lock );
    if( 0 != raw_delta )
    {
        input_trace_note( was_locked ? "nav_locked_discard" : "nav_consumed", raw_delta, delta );
    }
    return delta;
}

static bool consume_button_event( bool *a_event )
{
    const bool event = !locked && *a_event;
    *a_event = false;
    return event;
}

void input_set_locked( bool a_locked )
{
    if( locked == a_locked )
    {
        return;
    }

    locked = a_locked;
    button_event = false;
    button_long_press_event = false;
    button_global_hold_event = false;
    clear_navigation_delta();
    input_trace_note( "input_lock_change", locked ? 1 : 0, 0 );
    ESP_LOGI( TAG, "user input %s", locked ? "locked" : "unlocked" );
    rfscout_event_emit( locked ? RFSCOUT_EVENT_INPUT_LOCKED : RFSCOUT_EVENT_INPUT_UNLOCKED,
                        RFSCOUT_EVENT_SOURCE_SYSTEM,
                        locked ? 1 : 0 );
}

static void queue_global_hold( uint32_t a_press_sequence )
{
    button_lock_fired = true;
    button_lock_sequence = a_press_sequence;
    button_event = false;
    button_long_press_event = false;
    button_global_hold_event = true;
    clear_navigation_delta();
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
    button_long_press_event = false;
    button_global_hold_event = false;
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

    const uint8_t initial_state =
        (uint8_t)( ( gpio_get_level( RFSCOUT_BOARD_ENCODER_A_PIN ) << 1 ) |
                   gpio_get_level( RFSCOUT_BOARD_ENCODER_B_PIN ) );
    rotary_decoder_reset( &navigation_decoder, initial_state );
#if RFSCOUT_INPUT_TRACE_ENABLED
    edge_head = 0U;
    edge_tail = 0U;
    edge_drops = 0U;
    edge_record_seq = 0U;
#endif
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

esp_err_t input_init( void )
{
    provider = RFSCOUT_INPUT_PROVIDER_NONE;
    if( !RFSCOUT_BOARD_HAS_ROTARY_ENCODER )
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const esp_err_t result = install_rotary_provider();
    if( ESP_OK != result )
    {
        return result;
    }

    ESP_LOGI( TAG, "rotary/button input provider selected" );
    rfscout_event_emit(
        RFSCOUT_EVENT_INPUT_PROVIDER_SELECTED, RFSCOUT_EVENT_SOURCE_SYSTEM, (int32_t)provider );
    return ESP_OK;
}

void input_poll( void )
{
    input_trace_drain();
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
        ( now_us - pressed_us ) >= RFSCOUT_INPUT_LOCK_HOLD_US )
    {
        if( locked )
        {
            button_lock_fired = true;
            button_lock_sequence = press_sequence;
            input_set_locked( false );
        }
        else
        {
            queue_global_hold( press_sequence );
        }
    }

    if( completed_duration_us < INPUT_BUTTON_DEBOUNCE_US ||
        completed_sequence == button_lock_sequence )
    {
        return;
    }
    if( completed_duration_us >= RFSCOUT_INPUT_LOCK_HOLD_US )
    {
        if( locked )
        {
            button_lock_fired = true;
            button_lock_sequence = completed_sequence;
            input_set_locked( false );
        }
        else
        {
            queue_global_hold( completed_sequence );
        }
        return;
    }
    if( locked )
    {
        return;
    }

    if( completed_duration_us >= INPUT_BUTTON_LONG_PRESS_US )
    {
        button_long_press_event = true;
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
    movement = rotary_decoder_pending( &navigation_decoder );
    portEXIT_CRITICAL( &navigation_lock );

    portENTER_CRITICAL( &button_lock );
    button_activity = button_down || ( button_pending_duration_us > 0 );
    portEXIT_CRITICAL( &button_lock );

    const bool user_key =
        RFSCOUT_BOARD_USER_KEY_PIN >= 0 && 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN );
    return movement || button_activity || button_event || button_long_press_event ||
           button_global_hold_event || user_key;
}

void input_consume_wake( void )
{
    input_trace_note( "wake_consume", 0, 0 );
    clear_navigation_delta();
    button_event = false;
    button_long_press_event = false;
    button_global_hold_event = false;

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
    if( button_global_hold_event )
    {
        button_global_hold_event = false;
        a_event->type = RFSCOUT_INPUT_GLOBAL_HOLD;
        return true;
    }
    if( consume_button_event( &button_long_press_event ) )
    {
        a_event->type = RFSCOUT_INPUT_LONG_PRESS;
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
