/**
    display_idle.c : RFScout global display-idle policy
    Description: Keeps application execution independent from LCD dim/off presentation state.
    Copyright 2026 Daniel Wilson
    SPDX-License-Identifier: MIT
*/

#include "display_idle.h"

#include "board.h"
#include "device_config.h"
#include "display.h"
#include "input.h"
#include "power.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdint.h>

#define DISPLAY_IDLE_US_PER_SECOND 1000000LL
#define DISPLAY_IDLE_US_PER_MILLISECOND 1000LL
#define DISPLAY_IDLE_WAKE_GUARD_US 200000LL
#define DISPLAY_IDLE_DIM_DIVISOR 4U
#define DISPLAY_IDLE_DIM_MIN_PERCENT 1U
#define DISPLAY_IDLE_PIXEL_SHIFT_FALLBACK_SECONDS 120U
#define DISPLAY_IDLE_PIXEL_SHIFT_INTERVAL_SECONDS 60U

static const char *TAG = "display_idle";

typedef enum
{
    DISPLAY_IDLE_ACTIVE = 0,
    DISPLAY_IDLE_DIMMED,
    DISPLAY_IDLE_OFF
} display_idle_state_t;

static display_idle_state_t state;
static display_idle_state_t attention_restore_state;
static int64_t last_activity_us;
static int64_t attention_until_us;
static int64_t pixel_shift_last_advance_us;
static bool attention_restore_pending;
static bool off_requested;
static bool keep_awake;
static int64_t wake_guard_until_us;

static void reset_pixel_shift( int64_t a_now_us )
{
    display_pixel_shift_reset();
    pixel_shift_last_advance_us = a_now_us;
}

static void update_pixel_shift( int64_t a_now_us, int64_t a_idle_us, uint16_t a_dim_seconds )
{
    const uint16_t start_seconds =
        ( 0U != a_dim_seconds ) ? a_dim_seconds : DISPLAY_IDLE_PIXEL_SHIFT_FALLBACK_SECONDS;
    const int64_t start_us = (int64_t)start_seconds * DISPLAY_IDLE_US_PER_SECOND;
    if( a_idle_us < start_us )
    {
        return;
    }

    const int64_t shift_start_us = last_activity_us + start_us;
    if( pixel_shift_last_advance_us < shift_start_us ||
        a_now_us - pixel_shift_last_advance_us >=
            (int64_t)DISPLAY_IDLE_PIXEL_SHIFT_INTERVAL_SECONDS * DISPLAY_IDLE_US_PER_SECOND )
    {
        display_pixel_shift_advance();
        pixel_shift_last_advance_us = a_now_us;
    }
}

static uint8_t dim_brightness( uint8_t a_normal_percent )
{
    uint8_t dimmed = (uint8_t)( a_normal_percent / DISPLAY_IDLE_DIM_DIVISOR );
    if( dimmed < DISPLAY_IDLE_DIM_MIN_PERCENT )
    {
        dimmed = DISPLAY_IDLE_DIM_MIN_PERCENT;
    }
    return dimmed;
}

static void set_dimmed( void )
{
    const uint8_t brightness = device_config_get()->brightness_percent;
    display_set_brightness( dim_brightness( brightness ) );
    state = DISPLAY_IDLE_DIMMED;
    ESP_LOGI( TAG, "display dimmed after inactivity" );
}

static esp_err_t enter_off_state( const char *a_reason )
{
    if( DISPLAY_IDLE_OFF == state )
    {
        return ESP_OK;
    }

    const device_config_t *config = device_config_get();
    const uint8_t previous_brightness = ( DISPLAY_IDLE_DIMMED == state )
                                            ? dim_brightness( config->brightness_percent )
                                            : config->brightness_percent;

    display_set_brightness( 0U );
    const esp_err_t result = display_enter_low_power();
    if( ESP_OK != result )
    {
        display_set_brightness( previous_brightness );
        ESP_LOGW( TAG, "could not put display into low power: %s", esp_err_to_name( result ) );
        return result;
    }

    state = DISPLAY_IDLE_OFF;
    const rfscout_display_info_t *info = display_info();
    ESP_LOGI( TAG,
              "display entered %s low power (%s)",
              ( NULL != info && NULL != info->name ) ? info->name : "provider",
              ( NULL != a_reason ) ? a_reason : "requested" );
    return ESP_OK;
}

