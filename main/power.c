/**
    power.c : RFScout user-button power control
    Description: Implements tap-to-sleep and hold-to-power-off behavior.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "power.h"

#include "app_manager.h"
#include "battery.h"
#include "board.h"
#include "device_config.h"
#include "device_time.h"
#include "display.h"
#include "log_writer.h"
#include "platform.h"
#include "recorder.h"
#include "session_state.h"
#include "storage.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"

#define POWER_DEBOUNCE_US 30000LL
#define POWER_COUNTDOWN_START_US 500000LL
#define POWER_SHUTDOWN_HOLD_US 3000000LL
#define POWER_LOG_FLUSH_TIMEOUT_MS 3000U
#define POWER_RECORDER_STOP_TIMEOUT_MS 5000U

static const char *TAG = "power";
static bool raw_pressed;
static bool stable_pressed;
static int64_t raw_changed_us;
static int64_t pressed_since_us;
static bool suppress_current_press;
static power_status_t status;

/** @brief Update UI-visible power state and invalidate the composed display when it changes. */
static void set_power_status( power_hold_state_t a_state, uint8_t a_seconds_remaining )
{
    if( ( status.state == a_state ) && ( status.seconds_remaining == a_seconds_remaining ) )
    {
        return;
    }

    status.state = a_state;
    status.seconds_remaining = a_seconds_remaining;
    display_invalidate();
}

/** @brief Stop logging, optionally checkpoint wall time, and unmount storage. */
static bool prepare_storage_for_power_state( bool a_checkpoint_time )
{
    if( !storage_is_available() )
    {
        return true;
    }

    /* Deep sleep is allowed directly from an open settings page. Flush any
       canonical user-configuration changes before stopping storage users so a
       save failure leaves the running session intact. */
    if( !a_checkpoint_time )
    {
        const esp_err_t save_result = device_config_save_if_dirty();
        if( ESP_OK != save_result )
        {
            ESP_LOGW(
                TAG, "configuration save before sleep failed: %s", esp_err_to_name( save_result ) );
            return false;
        }
    }

    if( !recorder_prepare_storage_shutdown( POWER_RECORDER_STOP_TIMEOUT_MS ) )
    {
        ESP_LOGW( TAG, "storage remains mounted because Recorder did not stop" );
        return false;
    }

    if( !log_writer_shutdown( POWER_LOG_FLUSH_TIMEOUT_MS ) )
    {
        ESP_LOGW( TAG, "storage remains mounted because logger did not stop" );
        return false;
    }

    if( a_checkpoint_time )
    {
        const esp_err_t checkpoint = device_time_checkpoint();
        if( ESP_OK != checkpoint )
        {
            ESP_LOGW( TAG, "wall-clock checkpoint failed: %s", esp_err_to_name( checkpoint ) );
        }
    }

    storage_shutdown();
    return true;
}

/** @brief Restore SD state after a failed physical power-off request. */
static void restore_storage_after_failed_power_off( void )
{
    if( ESP_OK != storage_init() )
    {
        ESP_LOGE( TAG, "could not remount SD after failed power off" );
        return;
    }

    if( !device_config_get()->logging_enabled )
    {
        return;
    }
    if( ESP_OK != log_writer_init() )
    {
        ESP_LOGE( TAG, "could not restart logging after failed power off" );
    }
}

/** @brief Enter deep sleep and wake when the active-low user key is pressed. */
static void enter_sleep( void )
{
    ESP_LOGI( TAG, "entering deep sleep; user key wakes device" );
    if( !prepare_storage_for_power_state( false ) )
    {
        ESP_LOGW( TAG, "sleep cancelled because persistent state could not be prepared" );
        return;
    }

    session_state_store( app_manager_current(), platform_input_locked() );

    ESP_ERROR_CHECK( esp_sleep_enable_ext1_wakeup_io( 1ULL << RFSCOUT_BOARD_USER_KEY_PIN,
                                                      ESP_EXT1_WAKEUP_ANY_LOW ) );
    esp_deep_sleep_start();
}

/** @brief Update the UI-visible hold countdown. */
static void update_countdown( int64_t a_held_us )
{
    if( a_held_us < POWER_COUNTDOWN_START_US )
    {
        set_power_status( POWER_HOLD_IDLE, 0U );
        return;
    }

    if( a_held_us < 1000000LL )
    {
        set_power_status( POWER_HOLD_COUNTDOWN, 3U );
    }
    else if( a_held_us < 2000000LL )
    {
        set_power_status( POWER_HOLD_COUNTDOWN, 2U );
    }
    else
    {
        set_power_status( POWER_HOLD_COUNTDOWN, 1U );
    }
}

esp_err_t power_init( void )
{
    raw_pressed = ( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) );
    stable_pressed = raw_pressed;
    raw_changed_us = esp_timer_get_time();
    pressed_since_us = stable_pressed ? raw_changed_us : 0;
    suppress_current_press = false;
    status.state = POWER_HOLD_IDLE;
    status.seconds_remaining = 0U;
    return ESP_OK;
}

void power_consume_wake_input( void )
{
    if( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) )
    {
        suppress_current_press = true;
        pressed_since_us = 0;
        set_power_status( POWER_HOLD_IDLE, 0U );
    }
}

void power_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    const bool pressed = ( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) );

    if( suppress_current_press )
    {
        raw_pressed = pressed;
        stable_pressed = pressed;
        raw_changed_us = now_us;
        pressed_since_us = 0;
        set_power_status( POWER_HOLD_IDLE, 0U );
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
        }
        else
        {
            const int64_t held_us = now_us - pressed_since_us;
            set_power_status( POWER_HOLD_IDLE, 0U );
            pressed_since_us = 0;
            if( held_us < POWER_COUNTDOWN_START_US )
            {
                enter_sleep();
            }
        }
    }

    if( !stable_pressed || ( 0 == pressed_since_us ) )
    {
        return;
    }

    const int64_t held_us = now_us - pressed_since_us;
    if( held_us >= POWER_SHUTDOWN_HOLD_US )
    {
        set_power_status( POWER_HOLD_SHUTDOWN, 0U );
        ESP_LOGI( TAG, "user key held for 3 seconds; requesting power off" );

        if( !prepare_storage_for_power_state( true ) )
        {
            set_power_status( POWER_HOLD_IDLE, 0U );
            return;
        }

        const esp_err_t result = battery_shutdown();
        if( ESP_OK != result )
        {
            ESP_LOGE( TAG, "power off failed: %s", esp_err_to_name( result ) );
            restore_storage_after_failed_power_off();
            set_power_status( POWER_HOLD_IDLE, 0U );
        }
        return;
    }

    update_countdown( held_us );
}

void power_get_status( power_status_t *a_status )
{
    if( NULL != a_status )
    {
        *a_status = status;
    }
}
