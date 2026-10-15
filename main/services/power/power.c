/**
    power.c : RFScout semantic power control
    Description: Implements configurable short/long user-button gestures and board-supported power actions.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "power.h"

#include "app_manager.h"
#include "battery.h"
#include "board.h"
#include "board_i2c.h"
#include "breadcrumbs.h"
#include "device_config.h"
#include "display.h"
#include "display_idle.h"
#include "log_writer.h"
#include "media_store.h"
#include "input.h"
#include "platform.h"
#include "recorder.h"
#include "session_state.h"
#include "system_lifecycle.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

#define POWER_DEBOUNCE_US 30000LL
#define POWER_SHORT_PRESS_MAX_US 500000LL
#define POWER_LONG_PRESS_HOLD_US 3000000LL
#define POWER_LOG_FLUSH_TIMEOUT_MS 3000U
#define POWER_BREADCRUMB_SHUTDOWN_TIMEOUT_MS 3000U
#define POWER_RECORDER_STOP_TIMEOUT_MS 5000U

static const char *TAG = "power";
static bool raw_pressed;
static bool stable_pressed;
static int64_t raw_changed_us;
static int64_t pressed_since_us;
static bool suppress_current_press;
static bool long_action_fired;
static power_status_t status;
static bool requested_transition_pending;
static power_transition_reason_t requested_transition_reason;
static power_action_t requested_transition_action;
static int64_t requested_transition_deadline_us;
static char requested_transition_message[ POWER_STATUS_MESSAGE_MAX ];

static const char *power_action_prompt_name( power_action_t a_action )
{
    switch( a_action )
    {
        case POWER_ACTION_SCREEN_OFF:
            return "SCREEN OFF";
        case POWER_ACTION_SLEEP:
            return "SLEEP";
        case POWER_ACTION_POWER_OFF:
            return "POWER OFF";
        case POWER_ACTION_NONE:
        case POWER_ACTION_COUNT:
        default:
            return "NONE";
    }
}

const char *power_action_name( power_action_t a_action )
{
    switch( a_action )
    {
        case POWER_ACTION_SCREEN_OFF:
            return "Screen off";
        case POWER_ACTION_SLEEP:
            return "Sleep";
        case POWER_ACTION_POWER_OFF:
            return "Power off";
        case POWER_ACTION_NONE:
            return "None";
        case POWER_ACTION_COUNT:
        default:
            return "Unknown";
    }
}

bool power_action_available( power_action_t a_action )
{
    switch( a_action )
    {
        case POWER_ACTION_NONE:
            return true;
        case POWER_ACTION_SCREEN_OFF:
            return 0 != RFSCOUT_BOARD_CAN_SCREEN_OFF;
        case POWER_ACTION_SLEEP:
            return 0 != RFSCOUT_BOARD_CAN_DEEP_SLEEP;
        case POWER_ACTION_POWER_OFF:
            return 0 != RFSCOUT_BOARD_CAN_POWER_OFF;
        case POWER_ACTION_COUNT:
        default:
            return false;
    }
}

bool power_action_supported( power_action_t a_action, bool a_long_press )
{
    if( !power_action_available( a_action ) )
    {
        return false;
    }
    if( a_long_press )
    {
        return ( POWER_ACTION_NONE == a_action ) || ( POWER_ACTION_POWER_OFF == a_action );
    }
    return true;
}

/** @brief Update UI-visible power state and invalidate the composed display when it changes. */
static void set_power_status( power_hold_state_t a_state,
                              power_action_t a_action,
                              power_transition_reason_t a_reason,
                              uint8_t a_seconds_remaining,
                              const char *a_message,
                              const char *a_detail )
{
    const char *message = ( NULL != a_message ) ? a_message : "";
    const char *detail = ( NULL != a_detail ) ? a_detail : "";
    if( ( status.state == a_state ) && ( status.action == a_action ) &&
        ( status.reason == a_reason ) && ( status.seconds_remaining == a_seconds_remaining ) &&
        ( 0 == strcmp( status.message, message ) ) && ( 0 == strcmp( status.detail, detail ) ) )
    {
        return;
    }

    status.state = a_state;
    status.action = a_action;
    status.reason = a_reason;
    status.seconds_remaining = a_seconds_remaining;
    snprintf( status.message, sizeof( status.message ), "%s", message );
    snprintf( status.detail, sizeof( status.detail ), "%s", detail );
    display_invalidate();
}

static void set_idle_status( void )
{
    set_power_status( POWER_HOLD_IDLE, POWER_ACTION_NONE, POWER_TRANSITION_NONE, 0U, "", "" );
}

