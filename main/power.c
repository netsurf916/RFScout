/**
    power.c : RFScout user-button power control
    Description: Implements tap-to-sleep and hold-to-power-off behavior.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "power.h"

#include "battery.h"
#include "board.h"
#include "log_writer.h"
#include "storage.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"

#define POWER_DEBOUNCE_US          30000LL
#define POWER_COUNTDOWN_START_US  500000LL
#define POWER_SHUTDOWN_HOLD_US   3000000LL
#define POWER_LOG_FLUSH_TIMEOUT_MS 3000U

static const char *TAG = "power";
static bool raw_pressed;
static bool stable_pressed;
static int64_t raw_changed_us;
static int64_t pressed_since_us;
static power_status_t status;

/** @brief Stop logging and unmount storage only after the writer is quiescent. */
static bool prepare_storage_for_power_state( void )
{
    if( !storage_is_available() )
    {
        return true;
    }

    if( !log_writer_shutdown( POWER_LOG_FLUSH_TIMEOUT_MS ) )
    {
        ESP_LOGW( TAG, "storage remains mounted because logger did not stop" );
        return false;
    }

    storage_shutdown();
    return true;
}

/** @brief Restore SD logging after a failed physical power-off request. */
static void restore_storage_after_failed_power_off( void )
{
    if( ESP_OK != storage_init() )
    {
        ESP_LOGE( TAG, "could not remount SD after failed power off" );
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
    if( !prepare_storage_for_power_state() )
    {
        ESP_LOGW( TAG, "sleep cancelled because SD logger is still active" );
        return;
    }

    ESP_ERROR_CHECK( esp_sleep_enable_ext1_wakeup_io(
        1ULL << RFSCOUT_BOARD_USER_KEY_PIN,
        ESP_EXT1_WAKEUP_ANY_LOW ) );
    esp_deep_sleep_start();
}

/** @brief Update the UI-visible hold countdown. */
static void update_countdown( int64_t a_held_us )
{
    if( a_held_us < POWER_COUNTDOWN_START_US )
    {
        status.state = POWER_HOLD_IDLE;
        status.seconds_remaining = 0U;
        return;
    }

    status.state = POWER_HOLD_COUNTDOWN;
    if( a_held_us < 1000000LL ) status.seconds_remaining = 3U;
    else if( a_held_us < 2000000LL ) status.seconds_remaining = 2U;
    else status.seconds_remaining = 1U;
}

esp_err_t power_init( void )
{
    raw_pressed = ( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) );
    stable_pressed = raw_pressed;
    raw_changed_us = esp_timer_get_time();
    pressed_since_us = stable_pressed ? raw_changed_us : 0;
    status.state = POWER_HOLD_IDLE;
    status.seconds_remaining = 0U;
    return ESP_OK;
}

void power_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    const bool pressed = ( 0 == gpio_get_level( RFSCOUT_BOARD_USER_KEY_PIN ) );

    if( pressed != raw_pressed )
    {
        raw_pressed = pressed;
        raw_changed_us = now_us;
    }

    if( ( raw_pressed != stable_pressed ) &&
        ( ( now_us - raw_changed_us ) >= POWER_DEBOUNCE_US ) )
    {
        stable_pressed = raw_pressed;
        if( stable_pressed )
        {
            pressed_since_us = now_us;
        }
        else
        {
            const int64_t held_us = now_us - pressed_since_us;
            status.state = POWER_HOLD_IDLE;
            status.seconds_remaining = 0U;
            pressed_since_us = 0;
            if( held_us < POWER_COUNTDOWN_START_US ) enter_sleep();
        }
    }

    if( !stable_pressed || ( 0 == pressed_since_us ) ) return;

    const int64_t held_us = now_us - pressed_since_us;
    if( held_us >= POWER_SHUTDOWN_HOLD_US )
    {
        status.state = POWER_HOLD_SHUTDOWN;
        status.seconds_remaining = 0U;
        ESP_LOGI( TAG, "user key held for 3 seconds; requesting power off" );

        if( !prepare_storage_for_power_state() )
        {
            status.state = POWER_HOLD_IDLE;
            return;
        }

        const esp_err_t result = battery_shutdown();
        if( ESP_OK != result )
        {
            ESP_LOGE( TAG, "power off failed: %s", esp_err_to_name( result ) );
            restore_storage_after_failed_power_off();
            status.state = POWER_HOLD_IDLE;
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