static bool wake_display( const char *a_reason )
{
    const device_config_t *config = device_config_get();
    if( DISPLAY_IDLE_OFF == state )
    {
        const esp_err_t enabled = display_exit_low_power();
        if( ESP_OK != enabled )
        {
            ESP_LOGW(
                TAG, "could not wake display from low power: %s", esp_err_to_name( enabled ) );
            return false;
        }

        /* Keep the backlight dark until the latest application framebuffer has
           replaced the panel's pre-timeout contents. */
        display_invalidate();
        const esp_err_t presented = display_present();
        if( ESP_OK != presented )
        {
            ESP_LOGW( TAG, "wake presentation failed: %s", esp_err_to_name( presented ) );
            const esp_err_t sleeping = display_enter_low_power();
            if( ESP_OK != sleeping )
            {
                ESP_LOGW( TAG,
                          "could not restore display low-power state after wake failure: %s",
                          esp_err_to_name( sleeping ) );
            }
            return false;
        }
    }

    display_set_brightness( config->brightness_percent );
    state = DISPLAY_IDLE_ACTIVE;
    ESP_LOGI( TAG, "display woke (%s)", ( NULL != a_reason ) ? a_reason : "requested" );
    return true;
}

static void cancel_attention_restore( void )
{
    attention_until_us = 0;
    attention_restore_pending = false;
    attention_restore_state = DISPLAY_IDLE_ACTIVE;
}

static void restore_after_attention( void )
{
    if( !attention_restore_pending )
    {
        return;
    }

    const display_idle_state_t restore_state = attention_restore_state;
    attention_restore_pending = false;
    attention_restore_state = DISPLAY_IDLE_ACTIVE;

    if( DISPLAY_IDLE_OFF == restore_state )
    {
        (void)enter_off_state( "attention expired" );
    }
    else if( DISPLAY_IDLE_DIMMED == restore_state )
    {
        set_dimmed();
    }
}

esp_err_t display_idle_init( void )
{
    state = DISPLAY_IDLE_ACTIVE;
    attention_restore_state = DISPLAY_IDLE_ACTIVE;
    last_activity_us = esp_timer_get_time();
    attention_until_us = 0;
    pixel_shift_last_advance_us = last_activity_us;
    attention_restore_pending = false;
    off_requested = false;
    /* Display policy initializes before any application enters its runtime. */
    keep_awake = false;
    wake_guard_until_us = 0;
    display_pixel_shift_reset();
    const esp_err_t result = display_exit_low_power();
    display_set_brightness( device_config_get()->brightness_percent );
    return result;
}

void display_idle_set_keep_awake( bool a_enabled )
{
    keep_awake = a_enabled;
    if( keep_awake && !off_requested )
    {
        (void)display_idle_request_wake();
    }
    if( !keep_awake )
    {
        last_activity_us = esp_timer_get_time();
    }
}

esp_err_t display_idle_request_off( void )
{
    cancel_attention_restore();
    off_requested = true;
    return ESP_OK;
}

esp_err_t display_idle_request_wake( void )
{
    off_requested = false;
    cancel_attention_restore();
    last_activity_us = esp_timer_get_time();
    reset_pixel_shift( last_activity_us );
    if( DISPLAY_IDLE_ACTIVE == state )
    {
        display_set_brightness( device_config_get()->brightness_percent );
        return ESP_OK;
    }
    return wake_display( "device request" ) ? ESP_OK : ESP_FAIL;
}