/** @brief Put the LCD into its lowest software-controlled power state before SoC sleep/shutdown. */
static void prepare_display_for_power_state( void )
{
    display_set_brightness( 0U );
    const esp_err_t result = display_enter_low_power();
    if( ESP_OK != result )
    {
        ESP_LOGW( TAG, "could not put LCD into hardware sleep: %s", esp_err_to_name( result ) );
    }
}

/** @brief Restore the display if a physical power-off request fails. */
static void restore_display_after_failed_power_off( void )
{
    const esp_err_t result = display_exit_low_power();
    if( ESP_OK != result )
    {
        ESP_LOGE( TAG, "could not wake LCD after failed power off: %s", esp_err_to_name( result ) );
        return;
    }

    display_invalidate();
    const esp_err_t presented = display_present();
    if( ESP_OK != presented )
    {
        ESP_LOGE(
            TAG, "could not refresh LCD after failed power off: %s", esp_err_to_name( presented ) );
        return;
    }
    display_set_brightness( device_config_get()->brightness_percent );
}

/** @brief Enter deep sleep and wake when the active-low user key is pressed. */
static void enter_sleep( void )
{
    ESP_LOGI( TAG, "entering deep sleep; user key wakes device" );
    if( !system_lifecycle_prepare( SYSTEM_LIFECYCLE_DEEP_SLEEP ) )
    {
        ESP_LOGW( TAG, "sleep cancelled because persistent state could not be prepared" );
        set_idle_status();
        return;
    }

    session_state_store( app_manager_current(), input_locked() );
    prepare_display_for_power_state();

    ESP_ERROR_CHECK( esp_sleep_enable_ext1_wakeup_io( 1ULL << RFSCOUT_BOARD_USER_KEY_PIN,
                                                      ESP_EXT1_WAKEUP_ANY_LOW ) );
    esp_deep_sleep_start();
}

/** @brief Perform an orderly physical power-off request. */
static void power_off( bool a_thermal_fallback )
{
    if( !system_lifecycle_prepare( SYSTEM_LIFECYCLE_POWER_OFF ) )
    {
        set_idle_status();
        return;
    }

    prepare_display_for_power_state();
    const esp_err_t result = battery_shutdown();
    if( ESP_OK != result )
    {
        ESP_LOGE( TAG, "power off failed: %s", esp_err_to_name( result ) );
        system_lifecycle_abort( SYSTEM_LIFECYCLE_POWER_OFF );
        restore_display_after_failed_power_off();
        if( a_thermal_fallback && power_action_available( POWER_ACTION_SLEEP ) )
        {
            ESP_LOGW( TAG, "thermal power-off unavailable; falling back to deep sleep" );
            enter_sleep();
            return;
        }
        set_idle_status();
    }
}

/** @brief Execute one board-supported semantic action. */
static void execute_action( power_action_t a_action, bool a_long_press )
{
    if( !power_action_supported( a_action, a_long_press ) )
    {
        ESP_LOGW(
            TAG, "unsupported power action=%d long=%u", (int)a_action, a_long_press ? 1U : 0U );
        set_idle_status();
        return;
    }

    switch( a_action )
    {
        case POWER_ACTION_NONE:
            set_idle_status();
            return;
        case POWER_ACTION_SCREEN_OFF:
            if( ESP_OK != display_idle_request_off() )
            {
                ESP_LOGW( TAG, "display-off request failed" );
            }
            set_idle_status();
            return;
        case POWER_ACTION_SLEEP:
            enter_sleep();
            return;
        case POWER_ACTION_POWER_OFF:
            set_power_status(
                POWER_HOLD_EXECUTING, a_action, POWER_TRANSITION_NONE, 0U, "POWERING OFF...", "" );
            (void)display_present();
            power_off( false );
            return;
        case POWER_ACTION_COUNT:
        default:
            set_idle_status();
            return;
    }
}

/** @brief Update the UI-visible countdown for a configured long action. */
static void update_countdown( int64_t a_held_us, power_action_t a_action )
{
    if( ( a_held_us < POWER_SHORT_PRESS_MAX_US ) || ( POWER_ACTION_NONE == a_action ) ||
        !power_action_supported( a_action, true ) )
    {
        set_idle_status();
        return;
    }

    uint8_t remaining = 1U;
    if( a_held_us < 1000000LL )
    {
        remaining = 3U;
    }
    else if( a_held_us < 2000000LL )
    {
        remaining = 2U;
    }

    char message[ POWER_STATUS_MESSAGE_MAX ];
    snprintf( message, sizeof( message ), "HOLD TO %s", power_action_prompt_name( a_action ) );
    set_power_status(
        POWER_HOLD_COUNTDOWN, a_action, POWER_TRANSITION_NONE, remaining, message, "" );
}

esp_err_t power_init( void )
{
    raw_pressed = ( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) );
    stable_pressed = raw_pressed;
    raw_changed_us = esp_timer_get_time();
    pressed_since_us = stable_pressed ? raw_changed_us : 0;
    suppress_current_press = false;
    long_action_fired = false;
    memset( &status, 0, sizeof( status ) );
    status.state = POWER_HOLD_IDLE;
    status.action = POWER_ACTION_NONE;
    status.reason = POWER_TRANSITION_NONE;
    requested_transition_pending = false;
    requested_transition_reason = POWER_TRANSITION_NONE;
    requested_transition_action = POWER_ACTION_NONE;
    requested_transition_deadline_us = 0;
    requested_transition_message[ 0 ] = '\0';
    return ESP_OK;
}

bool power_request_transition( power_transition_reason_t a_reason,
                               power_action_t a_action,
                               const char *a_message,
                               uint8_t a_delay_seconds )
{
    if( POWER_TRANSITION_NONE == a_reason || POWER_ACTION_NONE == a_action || NULL == a_message ||
        '\0' == a_message[ 0 ] || 0U == a_delay_seconds || !power_action_available( a_action ) )
    {
        return false;
    }

    if( requested_transition_pending )
    {
        if( requested_transition_reason == a_reason )
        {
            return true;
        }
        if( POWER_TRANSITION_HIGH_TEMPERATURE == requested_transition_reason &&
            POWER_TRANSITION_LOW_BATTERY == a_reason )
        {
            return false;
        }
    }

    requested_transition_pending = true;
    requested_transition_reason = a_reason;
    requested_transition_action = a_action;
    requested_transition_deadline_us = esp_timer_get_time() + (int64_t)a_delay_seconds * 1000000LL;
    snprintf(
        requested_transition_message, sizeof( requested_transition_message ), "%s", a_message );
    const esp_err_t wake_result = display_idle_request_wake();
    if( ESP_OK != wake_result )
    {
        ESP_LOGW( TAG,
                  "could not wake display for protective transition: %s",
                  esp_err_to_name( wake_result ) );
    }
    display_invalidate();
    return true;
}

void power_cancel_transition( power_transition_reason_t a_reason )
{
    if( !requested_transition_pending ||
        ( POWER_TRANSITION_NONE != a_reason && requested_transition_reason != a_reason ) )
    {
        return;
    }
    requested_transition_pending = false;
    requested_transition_reason = POWER_TRANSITION_NONE;
    requested_transition_action = POWER_ACTION_NONE;
    requested_transition_deadline_us = 0;
    requested_transition_message[ 0 ] = '\0';
    set_idle_status();
}

static void format_requested_detail( power_action_t a_action,
                                     uint8_t a_seconds_remaining,
                                     bool a_executing,
                                     char *a_output,
                                     size_t a_capacity )
{
    if( a_executing )
    {
        switch( a_action )
        {
            case POWER_ACTION_SLEEP:
                snprintf( a_output, a_capacity, "SLEEPING..." );
                return;
            case POWER_ACTION_POWER_OFF:
                snprintf( a_output, a_capacity, "POWERING OFF..." );
                return;
            case POWER_ACTION_SCREEN_OFF:
                snprintf( a_output, a_capacity, "SCREEN OFF..." );
                return;
            case POWER_ACTION_NONE:
            case POWER_ACTION_COUNT:
            default:
                snprintf( a_output, a_capacity, "NO ACTION" );
                return;
        }
    }

    const char *name = power_action_prompt_name( a_action );
    snprintf( a_output, a_capacity, "%s IN %u", name, (unsigned)a_seconds_remaining );
}

static void execute_policy_action( power_action_t a_action,
                                   power_transition_reason_t a_reason )
{
    if( !power_action_available( a_action ) )
    {
        set_idle_status();
        return;
    }
    switch( a_action )
    {
        case POWER_ACTION_SCREEN_OFF:
            (void)display_idle_request_off();
            set_idle_status();
            return;
        case POWER_ACTION_SLEEP:
            enter_sleep();
            return;
        case POWER_ACTION_POWER_OFF:
            power_off( POWER_TRANSITION_HIGH_TEMPERATURE == a_reason );
            return;
        case POWER_ACTION_NONE:
        case POWER_ACTION_COUNT:
        default:
            set_idle_status();
            return;
    }
}