esp_err_t display_idle_request_attention( uint32_t a_duration_ms )
{
    if( 0U == a_duration_ms )
    {
        return ESP_ERR_INVALID_ARG;
    }

    const int64_t now_us = esp_timer_get_time();
    const int64_t requested_until =
        now_us + (int64_t)a_duration_ms * DISPLAY_IDLE_US_PER_MILLISECOND;
    if( requested_until > attention_until_us )
    {
        attention_until_us = requested_until;
    }

    if( off_requested )
    {
        attention_restore_state = DISPLAY_IDLE_OFF;
        attention_restore_pending = true;
        off_requested = false;
    }
    else if( !attention_restore_pending )
    {
        attention_restore_state = state;
        attention_restore_pending = ( DISPLAY_IDLE_ACTIVE != state );
    }

    if( DISPLAY_IDLE_ACTIVE == state )
    {
        display_set_brightness( device_config_get()->brightness_percent );
        return ESP_OK;
    }
    return wake_display( "system event" ) ? ESP_OK : ESP_FAIL;
}

bool display_idle_input_should_wake( void )
{
    const int64_t now_us = esp_timer_get_time();
    if( DISPLAY_IDLE_ACTIVE != state )
    {
        /* An edge can arrive after display_idle_poll but before input_get.
           Wake first even in this race, never dispatch navigation in the dark. */
        input_consume_wake();
        wake_guard_until_us = now_us + DISPLAY_IDLE_WAKE_GUARD_US;
        cancel_attention_restore();
        reset_pixel_shift( now_us );
        last_activity_us = now_us;
        (void)wake_display( "user activity" );
        return true;
    }
    if( now_us < wake_guard_until_us )
    {
        input_consume_wake();
        return true;
    }
    return false;
}

void display_idle_poll( void )
{
    const int64_t now_us = esp_timer_get_time();
    const bool input_activity = input_activity_pending();
    if( input_activity )
    {
        cancel_attention_restore();
        reset_pixel_shift( now_us );
        if( DISPLAY_IDLE_ACTIVE != state )
        {
            wake_guard_until_us = now_us + DISPLAY_IDLE_WAKE_GUARD_US;
            input_consume_wake();
            if( RFSCOUT_BOARD_USER_KEY_PIN >= 0 )
            {
                power_consume_wake_input();
            }
            (void)wake_display( "user activity" );
        }
        last_activity_us = now_us;
        return;
    }

    if( off_requested )
    {
        const esp_err_t result = enter_off_state( "explicit request" );
        if( ESP_OK == result )
        {
            off_requested = false;
            last_activity_us = now_us;
        }
        return;
    }

    if( now_us < attention_until_us )
    {
        return;
    }
    if( 0 != attention_until_us )
    {
        attention_until_us = 0;
        restore_after_attention();
        if( DISPLAY_IDLE_ACTIVE != state )
        {
            return;
        }
    }

    const device_config_t *config = device_config_get();
    if( keep_awake )
    {
        return;
    }
    const int64_t idle_us = now_us - last_activity_us;
    const uint16_t off_seconds = config->display_off_timeout_seconds;
    const uint16_t dim_seconds = config->display_dim_timeout_seconds;

    if( ( 0U != off_seconds ) && ( idle_us >= (int64_t)off_seconds * DISPLAY_IDLE_US_PER_SECOND ) )
    {
        if( DISPLAY_IDLE_OFF != state )
        {
            (void)enter_off_state( "inactivity" );
        }
        return;
    }

    update_pixel_shift( now_us, idle_us, dim_seconds );

    if( ( DISPLAY_IDLE_ACTIVE == state ) && ( 0U != dim_seconds ) &&
        ( idle_us >= (int64_t)dim_seconds * DISPLAY_IDLE_US_PER_SECOND ) )
    {
        set_dimmed();
    }
}