/** @brief Revalidate hardware-sensitive policy actions immediately before execution. */
static power_action_t resolve_policy_action_at_execution( power_transition_reason_t a_reason,
                                                          power_action_t a_action )
{
    if( ( POWER_TRANSITION_HIGH_TEMPERATURE != a_reason ) ||
        ( POWER_ACTION_POWER_OFF != a_action ) || !power_action_available( POWER_ACTION_SLEEP ) )
    {
        return a_action;
    }

    battery_refresh();
    battery_status_t battery;
    if( !board_i2c_ready() || !battery_get_status( &battery ) || !battery.charger_fresh ||
        battery.external_power )
    {
        ESP_LOGW( TAG, "thermal power-off no longer has confirmed removable power; using sleep" );
        return POWER_ACTION_SLEEP;
    }
    return a_action;
}

static void update_requested_transition( int64_t a_now_us )
{
    if( !requested_transition_pending )
    {
        return;
    }

    if( a_now_us >= requested_transition_deadline_us )
    {
        const power_transition_reason_t reason = requested_transition_reason;
        const power_action_t action =
            resolve_policy_action_at_execution( reason, requested_transition_action );
        char message[ POWER_STATUS_MESSAGE_MAX ];
        char detail[ POWER_STATUS_DETAIL_MAX ];
        snprintf( message, sizeof( message ), "%s", requested_transition_message );
        format_requested_detail( action, 0U, true, detail, sizeof( detail ) );
        requested_transition_pending = false;
        requested_transition_reason = POWER_TRANSITION_NONE;
        requested_transition_action = POWER_ACTION_NONE;
        requested_transition_deadline_us = 0;
        requested_transition_message[ 0 ] = '\0';
        set_power_status( POWER_HOLD_EXECUTING, action, reason, 0U, message, detail );
        (void)display_present();
        execute_policy_action( action, reason );
        return;
    }

    const int64_t remaining_us = requested_transition_deadline_us - a_now_us;
    uint8_t remaining = (uint8_t)( ( remaining_us + 999999LL ) / 1000000LL );
    if( 0U == remaining )
    {
        remaining = 1U;
    }
    char detail[ POWER_STATUS_DETAIL_MAX ];
    format_requested_detail(
        requested_transition_action, remaining, false, detail, sizeof( detail ) );
    set_power_status( POWER_HOLD_COUNTDOWN,
                      requested_transition_action,
                      requested_transition_reason,
                      remaining,
                      requested_transition_message,
                      detail );
}

void power_consume_wake_input( void )
{
    /* The dedicated power key is intentionally outside the global user-input lock. The lock
       suppresses semantic navigation/selection sources, not physical device power control. */
    if( RFSCOUT_BOARD_USER_KEY_PIN < 0 )
    {
        return;
    }

    if( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) )
    {
        suppress_current_press = true;
        pressed_since_us = 0;
        long_action_fired = false;
        if( !requested_transition_pending )
        {
            set_idle_status();
        }
    }
}

void power_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    const bool pressed = ( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) );

    if( requested_transition_pending )
    {
        update_requested_transition( now_us );
        return;
    }

    if( suppress_current_press )
    {
        raw_pressed = pressed;
        stable_pressed = pressed;
        raw_changed_us = now_us;
        pressed_since_us = 0;
        long_action_fired = false;
        set_idle_status();
        if( !pressed )
        {
            suppress_current_press = false;
        }
        return;
    }

    if( pressed != raw_pressed )
    {
        raw_pressed = pressed;
        raw_changed_us = now_us;
    }

    if( ( raw_pressed != stable_pressed ) && ( ( now_us - raw_changed_us ) >= POWER_DEBOUNCE_US ) )
    {
        stable_pressed = raw_pressed;
        if( stable_pressed )
        {
            pressed_since_us = now_us;
            long_action_fired = false;
        }
        else
        {
            const int64_t held_us = ( 0 != pressed_since_us ) ? now_us - pressed_since_us : 0;
            const bool was_long = long_action_fired;
            pressed_since_us = 0;
            long_action_fired = false;
            set_idle_status();

            if( !was_long && ( held_us < POWER_SHORT_PRESS_MAX_US ) )
            {
                execute_action( device_config_get()->power_button_short_action, false );
            }
            /* Releasing after the short window but before the long threshold is
               deliberate cancellation. It never falls back to the short action. */
        }
    }

    if( !stable_pressed || ( 0 == pressed_since_us ) || long_action_fired )
    {
        return;
    }

    const int64_t held_us = now_us - pressed_since_us;
    const power_action_t long_action = device_config_get()->power_button_long_action;
    if( held_us >= POWER_LONG_PRESS_HOLD_US )
    {
        long_action_fired = true;
        ESP_LOGI(
            TAG, "user key long press completed; action=%s", power_action_name( long_action ) );
        execute_action( long_action, true );
        return;
    }

    update_countdown( held_us, long_action );
}

void power_get_status( power_status_t *a_status )
{
    if( NULL != a_status )
    {
        *a_status = status;
    }
}
